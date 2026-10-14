// OpenRouter Meter firmware — entry point.
//
//   Tap screen / BOOT button   next screen        KEY button   previous screen
//   PWR short press            cycle brightness
//   PWR hold 3 s, release      forget the paired PC and pair a new one
//   PWR hold 8 s               power off (handled by the PMU)
#include <Arduino.h>
#include <lvgl.h>
#include "board/board.h"
#include "link/link.h"
#include "model.h"
#include "settings.h"
#include "ui/ui.h"

#ifndef SIMULATOR
#include <esp_heap_caps.h>
#define BUF_LINES 24
#define ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#else
#include <stdlib.h>
#define BUF_LINES 48
#define ALLOC(n) malloc(n)
#endif

static Model model = {};

// ---- Brightness & dimming -------------------------------------------------
static const uint8_t LEVELS[] = { 255, 170, 100, 45 };
#define N_LEVELS (sizeof(LEVELS) / sizeof(LEVELS[0]))
static uint8_t  level_idx = 0;
static bool     dimmed = false;
static uint32_t last_activity = 0;
static uint8_t  shown_brightness = 0;
static uint32_t fade_start = 0;
static uint8_t  fade_from = 0, fade_to = 0;

static void fade_to_level(uint8_t target) {
    fade_from = shown_brightness;
    fade_to = target;
    fade_start = millis();
}

static void brightness_tick(void) {
    if (fade_start == 0) return;
    uint32_t t = millis() - fade_start;
    const uint32_t dur = 350;
    uint8_t v = t >= dur ? fade_to : (uint8_t)(fade_from + (fade_to - fade_from) * (int)t / (int)dur);
    if (v != shown_brightness) { shown_brightness = v; display_set_brightness(v); }
    if (t >= dur) fade_start = 0;
}

static void activity(void) {
    last_activity = millis();
    if (dimmed) { dimmed = false; fade_to_level(LEVELS[level_idx]); }
}

static void dim_tick(void) {
    if (dimmed || model.dim_after_min == 0) return;
    if (millis() - last_activity > model.dim_after_min * 60000UL) {
        dimmed = true;
        fade_to_level(LEVELS[level_idx] / 6 + 4);
    }
}

// ---- LVGL glue -------------------------------------------------------------
static uint32_t tick_cb(void) { return millis(); }

static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    display_flush(a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1, (const uint16_t*)px);
    lv_display_flush_ready(d);
}

static void round_cb(lv_event_t* e) {
    lv_area_t* a = (lv_area_t*)lv_event_get_param(e);
    display_round_area(&a->x1, &a->y1, &a->x2, &a->y2);
}

// While dimmed, the first touch only wakes the panel (it isn't a tap).
static void touch_cb(lv_indev_t*, lv_indev_data_t* data) {
    static bool swallowing = false;
    uint16_t x, y;
    bool down = touch_read(&x, &y);
    if (down && dimmed) swallowing = true;
    if (down) activity();
    if (!down) swallowing = false;
    data->point.x = x;
    data->point.y = y;
    data->state = (down && !swallowing) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void lvgl_begin(void) {
    lv_init();
    lv_tick_set_cb(tick_cb);
    const size_t bytes = SCREEN_W * BUF_LINES * 2;
    void* b1 = ALLOC(bytes);
    void* b2 = ALLOC(bytes);
    lv_display_t* d = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(d, flush_cb);
    lv_display_set_buffers(d, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(d, round_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, touch_cb);
}

// ---- Power key: brightness + pairing gesture -------------------------------
enum PairState { PAIR_IDLE, PAIR_HELD, PAIR_ARMED };
static PairState pair = PAIR_IDLE;
static uint32_t  pair_held_ms = 0;

static void power_key_tick(void) {
    PowerKeyEvent e;
    while ((e = power_key_event()) != PKEY_NONE) {
        if (e == PKEY_SHORT) {
            if (dimmed) activity();
            else {
                level_idx = (level_idx + 1) % N_LEVELS;
                settings_save_brightness(level_idx);
                fade_to_level(LEVELS[level_idx]);
                activity();
            }
        } else if (e == PKEY_HOLD && pair == PAIR_IDLE) {
            pair = PAIR_HELD;                       // PMU reports ~1.5 s
            pair_held_ms = millis();
        } else if (e == PKEY_RELEASE) {
            if (pair == PAIR_ARMED) link_forget_pairings();
            pair = PAIR_IDLE;
            ui_set_pair_prompt(false);
        }
    }
    if (pair == PAIR_HELD && millis() - pair_held_ms >= 1500) {          // 3 s total
        pair = PAIR_ARMED;
        activity();
        ui_set_pair_prompt(true);
    } else if (pair == PAIR_ARMED && millis() - pair_held_ms >= 4500) {  // 6 s: heading for power-off
        pair = PAIR_IDLE;
        ui_set_pair_prompt(false);
    }
}

// ---- Arduino entry points -----------------------------------------------------
void setup() {
    Serial.begin(115200);
    board_begin();
    settings_begin();
    level_idx = settings_load_brightness() % N_LEVELS;
    lvgl_begin();
    ui_begin();
    link_begin();
    ui_set_link(link_state(), link_has_bond());
    ui_set_battery(battery_info());
    lv_timer_handler();                     // draw the first frame before lighting up
    fade_to_level(LEVELS[level_idx]);
    last_activity = millis();
    Serial.printf("[meter] ready, free heap %lu\n", (unsigned long)board_free_heap());
}

void loop() {
    board_tick();
    link_tick();

    static LinkState last_link = LINK_ADVERTISING;
    static bool last_bond = false;
    LinkState ls = link_state();
    bool bond = link_has_bond();
    if (ls != last_link || bond != last_bond) {
        last_link = ls;
        last_bond = bond;
        ui_set_link(ls, bond);
        if (ls == LINK_CONNECTED) activity();
    }

    static char msg[512];
    int handled = 0;
    while (handled < 4 && link_next_message(msg, sizeof(msg))) {
        uint16_t parts = protocol_apply(msg, &model);
        if (parts) ui_apply(model, parts);
        handled++;
    }

    switch (button_pressed()) {
        case BTN_BOOT: if (dimmed) activity(); else { activity(); ui_next(); } break;
        case BTN_KEY:  if (dimmed) activity(); else { activity(); ui_prev(); } break;
        default: break;
    }
    power_key_tick();

    static uint32_t batt_ms = 0;
    if (millis() - batt_ms > 3000) {
        batt_ms = millis();
        BatteryInfo b = battery_info();
        ui_set_battery(b);
        link_set_battery(b.percent);
    }

    if (display_orientation_changed()) lv_obj_invalidate(lv_screen_active());
    if (ui_take_wake_request()) activity();
    ui_take_tap();
    dim_tick();
    brightness_tick();

    ui_frame();
    lv_timer_handler();
    delay(4);
}
