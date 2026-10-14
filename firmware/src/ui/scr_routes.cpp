// ROUTES — the account hub routing packets to its top models. Route weight
// and packet rate follow each model's real share of 30-day requests.
#include "screens.h"
#include <stdio.h>
#include <string.h>

#define HUB_X 82
#define HUB_Y 262
#define HUB_D 64
#define NODE_X 222
#define NODE_D 16
#define PTS 24
#define PKT_N 16
#define TRAVEL_MS 1150

static lv_obj_t *hub_core, *lbl_total;
static lv_obj_t* line[MAX_ROWS];
static lv_point_precise_t pts[MAX_ROWS][PTS];
static lv_obj_t *node[MAX_ROWS], *lbl_name[MAX_ROWS], *lbl_meta[MAX_ROWS];
static int   n_rows = 0;
static float share[MAX_ROWS], spawn_acc[MAX_ROWS];
static uint32_t ping[MAX_ROWS], enter_ms = 0, last_ms = 0;

struct Packet { lv_obj_t* o; int8_t route; uint32_t t0; };
static Packet pk[PKT_N];

static int node_y(int i, int n) {
    if (n <= 1) return HUB_Y;
    int span = (n - 1) * 82;
    if (span > 328) span = 328;
    return HUB_Y - span / 2 + i * span / (n - 1);
}

static void bezier(float t, float y1, float* ox, float* oy) {
    const float x0 = HUB_X + HUB_D / 2.0f - 2, y0 = HUB_Y;
    const float x3 = NODE_X - NODE_D / 2.0f, y3 = y1;
    const float c1x = x0 + 58, c2x = x3 - 62;
    float u = 1 - t;
    *ox = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x3;
    *oy = u*u*u*y0 + 3*u*u*t*y0  + 3*u*t*t*y3  + t*t*t*y3;
}

static void build(lv_obj_t* g) {
    for (int i = 0; i < MAX_ROWS; i++) {
        line[i] = lv_line_create(g);
        lv_obj_set_style_line_rounded(line[i], true, 0);
        lv_obj_set_style_line_opa(line[i], LV_OPA_70, 0);
        lv_obj_remove_flag(line[i], LV_OBJ_FLAG_CLICKABLE);
        ui_show(line[i], false);
    }
    lv_obj_t* hub = ui_dot(g, HUB_D, C_SURFACE);
    lv_obj_set_style_border_width(hub, 3, 0);
    lv_obj_set_style_border_color(hub, C_ACCENT, 0);
    ui_center(hub, HUB_X, HUB_Y);
    hub_core = ui_dot(g, 20, C_PACKET);
    ui_center(hub_core, HUB_X, HUB_Y);

    lbl_total = ui_label(g, "--", &font_or_mono, C_TEXT);
    lv_obj_set_width(lbl_total, 140);
    lv_obj_set_style_text_align(lbl_total, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_total, HUB_X - 70, HUB_Y + HUB_D / 2 + 12);
    lv_obj_t* sub = ui_caps(g, "REQUESTS", C_DIM);
    lv_obj_set_width(sub, 140);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sub, HUB_X - 70, HUB_Y + HUB_D / 2 + 36);

    for (int i = 0; i < MAX_ROWS; i++) {
        node[i] = ui_dot(g, NODE_D, SERIES[i]);
        lbl_name[i] = ui_label(g, "", &font_or_ui20, C_TEXT);
        lv_label_set_long_mode(lbl_name[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_name[i], W - NODE_X - 20 - MARGIN + 6);
        lbl_meta[i] = ui_label(g, "", &font_or_mono, C_DIM);
        ui_show(node[i], false); ui_show(lbl_name[i], false); ui_show(lbl_meta[i], false);
    }
    for (int i = 0; i < PKT_N; i++) {
        pk[i].o = ui_dot(g, 8, C_PACKET);
        pk[i].route = -1;
        ui_show(pk[i].o, false);
    }
}

static void apply(const Model& m, uint16_t parts, bool) {
    if (!(parts & M_MODELS)) return;
    char b[48], c[24];
    n_rows = m.n_models;
    int32_t total = m.models_total;
    if (total <= 0) for (int i = 0; i < n_rows; i++) total += m.models[i].req;
    fmt_count(total, b, sizeof(b));
    ui_text(lbl_total, b);

    for (int i = 0; i < MAX_ROWS; i++) {
        bool on = i < n_rows;
        ui_show(line[i], on); ui_show(node[i], on); ui_show(lbl_name[i], on); ui_show(lbl_meta[i], on);
        if (!on) { share[i] = 0; continue; }
        const ShareRow& r = m.models[i];
        bool others = !strcmp(r.name, "others");
        lv_color_t col = others ? C_OTHERS : SERIES[i];
        share[i] = total > 0 ? (float)r.req / total : 0;
        int y = node_y(i, n_rows);
        for (int k = 0; k < PTS; k++) {
            float x, yy;
            bezier((float)k / (PTS - 1), (float)y, &x, &yy);
            pts[i][k].x = x;
            pts[i][k].y = yy;
        }
        lv_obj_set_style_line_width(line[i], 2 + (int)lroundf(share[i] * 8), 0);
        lv_obj_set_style_line_color(line[i], col, 0);
        lv_line_set_points(line[i], pts[i], PTS);
        lv_obj_set_style_bg_color(node[i], col, 0);
        lv_obj_set_size(node[i], NODE_D, NODE_D);
        ui_center(node[i], NODE_X, y);
        ui_text(lbl_name[i], others ? "other models" : r.name);
        lv_obj_set_style_text_color(lbl_name[i], others ? C_DIM : C_TEXT, 0);
        lv_obj_set_pos(lbl_name[i], NODE_X + 20, y - 24);
        fmt_money(r.cents, c, sizeof(c));
        snprintf(b, sizeof(b), "%d%% \xC2\xB7 %s", (int)lroundf(share[i] * 100), c);
        ui_text(lbl_meta[i], b);
        lv_obj_set_pos(lbl_meta[i], NODE_X + 20, y + 1);
    }
}

static void enter(void) {
    enter_ms = last_ms = lv_tick_get();
    for (int i = 0; i < PKT_N; i++) { pk[i].route = -1; ui_show(pk[i].o, false); }
    for (int i = 0; i < MAX_ROWS; i++) { spawn_acc[i] = 0.6f; ping[i] = 0; }
}

static void frame(uint32_t now) {
    ui_opa(hub_core, 150 + (int)(105 * breathe(now, 2200)));

    uint32_t since = now - enter_ms;
    bool drawing = false;
    for (int i = 0; i < n_rows; i++) {           // routes draw out, staggered
        int32_t local = (int32_t)since - i * 70;
        int n = (int)lroundf(ease_out(local / 520.0f) * (PTS - 1)) + 1;
        if (n < 2) n = local > 0 ? 2 : 0;
        if (n < PTS) drawing = true;
        lv_line_set_points(line[i], pts[i], n < 2 ? 0 : (n > PTS ? PTS : n));
        int opa = (int)(255 * ease_smooth((local - 300) / 300.0f));
        ui_opa(node[i], opa); ui_opa(lbl_name[i], opa); ui_opa(lbl_meta[i], opa);
    }

    uint32_t dt = now - last_ms;
    last_ms = now;
    if (!drawing) {
        for (int i = 0; i < n_rows; i++) {
            spawn_acc[i] += (share[i] > 0 ? 0.35f + 3.2f * share[i] : 0) * dt / 1000.0f;
            if (spawn_acc[i] < 1.0f) continue;
            spawn_acc[i] = spawn_acc[i] - 1.0f > 1.0f ? 1.0f : spawn_acc[i] - 1.0f;
            for (int k = 0; k < PKT_N; k++) {
                if (pk[k].route >= 0) continue;
                pk[k].route = (int8_t)i;
                pk[k].t0 = now;
                lv_obj_set_style_bg_color(pk[k].o, i == 0 ? C_PACKET : SERIES[i], 0);
                ui_show(pk[k].o, true);
                break;
            }
        }
    }
    for (int k = 0; k < PKT_N; k++) {
        int r = pk[k].route;
        if (r < 0) continue;
        float t = (now - pk[k].t0) / (float)TRAVEL_MS;
        if (t >= 1.0f || r >= n_rows) {
            pk[k].route = -1;
            ui_show(pk[k].o, false);
            if (r < n_rows) ping[r] = now;
            continue;
        }
        float e = ease_smooth(t) * (PTS - 1);
        int s = (int)e; if (s >= PTS - 1) s = PTS - 2;
        float f = e - s;
        ui_center(pk[k].o, pts[r][s].x + (pts[r][s + 1].x - pts[r][s].x) * f,
                           pts[r][s].y + (pts[r][s + 1].y - pts[r][s].y) * f);
        ui_opa(pk[k].o, (int)(255 * clampf(t / 0.12f, 0, 1)));
    }
    for (int i = 0; i < n_rows; i++) {           // node ping on arrival
        int d = NODE_D;
        if (ping[i]) {
            uint32_t pt = now - ping[i];
            if (pt > 320) ping[i] = 0;
            else d = NODE_D + (int)(8 * sinf(pt / 320.0f * 3.14159f));
        }
        if (lv_obj_get_style_width(node[i], LV_PART_MAIN) != d) {
            lv_obj_set_size(node[i], d, d);
            ui_center(node[i], NODE_X, node_y(i, n_rows));
        }
    }
}

static bool has_data(const Model& m) { return m.have_models && m.n_models > 0; }

const Screen SCREEN_ROUTES = { "ROUTES \xC2\xB7 30D", build, apply, enter, frame, has_data };
