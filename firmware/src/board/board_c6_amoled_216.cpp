// Waveshare ESP32-C6-Touch-AMOLED-2.16 board support.
//
//   Panel   CO5300 480x480 AMOLED, QSPI (CS 15, CLK 0, D0-D3 1-4), no reset GPIO
//   Touch   CST9220-family, I2C 0x5A, INT 5, RST 11
//   PMU     AXP2101, I2C 0x34 — rails, battery, PWR key
//   IMU     QMI8658, I2C 0x6B — auto-rotation
//   I2C     SDA 8, SCL 7          Buttons: BOOT GPIO 9, KEY GPIO 10 (active low)
//
// Reference: Waveshare's ESP32-C6-Touch-AMOLED-2.16 board documentation and
// vendor examples (pin map, panel driving-voltage registers).
#ifndef SIMULATOR
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <Arduino_GFX_Library.h>
#include <XPowersLib.h>
#include <TouchDrvCSTXXX.hpp>
#include <SensorQMI8658.hpp>

namespace {

constexpr int PIN_SDA = 8, PIN_SCL = 7;
constexpr int PIN_LCD_CS = 15, PIN_LCD_CLK = 0;
constexpr int PIN_LCD_D0 = 1, PIN_LCD_D1 = 2, PIN_LCD_D2 = 3, PIN_LCD_D3 = 4;
constexpr int PIN_TP_INT = 5, PIN_TP_RST = 11;
constexpr int PIN_BTN_BOOT = 9, PIN_BTN_KEY = 10;
constexpr uint8_t ADDR_PMU = 0x34, ADDR_TOUCH = 0x5A;

Arduino_DataBus* qspi = nullptr;
Arduino_CO5300*  panel = nullptr;
XPowersPMU       pmu;
TouchDrvCST92xx  touch;
SensorQMI8658    imu;
bool pmu_ok = false, touch_ok = false, imu_ok = false;

// Orientation: the panel rotates itself via MADCTL, so no CPU rotation buffer
// is needed. Index = which way gravity points; 0x30 is upright on a stand.
constexpr uint8_t MADCTL_FOR[4] = { 0x50, 0xF0, 0x90, 0x30 };
uint8_t  orient = 3, orient_candidate = 3;
uint32_t orient_since = 0, imu_last_ms = 0;
bool     orient_changed = false;

BatteryInfo batt = { -1, false, false };
uint32_t batt_last_ms = 0, pkey_last_ms = 0;
PowerKeyEvent pkey_queue[4]; uint8_t pkey_n = 0;

bool btn_down[2] = { false, false };
uint32_t btn_change_ms[2] = { 0, 0 };

void panel_cmd(uint8_t c, uint8_t d) {
    qspi->beginWrite();
    qspi->writeC8D8(c, d);
    qspi->endWrite();
}

void push_pkey(PowerKeyEvent e) {
    if (pkey_n < 4) pkey_queue[pkey_n++] = e;
}

void power_rails_up(void) {
    pmu_ok = pmu.begin(Wire, ADDR_PMU, PIN_SDA, PIN_SCL);
    if (!pmu_ok) { Serial.println("[board] AXP2101 not found"); return; }
    // ALDO1..4 feed the panel, touch and sensors at 3.3 V.
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();
    // ALDO3 doubles as the panel's reset: cycle it for a clean power-on reset.
    pmu.setALDO3Voltage(3300);
    pmu.disableALDO3(); delay(60);
    pmu.enableALDO3();  delay(120);

    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |
                  XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_8S);   // hold 8 s = power off
}

void panel_up(void) {
    qspi = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    panel = new Arduino_CO5300(qspi, GFX_NOT_DEFINED, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 0);
    panel->begin();
    // Panel driving voltages live on manufacturer page 0x20; without them the
    // panel stays dark on this board.
    panel_cmd(0xFE, 0x20);
    panel_cmd(0x19, 0x10);
    panel_cmd(0x1C, 0xA0);
    panel_cmd(0xFE, 0x00);
    panel_cmd(0x36, MADCTL_FOR[orient]);
    panel->fillScreen(0x0000);
    panel->setBrightness(0);
}

void touch_up(void) {
    touch.setPins(PIN_TP_RST, PIN_TP_INT);
    touch_ok = touch.begin(Wire, ADDR_TOUCH, PIN_SDA, PIN_SCL);
    if (!touch_ok) { Serial.println("[board] touch not found"); return; }
    touch.setMaxCoordinates(SCREEN_W, SCREEN_H);
    touch.setSwapXY(true);          // controller axes vs. the 0x30 panel orientation
    touch.setMirrorXY(true, false);
}

void imu_up(void) {
    imu_ok = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, PIN_SDA, PIN_SCL);
    if (!imu_ok) { Serial.println("[board] QMI8658 not found"); return; }
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_LOWPOWER_21Hz,
                            SensorQMI8658::LPF_MODE_3);
    imu.enableAccelerometer();
}

// Gravity direction -> orientation index, or -1 when flat / ambiguous.
int orientation_from(float ax, float ay) {
    float mag = sqrtf(ax * ax + ay * ay);
    if (mag < 0.55f) return -1;                   // lying flat-ish: keep current
    float deg = atan2f(ay, ax) * 57.2958f;        // -180..180
    // Accept only within ±35° of an axis so diagonals don't flicker.
    const float centers[4] = { 0, -90, 180, 90 }; // +X, -Y, -X, +Y
    for (int i = 0; i < 4; i++) {
        float d = fabsf(deg - centers[i]);
        if (d > 180) d = 360 - d;
        if (d <= 35) return i;
    }
    return -1;
}

void poll_imu(uint32_t now) {
    if (!imu_ok || now - imu_last_ms < 120) return;
    imu_last_ms = now;
    float ax, ay, az;
    if (!imu.getAccelerometer(ax, ay, az)) return;
    int o = orientation_from(ax, ay);
    if (o < 0 || o == orient) { orient_candidate = orient; return; }
    if (o != orient_candidate) { orient_candidate = (uint8_t)o; orient_since = now; return; }
    if (now - orient_since >= 400) {
        orient = (uint8_t)o;
        panel_cmd(0x36, MADCTL_FOR[orient]);
        orient_changed = true;
    }
}

void poll_pmu(uint32_t now) {
    if (!pmu_ok) return;
    if (now - pkey_last_ms >= 40) {
        pkey_last_ms = now;
        pmu.getIrqStatus();
        if (pmu.isPekeyShortPressIrq()) push_pkey(PKEY_SHORT);
        if (pmu.isPekeyLongPressIrq())  push_pkey(PKEY_HOLD);
        if (pmu.isPekeyPositiveIrq())   push_pkey(PKEY_RELEASE);
        pmu.clearIrqStatus();
    }
    if (now - batt_last_ms >= 1500 || batt_last_ms == 0) {
        batt_last_ms = now ? now : 1;
        bool present = pmu.isBatteryConnect();
        batt.percent   = present ? pmu.getBatteryPercent() : -1;
        batt.charging  = present && pmu.isCharging();
        batt.usb_power = pmu.isVbusIn();
    }
}

}  // namespace

void board_begin(void) {
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);
    power_rails_up();
    panel_up();
    touch_up();
    imu_up();
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
    pinMode(PIN_BTN_KEY, INPUT_PULLUP);
}

void board_tick(void) {
    uint32_t now = millis();
    poll_pmu(now);
    poll_imu(now);
}

void display_flush(int x, int y, int w, int h, const uint16_t* px) {
    panel->draw16bitRGBBitmap(x, y, (uint16_t*)px, w, h);
}

// The CO5300 needs flush windows that start on even and end on odd pixels.
void display_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    *x1 &= ~1; *y1 &= ~1;
    *x2 |= 1;  *y2 |= 1;
}

void display_set_brightness(uint8_t level) {
    if (panel) panel->setBrightness(level);
}

bool display_orientation_changed(void) {
    bool c = orient_changed;
    orient_changed = false;
    return c;
}

bool touch_read(uint16_t* x, uint16_t* y) {
    static uint32_t last = 0;
    static bool pressed = false;
    static uint16_t lx = 0, ly = 0;
    if (!touch_ok) return false;
    uint32_t now = millis();
    if (now - last >= 20) {                       // ~50 Hz is plenty for taps
        last = now;
        int16_t tx[2], ty[2];
        uint8_t n = touch.getPoint(tx, ty, 1);
        pressed = n > 0;
        if (pressed) { lx = (uint16_t)tx[0]; ly = (uint16_t)ty[0]; }
    }
    *x = lx; *y = ly;
    return pressed;
}

Button button_pressed(void) {
    const int pins[2] = { PIN_BTN_BOOT, PIN_BTN_KEY };
    uint32_t now = millis();
    for (int i = 0; i < 2; i++) {
        bool down = digitalRead(pins[i]) == LOW;
        if (down != btn_down[i] && now - btn_change_ms[i] > 30) {   // debounce
            btn_down[i] = down;
            btn_change_ms[i] = now;
            if (down) return i == 0 ? BTN_BOOT : BTN_KEY;
        }
    }
    return BTN_NONE;
}

PowerKeyEvent power_key_event(void) {
    if (!pkey_n) return PKEY_NONE;
    PowerKeyEvent e = pkey_queue[0];
    for (int i = 1; i < pkey_n; i++) pkey_queue[i - 1] = pkey_queue[i];
    pkey_n--;
    return e;
}

BatteryInfo battery_info(void) { return batt; }

uint32_t board_free_heap(void) { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }

#endif  // !SIMULATOR
