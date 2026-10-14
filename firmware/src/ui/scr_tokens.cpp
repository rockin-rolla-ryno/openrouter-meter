// TOKENS — 30-day token volume and mix (cached input / fresh input /
// output / reasoning), cache hit rate, effective $ per 1M tokens and free
// requests. Cached ⊂ prompt and reasoning ⊂ completion, so the total is
// prompt + completion.
#include "screens.h"
#include <stdio.h>

#define BAR_X 24
#define BAR_Y 150
#define BAR_W 432
#define BAR_H 18
#define SEG_N 4
#define GAP 3

static lv_obj_t *lbl_total, *lbl_sub1, *lbl_sub2;
static lv_obj_t *seg[SEG_N], *scan, *lbl_val[SEG_N], *lbl_pct[SEG_N];
static lv_obj_t *ring, *lbl_hit, *lbl_cost, *lbl_free, *lbl_free_sub;
static int16_t seg_x[SEG_N], seg_w[SEG_N];
static int32_t hit_permille = 0;
static uint32_t enter_ms = 0;

static const lv_color_t SEG_COL[SEG_N] = {
    lv_color_hex(0x3EE6A0), lv_color_hex(0x7C83FF), lv_color_hex(0x38E1FF), lv_color_hex(0xFFB547),
};
static const char* const SEG_NAME[SEG_N] = { "CACHED INPUT", "FRESH INPUT", "OUTPUT", "REASONING" };

static void build(lv_obj_t* g) {
    lbl_total = ui_label(g, "--", &font_or_hero, C_TEXT);
    lv_obj_set_pos(lbl_total, MARGIN, 52);
    lbl_sub1 = ui_label(g, "tokens in 30 days", &font_or_ui16, C_DIM);
    lbl_sub2 = ui_label(g, "", &font_or_mono, C_DIM);

    lv_obj_set_pos(ui_box(g, BAR_W, BAR_H, C_HAIRLINE, 4), BAR_X, BAR_Y);
    for (int i = 0; i < SEG_N; i++) {
        seg[i] = ui_box(g, 0, BAR_H, SEG_COL[i], 4);
        lv_obj_set_pos(seg[i], BAR_X, BAR_Y);
    }
    scan = ui_box(g, 4, BAR_H, C_TEXT, 2);
    ui_opa(scan, 0);

    for (int i = 0; i < SEG_N; i++) {
        int x = (i % 2) ? 264 : 44, y = 192 + (i / 2) * 54;
        lv_obj_t* d = ui_dot(g, 10, SEG_COL[i]);
        lv_obj_set_pos(d, x - 18, y + 3);
        lv_obj_set_pos(ui_caps(g, SEG_NAME[i], C_DIM), x, y);
        lbl_val[i] = ui_label(g, "--", &font_or_ui20, C_TEXT);
        lv_obj_set_pos(lbl_val[i], x, y + 20);
        lbl_pct[i] = ui_label(g, "", &font_or_mono, C_DIM);
    }

    lv_obj_set_pos(ui_box(g, BAR_W, 1, C_HAIRLINE, 0), BAR_X, 308);

    ring = ui_arc(g, 96, 9, C_HAIRLINE, C_MINT);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_rotation(ring, 270);
    ui_center(ring, 82, 380);
    lbl_hit = ui_label(g, "--", &font_or_ui20, C_TEXT);
    lv_obj_align(lbl_hit, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* hc = ui_caps(g, "CACHE HIT", C_DIM);
    lv_obj_set_pos(hc, 34, 436);

    lv_obj_set_pos(ui_caps(g, "COST / 1M TOKENS", C_DIM), 160, 334);
    lbl_cost = ui_label(g, "--", &font_or_num, C_TEXT);
    lv_obj_set_pos(lbl_cost, 160, 354);

    lv_obj_set_pos(ui_caps(g, "FREE REQUESTS", C_DIM), 160, 398);
    lbl_free = ui_label(g, "--", &font_or_ui20, C_TEXT);
    lv_obj_set_pos(lbl_free, 160, 418);
    lbl_free_sub = ui_label(g, "", &font_or_mono, C_DIM);
}

static void apply(const Model& m, uint16_t parts, bool visible) {
    if (!(parts & M_TOK)) return;
    char b[48], c[24];
    int64_t cached = m.tok_cached, fresh = m.tok_prompt - m.tok_cached;
    int64_t reasoning = m.tok_reasoning, output = m.tok_completion - m.tok_reasoning;
    if (fresh < 0) { fresh = 0; }
    if (output < 0) { output = 0; }
    int64_t total = cached + fresh + output + reasoning;
    int64_t part[SEG_N] = { cached, fresh, output, reasoning };

    fmt_count(total, b, sizeof(b));
    ui_text(lbl_total, b);
    lv_obj_update_layout(lbl_total);
    lv_obj_align_to(lbl_sub1, lbl_total, LV_ALIGN_OUT_RIGHT_TOP, 14, 14);
    fmt_int(m.tok_requests, c, sizeof(c));
    snprintf(b, sizeof(b), "%s requests", c);
    ui_text(lbl_sub2, b);
    lv_obj_align_to(lbl_sub2, lbl_sub1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    // Segment widths: proportional, but any non-zero part gets >= 8 px.
    int avail = BAR_W - GAP * (SEG_N - 1), x = BAR_X, used = 0, nz = 0;
    int w[SEG_N];
    for (int i = 0; i < SEG_N; i++) {
        w[i] = total > 0 ? (int)(part[i] * avail / total) : 0;
        if (part[i] > 0 && w[i] < 8) w[i] = 8;
        if (part[i] > 0) nz = i;
        used += w[i];
    }
    w[nz] += avail - used;                      // absorb rounding in the last non-zero part
    if (w[nz] < 8 && part[nz] > 0) w[nz] = 8;
    for (int i = 0; i < SEG_N; i++) {
        seg_x[i] = (int16_t)x;
        seg_w[i] = (int16_t)(w[i] < 0 ? 0 : w[i]);
        x += w[i] + (w[i] > 0 ? GAP : 0);
        fmt_count(part[i], b, sizeof(b));
        ui_text(lbl_val[i], b);
        int pct = total > 0 ? (int)lround(part[i] * 100.0 / total) : 0;
        snprintf(b, sizeof(b), pct == 0 && part[i] > 0 ? "<1%%" : "%d%%", pct);
        ui_text(lbl_pct[i], b);
        lv_obj_update_layout(lbl_val[i]);
        lv_obj_align_to(lbl_pct[i], lbl_val[i], LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -1);
    }

    hit_permille = m.tok_prompt > 0 ? (int32_t)(m.tok_cached * 1000 / m.tok_prompt) : 0;
    snprintf(b, sizeof(b), "%d%%", (int)((hit_permille + 5) / 10));
    ui_text(lbl_hit, b);
    lv_obj_update_layout(lbl_hit);
    ui_center(lbl_hit, 82, 380);

    if (total > 0) {
        double per_m = m.tok_cents / 100.0 * 1e6 / (double)total;
        if (per_m < 10) snprintf(b, sizeof(b), "$%.2f", per_m);
        else snprintf(b, sizeof(b), "$%.1f", per_m);
    } else snprintf(b, sizeof(b), "--");
    ui_text(lbl_cost, b);

    fmt_int(m.tok_free_req, b, sizeof(b));
    ui_text(lbl_free, b);
    int fp = m.tok_requests > 0 ? (int)lround(m.tok_free_req * 100.0 / m.tok_requests) : 0;
    snprintf(b, sizeof(b), "%d%% of all", fp);
    ui_text(lbl_free_sub, b);
    lv_obj_update_layout(lbl_free);
    lv_obj_align_to(lbl_free_sub, lbl_free, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -1);

    if (visible) enter_ms = lv_tick_get();
}

static void enter(void) {
    enter_ms = lv_tick_get();
    for (int i = 0; i < SEG_N; i++) lv_obj_set_width(seg[i], 0);
    lv_arc_set_value(ring, 0);
}

static void frame(uint32_t now) {
    uint32_t since = now - enter_ms;
    // Segments fill left to right, one after another.
    for (int i = 0; i < SEG_N; i++) {
        float t = ease_out(((int32_t)since - i * 160) / 380.0f);
        int w = (int)(seg_w[i] * t);
        if (lv_obj_get_style_width(seg[i], LV_PART_MAIN) != w) {
            lv_obj_set_width(seg[i], w);
            lv_obj_set_x(seg[i], seg_x[i]);
        }
    }
    int v = (int)(hit_permille * ease_out((int32_t)(since - 200) / 900.0f));
    if (lv_arc_get_value(ring) != v) lv_arc_set_value(ring, (int16_t)v);

    // A scan packet glides across the bar every 3.2 s once it has filled.
    if (since > 1100) {
        uint32_t t = (since - 1100) % 3200;
        if (t < 1200) {
            float p = ease_smooth(t / 1200.0f);
            lv_obj_set_pos(scan, BAR_X + (int)(p * (BAR_W - 4)), BAR_Y);
            ui_opa(scan, (int)(170 * sinf(p * 3.14159f)));
        } else ui_opa(scan, 0);
    }
}

static bool has_data(const Model& m) { return m.have_tok; }

const Screen SCREEN_TOKENS = { "TOKENS \xC2\xB7 30D", build, apply, enter, frame, has_data };
