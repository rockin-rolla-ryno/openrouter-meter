// SPEND — 30 daily bars. A lone outlier day (> 3x the runner-up) is clipped
// with an axis break so ordinary days stay readable; it keeps its true value
// as a label. Today's bar follows the live spend-today figure and breathes.
#include "screens.h"
#include <stdio.h>
#include <string.h>

#define CH_X 24
#define CH_W 432
#define CH_TOP 140
#define CH_BASE 274
#define BAR_W 9

static lv_obj_t *lbl_s30, *lbl_s30_sub, *bars[HIST_DAYS], *brk[4], *lbl_peak, *avg_line, *lbl_avg_tag;
static lv_obj_t *lbl_avg, *lbl_runway, *lbl_yday, *lbl_yreq, *lbl_ytok;
static int16_t bar_h[HIST_DAYS], avg_y = -1;
static int8_t  brk_bar[2] = { -1, -1 }, peak_bar = -1;
static int32_t hist[HIST_DAYS];
static uint32_t enter_ms = 0;

static int bar_x(int i) {
    const float slot = CH_W / (float)HIST_DAYS;
    return CH_X + (int)lroundf(i * slot + (slot - BAR_W) / 2);
}

static void build(lv_obj_t* g) {
    lbl_s30 = ui_label(g, "$-.--", &font_or_num, C_TEXT);
    lv_obj_set_pos(lbl_s30, MARGIN, 58);
    lbl_s30_sub = ui_label(g, "spent in the last 30 days", &font_or_ui16, C_DIM);
    lv_obj_align_to(lbl_s30_sub, lbl_s30, LV_ALIGN_OUT_RIGHT_BOTTOM, 12, -7);

    lv_obj_set_pos(ui_box(g, CH_W, 1, C_HAIRLINE, 0), CH_X, CH_BASE + 1);
    for (int i = 0; i < HIST_DAYS; i++) {
        bars[i] = ui_box(g, BAR_W, 3, C_FAINT, 3);
        lv_obj_set_pos(bars[i], bar_x(i), CH_BASE - 3);
        bar_h[i] = 3;
    }
    for (int k = 0; k < 4; k++) { brk[k] = ui_box(g, BAR_W + 8, 3, C_BG, 0); ui_show(brk[k], false); }
    avg_line = ui_box(g, CH_W, 1, C_DIM, 0);
    ui_opa(avg_line, 110);
    lbl_avg_tag = ui_caps(g, "AVG", C_DIM);
    lbl_peak = ui_caps(g, "", C_TEXT);
    lv_obj_set_style_text_letter_space(lbl_peak, 0, 0);
    ui_show(avg_line, false); ui_show(lbl_avg_tag, false); ui_show(lbl_peak, false);

    lv_obj_set_pos(ui_caps(g, "30D AGO", C_FAINT), CH_X, CH_BASE + 8);
    lv_obj_align(ui_caps(g, "TODAY", C_FAINT), LV_ALIGN_TOP_RIGHT, -(W - CH_X - CH_W), CH_BASE + 8);

    const int c0 = 44, c1 = 264, r0 = 312, r1 = 384, dv = 18;
    lv_obj_set_pos(ui_caps(g, "AVG / DAY", C_DIM), c0, r0);
    lbl_avg = ui_label(g, "--", &font_or_num, C_TEXT);      lv_obj_set_pos(lbl_avg, c0, r0 + dv);
    lv_obj_set_pos(ui_caps(g, "RUNWAY", C_DIM), c1, r0);
    lbl_runway = ui_label(g, "--", &font_or_num, C_TEXT);   lv_obj_set_pos(lbl_runway, c1, r0 + dv);
    lv_obj_set_pos(ui_caps(g, "YESTERDAY", C_DIM), c0, r1);
    lbl_yday = ui_label(g, "--", &font_or_num, C_TEXT);     lv_obj_set_pos(lbl_yday, c0, r1 + dv);
    lbl_yreq = ui_label(g, "", &font_or_mono, C_DIM);
    lv_obj_set_pos(ui_caps(g, "TOKENS YDAY", C_DIM), c1, r1);
    lbl_ytok = ui_label(g, "--", &font_or_num, C_TEXT);     lv_obj_set_pos(lbl_ytok, c1, r1 + dv);
    lv_obj_set_pos(ui_box(g, 1, 128, C_HAIRLINE, 0), 240, r0 + 4);
}

static void compute_bars(void) {
    const int maxh = CH_BASE - CH_TOP;
    int32_t m1 = 0, m2 = 0; int i1 = -1;
    for (int i = 0; i < HIST_DAYS; i++) {
        if (hist[i] > m1) { m2 = m1; m1 = hist[i]; i1 = i; } else if (hist[i] > m2) m2 = hist[i];
    }
    int32_t cap = (m2 > 0 && m1 > 3 * m2) ? m2 + m2 / 2 : m1;
    brk_bar[0] = brk_bar[1] = -1;
    peak_bar = m1 > 0 ? (int8_t)i1 : -1;
    int nb = 0;
    for (int i = 0; i < HIST_DAYS; i++) {
        int32_t v = hist[i];
        int h;
        if (v <= 0 || cap <= 0) h = 3;
        else if (v > cap) { h = maxh; if (nb < 2) brk_bar[nb++] = (int8_t)i; }
        else h = 4 + (int)((int64_t)v * (maxh - 4) / cap);
        bar_h[i] = (int16_t)h;
        lv_obj_set_style_bg_color(bars[i], v <= 0 ? C_FAINT : (i == HIST_DAYS - 1 ? C_PACKET : C_ACCENT), 0);
    }
    int64_t sum = 0;
    for (int i = 0; i < HIST_DAYS; i++) sum += hist[i];
    int32_t avg = (int32_t)(sum / HIST_DAYS);
    avg_y = -1;
    if (avg > 0 && cap > 0 && avg <= cap) {
        avg_y = (int16_t)(CH_BASE - (4 + (int)((int64_t)avg * (maxh - 4) / cap)));
        lv_obj_set_pos(avg_line, CH_X, avg_y);
        lv_obj_update_layout(lbl_avg_tag);
        lv_obj_set_pos(lbl_avg_tag, CH_X + CH_W - lv_obj_get_width(lbl_avg_tag), avg_y - 20);
    }
    if (peak_bar >= 0) {
        char b[24];
        fmt_money(m1, b, sizeof(b));
        lv_label_set_text(lbl_peak, b);
        lv_obj_update_layout(lbl_peak);
        int lw = lv_obj_get_width(lbl_peak);
        int x = bar_x(peak_bar) + BAR_W / 2 - lw / 2;
        if (x < CH_X) x = CH_X;
        if (x + lw > CH_X + CH_W) x = CH_X + CH_W - lw;
        lv_obj_set_pos(lbl_peak, x, CH_BASE - bar_h[peak_bar] - 22);
    }
}

static void stats(const Model& m) {
    char b[32], r[16];
    int64_t s30 = 0;
    for (int i = 0; i < HIST_DAYS; i++) s30 += hist[i];
    if (!m.have_hist) s30 = m.s30;
    fmt_money((int32_t)s30, b, sizeof(b));
    ui_text(lbl_s30, b);
    lv_obj_align_to(lbl_s30_sub, lbl_s30, LV_ALIGN_OUT_RIGHT_BOTTOM, 12, -7);

    int32_t avg = (int32_t)(s30 / HIST_DAYS);
    fmt_money(avg, b, sizeof(b));
    ui_text(lbl_avg, b);
    if (avg > 0) {
        long days = m.bal / avg;
        if (days > 999) snprintf(b, sizeof(b), "999+ d");
        else snprintf(b, sizeof(b), "%ld %s", days, days == 1 ? "day" : "days");
        lv_obj_set_style_text_color(lbl_runway, days < 7 ? C_CORAL : (days < 21 ? C_AMBER : C_TEXT), 0);
    } else {
        snprintf(b, sizeof(b), "--");
        lv_obj_set_style_text_color(lbl_runway, C_TEXT, 0);
    }
    ui_text(lbl_runway, b);

    fmt_money(m.yday, b, sizeof(b));
    ui_text(lbl_yday, b);
    fmt_count(m.yday_req, r, sizeof(r));
    snprintf(b, sizeof(b), "%s req", r);
    ui_text(lbl_yreq, b);
    lv_obj_align_to(lbl_yreq, lbl_yday, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -7);
    fmt_count(m.yday_tok, b, sizeof(b));
    ui_text(lbl_ytok, b);
}

static void reset_overlays(void) {
    for (int k = 0; k < 4; k++) ui_show(brk[k], false);
    ui_show(lbl_peak, false); ui_show(avg_line, false); ui_show(lbl_avg_tag, false);
}

static void enter(void) {
    enter_ms = lv_tick_get();
    reset_overlays();
}

static void apply(const Model& m, uint16_t parts, bool visible) {
    if (!(parts & (M_HIST | M_CORE))) return;
    int32_t next[HIST_DAYS];
    memcpy(next, m.hist, sizeof(next));
    // Today's bar tracks the live spend-today figure between history pushes.
    if (m.have_core && m.day > next[HIST_DAYS - 1]) next[HIST_DAYS - 1] = m.day;
    bool changed = memcmp(next, hist, sizeof(hist)) != 0;
    bool regrow = (parts & M_HIST) && changed;
    memcpy(hist, next, sizeof(hist));
    if (changed) compute_bars();
    if (regrow && visible) enter();
    stats(m);
}

static void frame(uint32_t now) {
    uint32_t since = now - enter_ms;
    bool growing = false;
    for (int i = 0; i < HIST_DAYS; i++) {
        float t = ease_out(((int32_t)since - i * 16) / 420.0f);
        if (t < 1.0f) growing = true;
        int h = bar_h[i] <= 3 ? 3 : 3 + (int)((bar_h[i] - 3) * t);
        if (lv_obj_get_style_height(bars[i], LV_PART_MAIN) != h) {
            lv_obj_set_height(bars[i], h);
            lv_obj_set_y(bars[i], CH_BASE - h);
        }
    }
    if (!growing) {
        for (int k = 0; k < 4; k++) {
            int b = brk_bar[k / 2];
            ui_show(brk[k], b >= 0);
            if (b >= 0) lv_obj_set_pos(brk[k], bar_x(b) - 4, CH_BASE - bar_h[b] + 18 + (k & 1) * 7);
        }
        ui_show(avg_line, avg_y >= 0);
        ui_show(lbl_avg_tag, avg_y >= 0);
        ui_show(lbl_peak, peak_bar >= 0);
    }
    ui_opa(bars[HIST_DAYS - 1], 140 + (int)(115 * breathe(now, 2000)));
}

static bool has_data(const Model& m) { return m.have_hist || m.have_core; }

const Screen SCREEN_SPEND = { "SPEND \xC2\xB7 30D", build, apply, enter, frame, has_data };
