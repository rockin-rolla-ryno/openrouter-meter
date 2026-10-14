// KEYS — spend per API key (today / this week / this month) with a limit
// bar for keys that have a spending cap. Keys used today pulse.
#include "screens.h"
#include <stdio.h>

#define CARD_X 24
#define CARD_W 432
#define CARD_H 88
#define CARD_GAP 10
#define TRACK_W 220

struct Card {
    lv_obj_t *box, *name, *live, *today, *periods, *track, *fill, *limit, *tag;
};
static Card card[MAX_KEYS];
static int n_keys = 0;
static int16_t card_y[MAX_KEYS], fill_w[MAX_KEYS];
static bool used_today[MAX_KEYS];
static uint32_t enter_ms = 0;

static void build(lv_obj_t* g) {
    for (int i = 0; i < MAX_KEYS; i++) {
        Card& c = card[i];
        c.box = ui_box(g, CARD_W, CARD_H, C_SURFACE, 14);
        c.name = ui_label(c.box, "", &font_or_ui20, C_TEXT);
        lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(c.name, 250);
        lv_obj_set_pos(c.name, 18, 12);
        c.live = ui_dot(c.box, 8, C_MINT);
        c.today = ui_label(c.box, "", &font_or_ui20, C_TEXT);
        lv_obj_align(c.today, LV_ALIGN_TOP_RIGHT, -18, 12);
        c.tag = ui_caps(c.box, "TODAY", C_DIM);
        c.periods = ui_label(c.box, "", &font_or_mono, C_DIM);
        lv_obj_set_pos(c.periods, 18, 40);
        c.track = ui_box(c.box, TRACK_W, 6, C_HAIRLINE, 3);
        lv_obj_set_pos(c.track, 18, 70);
        c.fill = ui_box(c.box, 0, 6, C_ACCENT, 3);
        lv_obj_set_pos(c.fill, 18, 70);
        c.limit = ui_label(c.box, "", &font_or_mono, C_DIM);
        lv_obj_align(c.limit, LV_ALIGN_TOP_RIGHT, -18, 61);
        ui_show(c.box, false);
    }
}

static void apply(const Model& m, uint16_t parts, bool visible) {
    if (!(parts & M_KEYS)) return;
    char b[64], d[24], w[24], mo[24];
    n_keys = m.n_keys;
    int total_h = n_keys * CARD_H + (n_keys > 0 ? (n_keys - 1) * CARD_GAP : 0);
    int top = CONTENT_TOP + 6 + ((CONTENT_BOTTOM - CONTENT_TOP - 6) - total_h) / 2;
    for (int i = 0; i < MAX_KEYS; i++) {
        Card& c = card[i];
        ui_show(c.box, i < n_keys);
        if (i >= n_keys) continue;
        const KeyRow& k = m.keys[i];
        card_y[i] = (int16_t)(top + i * (CARD_H + CARD_GAP));
        lv_obj_set_pos(c.box, CARD_X, card_y[i]);

        ui_text(c.name, k.name[0] ? k.name : "(unnamed key)");
        lv_obj_set_style_text_color(c.name, k.disabled ? C_DIM : C_TEXT, 0);
        lv_obj_update_layout(c.name);
        lv_obj_set_pos(c.live, 18 + lv_obj_get_width(c.name) + 10, 21);
        used_today[i] = k.day > 0 && !k.disabled;
        ui_show(c.live, used_today[i]);

        fmt_money(k.day, d, sizeof(d));
        ui_text(c.today, d);
        lv_obj_update_layout(c.today);
        lv_obj_align_to(c.tag, c.today, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        if (k.disabled) { ui_text(c.tag, "DISABLED"); lv_obj_set_style_text_color(c.tag, C_CORAL, 0); }
        else { ui_text(c.tag, "TODAY"); lv_obj_set_style_text_color(c.tag, C_DIM, 0); }

        fmt_money(k.week, w, sizeof(w));
        fmt_money(k.month, mo, sizeof(mo));
        snprintf(b, sizeof(b), "week %s \xC2\xB7 month %s", w, mo);
        ui_text(c.periods, b);

        bool has_limit = k.limit > 0;
        ui_show(c.track, has_limit);
        ui_show(c.fill, has_limit);
        if (has_limit) {
            int32_t used = k.remaining >= 0 ? k.limit - k.remaining : 0;
            if (used < 0) used = 0;
            float f = (float)used / k.limit;
            if (f > 1) f = 1;
            fill_w[i] = (int16_t)(f > 0 && f * TRACK_W < 6 ? 6 : f * TRACK_W);
            lv_obj_set_style_bg_color(c.fill, f >= 0.9f ? C_CORAL : (f >= 0.75f ? C_AMBER : C_ACCENT), 0);
            fmt_money_short(used, d, sizeof(d));
            fmt_money_short(k.limit, w, sizeof(w));
            snprintf(b, sizeof(b), "%s / %s", d, w);
        } else {
            fill_w[i] = 0;
            snprintf(b, sizeof(b), "no limit");
        }
        ui_text(c.limit, b);
        lv_obj_align(c.limit, LV_ALIGN_TOP_RIGHT, -18, has_limit ? 61 : 40);
    }
    if (visible) enter_ms = lv_tick_get();
}

static void enter(void) {
    enter_ms = lv_tick_get();
    for (int i = 0; i < MAX_KEYS; i++) lv_obj_set_width(card[i].fill, 0);
}

static void frame(uint32_t now) {
    uint32_t since = now - enter_ms;
    for (int i = 0; i < n_keys; i++) {
        Card& c = card[i];
        // Cards rise in, staggered; limit bars fill after.
        float t = ease_out(((int32_t)since - i * 70) / 420.0f);
        int y = card_y[i] + (int)(18 * (1 - t));
        if (lv_obj_get_y(c.box) != y) lv_obj_set_y(c.box, y);
        int fw = (int)(fill_w[i] * ease_out(((int32_t)since - 250 - i * 70) / 600.0f));
        if (lv_obj_get_style_width(c.fill, LV_PART_MAIN) != fw) lv_obj_set_width(c.fill, fw);
        if (used_today[i]) ui_opa(c.live, 90 + (int)(165 * breathe(now + i * 300, 1800)));
    }
}

static bool has_data(const Model& m) { return m.have_keys && m.n_keys > 0; }

const Screen SCREEN_KEYS = { "KEYS", build, apply, enter, frame, has_data };
