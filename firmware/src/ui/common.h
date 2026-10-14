// Shared UI helpers: palette, fonts, object factories, formatting, easing.
#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <math.h>
#include "../model.h"

// ---- Palette (true-black AMOLED base) ----
#define C_BG        lv_color_hex(0x000000)
#define C_SURFACE   lv_color_hex(0x0E0F15)
#define C_HAIRLINE  lv_color_hex(0x1B1D27)
#define C_TEXT      lv_color_hex(0xF4F5F8)
#define C_DIM       lv_color_hex(0x8A8FA3)
#define C_FAINT     lv_color_hex(0x464B5E)
#define C_ACCENT    lv_color_hex(0x7C83FF)   // route indigo
#define C_PACKET    lv_color_hex(0x38E1FF)   // packet cyan
#define C_MINT      lv_color_hex(0x3EE6A0)
#define C_AMBER     lv_color_hex(0xFFB547)
#define C_CORAL     lv_color_hex(0xFF5D73)
#define C_PINK      lv_color_hex(0xFF7AC6)
#define C_OTHERS    lv_color_hex(0x5A5F73)

extern const lv_color_t SERIES[MAX_ROWS];   // indigo, cyan, mint, amber, pink

// ---- Fonts ----
LV_FONT_DECLARE(font_or_hero);   // Inter Display SemiBold 64: " $%+,-./0-9BKMk"
LV_FONT_DECLARE(font_or_num);    // Inter Display SemiBold 34
LV_FONT_DECLARE(font_or_ui20);   // Inter Medium 20
LV_FONT_DECLARE(font_or_ui16);   // Inter Regular 16
LV_FONT_DECLARE(font_or_cap);    // JetBrains Mono Medium 15 (spaced caps)
LV_FONT_DECLARE(font_or_mono);   // JetBrains Mono Regular 18

// ---- Geometry ----
#define W 480
#define H 480
#define MARGIN 24
#define CONTENT_TOP 58      // below the header
#define CONTENT_BOTTOM 446  // above the page dots

// ---- Factories (all non-clickable: taps fall through to the screen) ----
lv_obj_t* ui_box(lv_obj_t* parent, int w, int h, lv_color_t c, int radius);
lv_obj_t* ui_dot(lv_obj_t* parent, int d, lv_color_t c);
lv_obj_t* ui_label(lv_obj_t* parent, const char* txt, const lv_font_t* f, lv_color_t c);
lv_obj_t* ui_caps(lv_obj_t* parent, const char* txt, lv_color_t c);
lv_obj_t* ui_group(lv_obj_t* parent);
lv_obj_t* ui_arc(lv_obj_t* parent, int d, int width, lv_color_t track, lv_color_t ind);
void      ui_center(lv_obj_t* o, float cx, float cy);
void      ui_show(lv_obj_t* o, bool on);
void      ui_opa(lv_obj_t* o, int opa);          // layer-free fade
void      ui_text(lv_obj_t* label, const char* s); // set only if changed

// ---- Formatting ----
void fmt_money(int32_t cents, char* buf, size_t n);         // $1,234.56
void fmt_money_short(int32_t cents, char* buf, size_t n);   // $1.2k / $12.34
void fmt_count(int64_t v, char* buf, size_t n);              // 1,964 / 27.3k / 1.2M / 3.4B
void fmt_int(int64_t v, char* buf, size_t n);                // 1,964

// ---- Easing ----
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float ease_smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static inline float ease_out(float t) { t = clampf(t, 0, 1); float u = 1 - t; return 1 - u * u * u; }
static inline float breathe(uint32_t now, uint32_t period) {
    return 0.5f + 0.5f * sinf((now % period) / (float)period * 6.2831853f);
}
