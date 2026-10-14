#include "common.h"
#include <stdio.h>
#include <string.h>

const lv_color_t SERIES[MAX_ROWS] = {
    lv_color_hex(0x7C83FF), lv_color_hex(0x38E1FF), lv_color_hex(0x3EE6A0),
    lv_color_hex(0xFFB547), lv_color_hex(0xFF7AC6),
};

static void inert(lv_obj_t* o) {
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
}

lv_obj_t* ui_box(lv_obj_t* parent, int w, int h, lv_color_t c, int radius) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    inert(o);
    return o;
}

lv_obj_t* ui_dot(lv_obj_t* parent, int d, lv_color_t c) {
    return ui_box(parent, d, d, c, LV_RADIUS_CIRCLE);
}

lv_obj_t* ui_label(lv_obj_t* parent, const char* txt, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

lv_obj_t* ui_caps(lv_obj_t* parent, const char* txt, lv_color_t c) {
    lv_obj_t* l = ui_label(parent, txt, &font_or_cap, c);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    return l;
}

lv_obj_t* ui_group(lv_obj_t* parent) {
    lv_obj_t* g = lv_obj_create(parent);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, W, H);
    inert(g);
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
    return g;
}

lv_obj_t* ui_arc(lv_obj_t* parent, int d, int width, lv_color_t track, lv_color_t ind) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, track, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, ind, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    inert(a);
    return a;
}

void ui_center(lv_obj_t* o, float cx, float cy) {
    int w = lv_obj_get_style_width(o, LV_PART_MAIN);
    int h = lv_obj_get_style_height(o, LV_PART_MAIN);
    if (w == LV_SIZE_CONTENT || h == LV_SIZE_CONTENT) {      // labels: measure first
        lv_obj_update_layout(o);
        w = lv_obj_get_width(o);
        h = lv_obj_get_height(o);
    }
    lv_obj_set_pos(o, (int32_t)lroundf(cx - w / 2.0f), (int32_t)lroundf(cy - h / 2.0f));
}

void ui_show(lv_obj_t* o, bool on) {
    if (!o) return;
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (on && hidden) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!on && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// style_opa would render the object through an off-screen layer (slow and
// heap-hungry without PSRAM); per-part opacity draws directly.
void ui_opa(lv_obj_t* o, int opa) {
    lv_opa_t v = (lv_opa_t)(opa < 0 ? 0 : (opa > 255 ? 255 : opa));
    if (lv_obj_check_type(o, &lv_label_class)) {
        if (lv_obj_get_style_text_opa(o, LV_PART_MAIN) != v) lv_obj_set_style_text_opa(o, v, 0);
    } else if (lv_obj_check_type(o, &lv_line_class)) {
        if (lv_obj_get_style_line_opa(o, LV_PART_MAIN) != v) lv_obj_set_style_line_opa(o, v, 0);
    } else if (lv_obj_check_type(o, &lv_arc_class)) {
        // Indicator only: some arcs keep a hidden (transparent) track.
        if (lv_obj_get_style_arc_opa(o, LV_PART_INDICATOR) != v) lv_obj_set_style_arc_opa(o, v, LV_PART_INDICATOR);
    } else if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != v) {
        lv_obj_set_style_bg_opa(o, v, 0);
        lv_obj_set_style_border_opa(o, v, 0);
    }
}

void ui_text(lv_obj_t* l, const char* s) {
    if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}

void fmt_int(int64_t v, char* buf, size_t n) {
    char tmp[24];
    bool neg = v < 0;
    snprintf(tmp, sizeof(tmp), "%lld", (long long)(neg ? -v : v));
    int len = (int)strlen(tmp), o = 0;
    if (neg && o < (int)n - 1) buf[o++] = '-';
    for (int i = 0; i < len && o < (int)n - 1; i++) {
        if (i > 0 && (len - i) % 3 == 0 && o < (int)n - 1) buf[o++] = ',';
        buf[o++] = tmp[i];
    }
    buf[o] = 0;
}

void fmt_money(int32_t c, char* buf, size_t n) {
    bool neg = c < 0;
    int64_t v = neg ? -(int64_t)c : c;
    char whole[24];
    fmt_int(v / 100, whole, sizeof(whole));
    snprintf(buf, n, "%s$%s.%02d", neg ? "-" : "", whole, (int)(v % 100));
}

void fmt_money_short(int32_t c, char* buf, size_t n) {
    if (c >= 100000) snprintf(buf, n, "$%.1fk", c / 100000.0);
    else fmt_money(c, buf, n);
}

void fmt_count(int64_t v, char* buf, size_t n) {
    if (v < 0) v = 0;
    if (v < 10000)              fmt_int(v, buf, n);
    else if (v < 100000)        snprintf(buf, n, "%.1fk", v / 1e3);
    else if (v < 1000000)       snprintf(buf, n, "%.0fk", v / 1e3);
    else if (v < 1000000000LL)  snprintf(buf, n, "%.1fM", v / 1e6);
    else                        snprintf(buf, n, "%.1fB", v / 1e9);
}
