// PROVIDERS — who actually served your requests. A segmented ring sweeps
// in, then the spotlight cycles through the providers while a packet orbits.
#include "screens.h"
#include <stdio.h>
#include <string.h>

#define CX 132
#define CY 262
#define RING_D 212
#define RING_W 24
#define LIFT_W 32
#define SEG_GAP 3
#define LEG_X 270
#define SPOT_MS 2600

static lv_obj_t *track, *seg[MAX_ROWS], *orbiter, *lbl_count, *lbl_count_sub;
static lv_obj_t *dot[MAX_ROWS], *lbl_name[MAX_ROWS], *lbl_meta[MAX_ROWS];
static int n_rows = 0;
static int16_t a0[MAX_ROWS], a1[MAX_ROWS];   // segment start/end angles (deg, 0 = top)
static uint32_t enter_ms = 0;
static int spot = -1;

static void build(lv_obj_t* g) {
    track = ui_arc(g, RING_D, 2, C_HAIRLINE, C_HAIRLINE);
    lv_arc_set_bg_angles(track, 0, 360);
    lv_obj_remove_style(track, NULL, LV_PART_INDICATOR);
    ui_center(track, CX, CY);

    for (int i = 0; i < MAX_ROWS; i++) {
        seg[i] = ui_arc(g, RING_D, RING_W, C_BG, SERIES[i]);
        lv_obj_set_style_arc_opa(seg[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(seg[i], false, LV_PART_INDICATOR);
        lv_arc_set_rotation(seg[i], 270);
        lv_arc_set_bg_angles(seg[i], 0, 360);
        lv_arc_set_angles(seg[i], 0, 0);
        ui_center(seg[i], CX, CY);
        ui_show(seg[i], false);
    }
    orbiter = ui_dot(g, 9, C_PACKET);
    ui_opa(orbiter, 0);

    lbl_count = ui_label(g, "--", &font_or_num, C_TEXT);
    lbl_count_sub = ui_caps(g, "PROVIDERS", C_DIM);
    lv_obj_align(lbl_count_sub, LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; i < MAX_ROWS; i++) {
        dot[i] = ui_dot(g, 10, SERIES[i]);
        lbl_name[i] = ui_label(g, "", &font_or_ui20, C_TEXT);
        lv_label_set_long_mode(lbl_name[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_name[i], W - LEG_X - MARGIN);
        lbl_meta[i] = ui_label(g, "", &font_or_mono, C_DIM);
        ui_show(dot[i], false); ui_show(lbl_name[i], false); ui_show(lbl_meta[i], false);
    }
}

static int row_y(int i, int n) {
    int step = 66, span = (n - 1) * step;
    return CY - span / 2 + i * step;
}

static void apply(const Model& m, uint16_t parts, bool visible) {
    if (!(parts & M_PROV)) return;
    char b[48], c[24];
    n_rows = m.n_prov;
    int32_t total = m.prov_total;
    if (total <= 0) for (int i = 0; i < n_rows; i++) total += m.prov[i].req;

    snprintf(b, sizeof(b), "%d", m.prov_count);
    ui_text(lbl_count, b);
    lv_obj_update_layout(lbl_count);
    ui_center(lbl_count, CX, CY - 10);
    lv_obj_update_layout(lbl_count_sub);
    ui_center(lbl_count_sub, CX, CY + 22);

    float acc = 0;
    for (int i = 0; i < MAX_ROWS; i++) {
        bool on = i < n_rows;
        ui_show(seg[i], on); ui_show(dot[i], on); ui_show(lbl_name[i], on); ui_show(lbl_meta[i], on);
        if (!on) continue;
        const ShareRow& r = m.prov[i];
        bool others = !strcmp(r.name, "others");
        lv_color_t col = others ? C_OTHERS : SERIES[i];
        float share = total > 0 ? (float)r.req / total : 0;
        a0[i] = (int16_t)lroundf(acc * 360);
        acc += share;
        a1[i] = (int16_t)lroundf(acc * 360);
        if (i == n_rows - 1) a1[i] = 360;
        lv_obj_set_style_arc_color(seg[i], col, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(dot[i], col, 0);

        int y = row_y(i, n_rows);
        lv_obj_set_pos(dot[i], LEG_X, y - 15);
        ui_text(lbl_name[i], others ? "other providers" : r.name);
        lv_obj_set_style_text_color(lbl_name[i], others ? C_DIM : C_TEXT, 0);
        lv_obj_set_pos(lbl_name[i], LEG_X + 20, y - 24);
        fmt_money(r.cents, c, sizeof(c));
        snprintf(b, sizeof(b), "%d%% \xC2\xB7 %s", (int)lroundf(share * 100), c);
        ui_text(lbl_meta[i], b);
        lv_obj_set_pos(lbl_meta[i], LEG_X + 20, y + 1);
    }
    if (visible) enter_ms = lv_tick_get();
}

static void enter(void) {
    enter_ms = lv_tick_get();
    spot = -1;
    for (int i = 0; i < MAX_ROWS; i++) {
        lv_arc_set_angles(seg[i], 0, 0);
        lv_obj_set_style_arc_width(seg[i], RING_W, LV_PART_INDICATOR);
    }
    ui_opa(orbiter, 0);
}

static void frame(uint32_t now) {
    uint32_t since = now - enter_ms;
    const uint32_t SWEEP = 900;
    // 1) Sweep: the ring fills clockwise from the top; legend rows fade in.
    float sweep_deg = 360.0f * ease_out(since / (float)SWEEP);
    for (int i = 0; i < n_rows; i++) {
        int s = a0[i] + (i ? SEG_GAP / 2 : 0), e = a1[i] - SEG_GAP / 2;
        if (e < s) e = s;
        int ee = (int)fminf((float)e, sweep_deg);
        int ss = s;
        if (ee <= ss) { lv_arc_set_angles(seg[i], 0, 0); ui_show(seg[i], false); }
        else {
            ui_show(seg[i], true);
            if (lv_arc_get_angle_start(seg[i]) != ss || lv_arc_get_angle_end(seg[i]) != ee)
                lv_arc_set_angles(seg[i], ss, ee);
        }
        int opa = (int)(255 * ease_smooth(((int32_t)since - 250 - i * 90) / 300.0f));
        ui_opa(dot[i], opa);
        ui_opa(lbl_meta[i], opa);
        if (since < SWEEP + 600) ui_opa(lbl_name[i], opa);
    }
    if (since < SWEEP + 600 || n_rows == 0) return;

    // 2) Spotlight: one provider at a time lifts out of the ring.
    int s = (int)(((since - SWEEP - 600) / SPOT_MS) % n_rows);
    if (s != spot) {
        spot = s;
        for (int i = 0; i < n_rows; i++) {
            lv_obj_set_style_arc_width(seg[i], i == spot ? LIFT_W : RING_W, LV_PART_INDICATOR);
            ui_opa(seg[i], i == spot ? 255 : 185);
            ui_opa(lbl_name[i], i == spot ? 255 : 165);
        }
    }

    // 3) A packet orbits just outside the ring (7 s per lap).
    float a = ((now % 7000) / 7000.0f) * 6.2831853f - 1.5707963f;
    const float r = RING_D / 2.0f + 14;
    ui_center(orbiter, CX + r * cosf(a), CY + r * sinf(a));
    ui_opa(orbiter, 230);
}

static bool has_data(const Model& m) { return m.have_prov && m.n_prov > 0; }

const Screen SCREEN_PROVIDERS = { "PROVIDERS \xC2\xB7 30D", build, apply, enter, frame, has_data };
