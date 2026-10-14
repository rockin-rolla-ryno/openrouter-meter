// Hardware layer for the Waveshare ESP32-C6-Touch-AMOLED-2.16 (and the
// desktop simulator, which implements the same functions with SDL2).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SCREEN_W 480
#define SCREEN_H 480

enum Button : uint8_t { BTN_NONE = 0, BTN_BOOT, BTN_KEY };

enum PowerKeyEvent : uint8_t {
    PKEY_NONE = 0,
    PKEY_SHORT,        // short press
    PKEY_HOLD,         // held ~1.5 s (still down)
    PKEY_RELEASE,      // released (after any press)
};

struct BatteryInfo {
    int  percent;      // 0..100, -1 = no battery fitted
    bool charging;
    bool usb_power;
};

void     board_begin(void);                       // power rails, I2C, panel, touch, IMU
void     board_tick(void);                        // poll PMU / IMU (call every loop)

// Display
void     display_flush(int x, int y, int w, int h, const uint16_t* rgb565);
void     display_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2);
void     display_set_brightness(uint8_t level);   // 0..255
bool     display_orientation_changed(void);       // true once after the IMU flips it

// Input
bool     touch_read(uint16_t* x, uint16_t* y);    // true while pressed
Button   button_pressed(void);                    // edge: returns each press once
PowerKeyEvent power_key_event(void);              // edge events from the PMU

BatteryInfo battery_info(void);
uint32_t board_free_heap(void);
