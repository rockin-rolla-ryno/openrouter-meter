// CREDITS — balance gauge with a packet comet sweeping the track.
// A spend event counts the balance down, floats the delta and bursts the comet.
#include "screens.h"
#include <stdio.h>

#define RCX 240
#define RCY 206
#define RD  280
#define RW  12
#define RR  (RD / 2 - RW / 2)     // radius of the track centreline
#define COMET_N 7

static lv_obj_t *ring, *lbl_bal, *lbl_of, *lbl_pct, *lbl_delta, *lbl_today, *lbl_month;
static lv_obj_t* comet[COMET_N];
static const uint8_t COMET_D[COMET_N]   = { 14, 11, 9, 8, 7, 6, 5 };
static const uint8_t COMET_OPA[COMET_N] = { 255, 190, 140, 100, 70, 45, 25 };

static bool     seeded = false;
static int32_t  prev_bal = 0, prev_day = 0, bal_shown = 0, ring_target = 0;
static uint32_t lap_start = 0, burst_until = 0, delta_start = 0;

static lv_color_t level_color(int permille) {
    if (permille < 100) return C_CORAL;
    if (permille < 250) return C_AMBER;
    return C_ACCENT;
}

static void bal_cb(void*, int32_t v) {
    bal_shown = v;
    char b[24];
    fmt_money(v, b, sizeof(b));
    lv_label_set_text(lbl_bal, b);
}
static void ring_cb(void* o, int32_t v) { lv_arc_set_value((lv_obj_t*)o, (int16_t)v); }

static void anim(void* var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void credits_note_spend(int32_t cents) {
    char m[24], b[28];
    fmt_money(cents, m, sizeof(m));
    snprintf(b, sizeof(b), "-%s", m);
    lv_label_set_text(lbl_delta, b);
    uint32_t now = lv_tick_get();
    delta_start = now;
    burst_until = now + 5000;
    lap_start = now;
}

static void build(lv_obj_t* g) {
    lv_obj_t* bezel = ui_arc(g, RD + 22, 1, C_HAIRLINE, C_HAIRLINE);
    lv_arc_set_bg_angles(bezel, 135, 45);
    lv_obj_remove_style(bezel, NULL, LV_PART_INDICATOR);
    ui_center(bezel, RCX, RCY);

    ring = ui_arc(g, RD, RW, C_HAIRLINE, C_ACCENT);
    lv_arc_set_bg_angles(ring, 135, 45);
    ui_center(ring, RCX, RCY);

    for (int i = COMET_N - 1; i >= 0; i--) {      // tail first: head on top
        comet[i] = ui_dot(g, COMET_D[i], i == 0 ? C_TEXT : C_PACKET);
        ui_opa(comet[i], 0);
    }

    lv_obj_align(ui_caps(g, "BALANCE", C_DIM), LV_ALIGN_TOP_MID, 0, RCY - 74);
    lbl_bal = ui_label(g, "$-.--", &font_or_hero, C_TEXT);
    lv_obj_align(lbl_bal, LV_ALIGN_TOP_MID, 0, RCY - 46);
    lbl_of = ui_label(g, "", &font_or_ui16, C_DIM);
    lv_obj_align(lbl_of, LV_ALIGN_TOP_MID, 0, RCY + 30);
    lbl_delta = ui_label(g, "", &font_or_ui20, C_CORAL);
    lv_obj_align(lbl_delta, LV_ALIGN_TOP_MID, 0, RCY + 52);
    ui_opa(lbl_delta, 0);
    lbl_pct = ui_caps(g, "", C_ACCENT);
    lv_obj_align(lbl_pct, LV_ALIGN_TOP_MID, 0, RCY + 108);

    const int ly = 372, vy = 391;
    lv_obj_set_pos(ui_caps(g, "TODAY", C_DIM), 44, ly);
    lbl_today = ui_label(g, "$-.--", &font_or_num, C_TEXT);
    lv_obj_set_pos(lbl_today, 44, vy);
    lv_obj_set_pos(ui_caps(g, "THIS MONTH", C_DIM), 264, ly);
    lbl_month = ui_label(g, "$-.--", &font_or_num, C_TEXT);
    lv_obj_set_pos(lbl_month, 264, vy);
    lv_obj_set_pos(ui_box(g, 1, 54, C_HAIRLINE, 0), 240, ly + 4);
}

static void apply(const Model& m, uint16_t parts, bool visible) {
    if (!(parts & M_CORE)) return;
    char b[48], c[24];

    int32_t pm = m.credits > 0 ? (int32_t)((int64_t)m.bal * 1000 / m.credits) : 0;
    pm = pm < 0 ? 0 : (pm > 1000 ? 1000 : pm);
    lv_color_t lc = level_color(pm);
    lv_obj_set_style_arc_color(ring, lc, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(lbl_pct, lc, 0);
    if (m.credits > 0) {
        snprintf(b, sizeof(b), "%d%% LEFT", (int)((pm + 5) / 10));
        ui_text(lbl_pct, b);
        fmt_money(m.credits, c, sizeof(c));
        snprintf(b, sizeof(b), "of %s credits", c);
        ui_text(lbl_of, b);
    } else {
        ui_text(lbl_pct, "");
        ui_text(lbl_of, "credits");
    }

    // Spend event: the balance dropped (covers every key and the chatroom) or
    // the live daily total rose. Not on the first message after boot.
    if (seeded) {
        int32_t spent = prev_bal > m.bal ? prev_bal - m.bal : 0;
        if (m.day > prev_day && m.day - prev_day > spent) spent = m.day - prev_day;
        if (spent > 0) credits_note_spend(spent);
    }
    if (!seeded || m.bal != bal_shown) anim(lbl_bal, bal_cb, seeded ? bal_shown : 0, m.bal, seeded ? 900 : 1100);
    if (pm != ring_target) {
        int32_t from = seeded ? lv_arc_get_value(ring) : 0;
        ring_target = pm;
        if (visible) anim(ring, ring_cb, from, pm, seeded ? 700 : 900);
        else lv_arc_set_value(ring, (int16_t)pm);
    }
    prev_bal = m.bal;
    prev_day = m.day;
    seeded = true;

    fmt_money(m.day, b, sizeof(b));
    ui_text(lbl_today, b);
    fmt_money(m.month, b, sizeof(b));
    ui_text(lbl_month, b);
}

static void enter(void) {
    anim(ring, ring_cb, 0, ring_target, 900);
    lap_start = lv_tick_get() + 350;
    for (int i = 0; i < COMET_N; i++) ui_opa(comet[i], 0);
}

static void frame(uint32_t now) {
    bool burst = (int32_t)(burst_until - now) > 0;
    uint32_t sweep = burst ? 950 : 2600, rest = burst ? 120 : 1400;
    if ((int32_t)(now - lap_start) < 0) {
        for (int i = 0; i < COMET_N; i++) ui_opa(comet[i], 0);
    } else {
        uint32_t t = (now - lap_start) % (sweep + rest);
        float p = ease_smooth((float)t / sweep);
        float fade = t > sweep ? 0.0f : (p > 0.9f ? (1.0f - p) / 0.1f : 1.0f);
        float spacing = burst ? 0.028f : 0.020f;
        for (int i = 0; i < COMET_N; i++) {
            float pi = p - i * spacing;
            if (pi < 0 || fade <= 0) { ui_opa(comet[i], 0); continue; }
            float a = (135.0f + 270.0f * pi) * 0.01745329f;
            ui_center(comet[i], RCX + RR * cosf(a), RCY + RR * sinf(a));
            ui_opa(comet[i], (int)(COMET_OPA[i] * fade));
        }
    }
    if (delta_start) {
        uint32_t dt = now - delta_start;
        if (dt > 1800) {
            ui_opa(lbl_delta, 0);
            ui_opa(lbl_of, 255);
            delta_start = 0;
        } else {
            float t = dt / 1800.0f;
            lv_obj_align(lbl_delta, LV_ALIGN_TOP_MID, 0, RCY + 52 - (int)(24 * ease_out(t)));
            int o = (int)(255 * (t < 0.15f ? t / 0.15f : 1.0f - ease_smooth((t - 0.6f) / 0.4f)));
            ui_opa(lbl_delta, o);
            ui_opa(lbl_of, 255 - o);
        }
    }
}

static bool has_data(const Model& m) { return m.have_core; }

const Screen SCREEN_CREDITS = { "CREDITS", build, apply, enter, frame, has_data };
