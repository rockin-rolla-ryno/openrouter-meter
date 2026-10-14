// UI shell: header, page dots, navigation, waiting/pairing view, alerts and
// burn-in protection. Screens live in scr_*.cpp.
#include "ui.h"
#include "screens.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FRAME_MS 33
#define STALE_MS 90000
#define SHIFT_MS (4 * 60 * 1000)

static const Screen* const SCREENS[] = {
    &SCREEN_CREDITS, &SCREEN_ROUTES, &SCREEN_SPEND,
    &SCREEN_TOKENS, &SCREEN_PROVIDERS, &SCREEN_KEYS,
};
#define N_SCREENS ((int)(sizeof(SCREENS) / sizeof(SCREENS[0])))

static lv_obj_t* stage;                   // everything; nudged for burn-in protection
static lv_obj_t* group[N_SCREENS];
static lv_obj_t *live_dot, *lbl_brand, *lbl_section, *lbl_clock, *batt_body, *batt_fill, *batt_tip, *batt_bolt;
static lv_obj_t* page_dot[N_SCREENS];
static lv_obj_t* lbl_empty;

static Model    M = {};
static int      cur = 0;
static bool     linked = false, bonded = false, pair_prompt = false;
static uint32_t last_core_ms = 0, last_frame_ms = 0, last_shift_ms = 0;
static int      view = -1;                // 0 waiting, 1 data
static bool     wake_request = false, tap_flag = false;
static int      shift_idx = 0;

// Clock
static int64_t  clock_base = 0;
static uint32_t clock_base_ms = 0;
static int      clock_last_min = -1;

// ---------------------------------------------------------------------------
// Waiting / pairing view
// ---------------------------------------------------------------------------
#define ORB_N 3
static lv_obj_t *grp_wait, *wait_core, *orb[ORB_N], *lbl_w_title, *lbl_w_1, *lbl_w_2;

static void build_wait(void) {
    grp_wait = ui_group(stage);
    lv_obj_t* hub = ui_dot(grp_wait, 76, C_SURFACE);
    lv_obj_set_style_border_width(hub, 3, 0);
    lv_obj_set_style_border_color(hub, C_ACCENT, 0);
    ui_center(hub, 240, 196);
    wait_core = ui_dot(grp_wait, 24, C_PACKET);
    ui_center(wait_core, 240, 196);
    const lv_color_t oc[ORB_N] = { C_PACKET, C_ACCENT, C_MINT };
    for (int i = 0; i < ORB_N; i++) orb[i] = ui_dot(grp_wait, 10 - i * 2, oc[i]);
    lbl_w_title = ui_label(grp_wait, "", &font_or_ui20, C_TEXT);
    lv_obj_align(lbl_w_title, LV_ALIGN_TOP_MID, 0, 300);
    lbl_w_1 = ui_label(grp_wait, "", &font_or_ui16, C_DIM);
    lv_obj_align(lbl_w_1, LV_ALIGN_TOP_MID, 0, 334);
    lbl_w_2 = ui_label(grp_wait, "", &font_or_ui16, C_DIM);
    lv_obj_align(lbl_w_2, LV_ALIGN_TOP_MID, 0, 358);
}

static void wait_text(const char* t, const char* a, const char* b, lv_color_t tc) {
    ui_text(lbl_w_title, t);
    ui_text(lbl_w_1, a);
    ui_text(lbl_w_2, b);
    lv_obj_set_style_text_color(lbl_w_title, tc, 0);
}

static void wait_frame(uint32_t now) {
    const uint32_t period[ORB_N] = { 2400, 3300, 4700 };
    float speed = pair_prompt ? 3.0f : 1.0f;
    for (int i = 0; i < ORB_N; i++) {
        float a = ((uint32_t)(now * speed) % period[i]) / (float)period[i] * 6.2831853f + i * 2.1f;
        if (i == 1) a = -a;
        ui_center(orb[i], 240 + 64 * cosf(a), 196 + 59 * sinf(a));
    }
    ui_opa(wait_core, 120 + (int)(135 * breathe(now, 2200)));
}

// ---------------------------------------------------------------------------
// Alerts: low balance / daily budget. Banner slides in on the top layer,
// the screen edges glow, and the panel is woken to full brightness.
// ---------------------------------------------------------------------------
static lv_obj_t *banner, *ban_dot, *ban_title, *ban_detail, *edge[4];
static uint32_t alert_start = 0;
static bool     alert_low_armed = true, alert_budget_armed = true;
static bool     alert_active_low = false, alert_active_budget = false;
static lv_color_t alert_color;

static void build_alerts(void) {
    lv_obj_t* top = lv_layer_top();
    banner = ui_box(top, 440, 64, C_SURFACE, 18);
    lv_obj_set_style_border_width(banner, 2, 0);
    lv_obj_set_pos(banner, 20, -80);
    ban_dot = ui_dot(banner, 12, C_CORAL);
    lv_obj_set_pos(ban_dot, 18, 26);
    ban_title = ui_label(banner, "", &font_or_ui20, C_TEXT);
    lv_obj_set_pos(ban_title, 42, 9);
    ban_detail = ui_label(banner, "", &font_or_mono, C_DIM);
    lv_obj_set_pos(ban_detail, 42, 35);
    const int thick = 5;
    edge[0] = ui_box(top, W, thick, C_CORAL, 0); lv_obj_set_pos(edge[0], 0, 0);
    edge[1] = ui_box(top, W, thick, C_CORAL, 0); lv_obj_set_pos(edge[1], 0, H - thick);
    edge[2] = ui_box(top, thick, H, C_CORAL, 0); lv_obj_set_pos(edge[2], 0, 0);
    edge[3] = ui_box(top, thick, H, C_CORAL, 0); lv_obj_set_pos(edge[3], W - thick, 0);
    for (int i = 0; i < 4; i++) ui_opa(edge[i], 0);
}

static void raise_alert(const char* title, const char* detail, lv_color_t c) {
    alert_color = c;
    ui_text(ban_title, title);
    ui_text(ban_detail, detail);
    lv_obj_set_style_bg_color(ban_dot, c, 0);
    lv_obj_set_style_border_color(banner, c, 0);
    for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(edge[i], c, 0);
    alert_start = lv_tick_get();
    wake_request = true;
}

static void check_alerts(void) {
    char a[24], b[24], d[64];
    if (!M.have_core) return;                 // thresholds can arrive before any data
    // Low balance: fire once when it drops below the threshold; re-arm after a top-up.
    alert_active_low = M.alert_low_bal > 0 && M.bal < M.alert_low_bal;
    if (alert_active_low && alert_low_armed) {
        fmt_money(M.bal, a, sizeof(a));
        fmt_money(M.alert_low_bal, b, sizeof(b));
        snprintf(d, sizeof(d), "%s left \xC2\xB7 alert below %s", a, b);
        raise_alert("Low balance", d, C_CORAL);
        alert_low_armed = false;
    } else if (!alert_active_low) alert_low_armed = true;

    // Daily budget: once per UTC day (spend-today resets to 0 at midnight UTC).
    alert_active_budget = M.alert_day_budget > 0 && M.day >= M.alert_day_budget;
    if (alert_active_budget && alert_budget_armed) {
        fmt_money(M.day, a, sizeof(a));
        fmt_money(M.alert_day_budget, b, sizeof(b));
        snprintf(d, sizeof(d), "%s spent today \xC2\xB7 budget %s", a, b);
        raise_alert("Daily budget reached", d, C_AMBER);
        alert_budget_armed = false;
    } else if (!alert_active_budget) alert_budget_armed = true;
}

static bool alert_banner_visible(void) {
    return alert_start && lv_tick_get() - alert_start < 12000;
}

static void alerts_frame(uint32_t now) {
    if (!alert_start) return;
    uint32_t t = now - alert_start;
    if (t >= 12500) {
        lv_obj_set_y(banner, -80);
        for (int i = 0; i < 4; i++) ui_opa(edge[i], 0);
        alert_start = 0;
        return;
    }
    int y = t < 450 ? -80 + (int)(94 * ease_out(t / 450.0f))
          : t > 12000 ? 14 - (int)(94 * ease_smooth((t - 12000) / 500.0f)) : 14;
    if (lv_obj_get_y(banner) != y) lv_obj_set_y(banner, y);
    int glow = t < 8000 ? (int)(60 + 140 * breathe(t, 1200)) : 0;
    for (int i = 0; i < 4; i++) ui_opa(edge[i], glow);
}

// ---------------------------------------------------------------------------
// Header / page dots
// ---------------------------------------------------------------------------
static void build_header(void) {
    live_dot = ui_dot(stage, 10, C_FAINT);
    lv_obj_set_pos(live_dot, MARGIN, 29);
    lbl_brand = ui_caps(stage, "OPENROUTER", C_TEXT);
    lv_obj_set_pos(lbl_brand, MARGIN + 18, 24);
    lbl_section = ui_caps(stage, "", C_DIM);
    lv_obj_align_to(lbl_section, lbl_brand, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lbl_clock = ui_caps(stage, "", C_DIM);

    // Battery: a drawn outline + fill (no bitmap assets).
    batt_body = ui_box(stage, 30, 16, C_BG, 4);
    lv_obj_set_style_border_width(batt_body, 2, 0);
    lv_obj_set_style_border_color(batt_body, C_DIM, 0);
    lv_obj_set_pos(batt_body, W - MARGIN - 34, 25);
    batt_tip = ui_box(stage, 3, 6, C_DIM, 1);
    lv_obj_set_pos(batt_tip, W - MARGIN - 3, 30);
    batt_fill = ui_box(stage, 0, 8, C_DIM, 2);
    lv_obj_set_pos(batt_fill, W - MARGIN - 30, 29);
    batt_bolt = ui_caps(stage, "+", C_MINT);
    lv_obj_set_style_text_letter_space(batt_bolt, 0, 0);
    lv_obj_set_pos(batt_bolt, W - MARGIN - 52, 23);
    ui_show(batt_bolt, false);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -(MARGIN + 46), 24);

    for (int i = 0; i < N_SCREENS; i++) page_dot[i] = ui_box(stage, 6, 6, C_FAINT, 3);
}

static bool screen_enabled(int i) {
    return M.screens_mask == 0 || (M.screens_mask & (1 << i));
}

static void layout_page_dots(void) {
    const int gap = 8, y = 458;
    int total = 0, n = 0;
    for (int i = 0; i < N_SCREENS; i++) if (screen_enabled(i)) { total += (i == cur ? 20 : 6) + (n ? gap : 0); n++; }
    int x = (W - total) / 2;
    for (int i = 0; i < N_SCREENS; i++) {
        bool on = screen_enabled(i) && view == 1;
        ui_show(page_dot[i], on);
        if (!screen_enabled(i)) continue;
        bool a = i == cur;
        lv_obj_set_size(page_dot[i], a ? 20 : 6, 6);
        lv_obj_set_pos(page_dot[i], x, y);
        lv_obj_set_style_bg_color(page_dot[i], a ? C_ACCENT : C_FAINT, 0);
        x += (a ? 20 : 6) + gap;
    }
}

static void set_section(const char* s, lv_color_t c) {
    if (strcmp(lv_label_get_text(lbl_section), s) != 0) {
        lv_label_set_text(lbl_section, s);
        lv_obj_align_to(lbl_section, lbl_brand, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    }
    lv_obj_set_style_text_color(lbl_section, c, 0);
}

// ---------------------------------------------------------------------------
// View state
// ---------------------------------------------------------------------------
static void enter_current(void) {
    bool has = SCREENS[cur]->has_data(M);
    ui_show(lbl_empty, !has);
    if (has) SCREENS[cur]->enter();
}

static void update_view(void) {
    uint32_t now = lv_tick_get();
    int v = (linked && M.have_core && !M.key_error && !pair_prompt) ? 1 : 0;
    if (v != view) {
        view = v;
        ui_show(grp_wait, v == 0);
        layout_page_dots();
        if (v == 1) enter_current();
    }
    for (int i = 0; i < N_SCREENS; i++) ui_show(group[i], v == 1 && i == cur);
    if (v == 1) ui_show(lbl_empty, !SCREENS[cur]->has_data(M));
    else ui_show(lbl_empty, false);

    bool stale = M.have_core && now - last_core_ms >= STALE_MS;
    bool alerting = alert_active_low || alert_active_budget;
    lv_color_t dc = !linked ? C_FAINT
                  : (v == 1 && !stale) ? (alerting ? alert_color : C_MINT) : C_AMBER;
    lv_obj_set_style_bg_color(live_dot, dc, 0);

    if (v == 1) {
        if (stale) {
            char b[32];
            uint32_t mins = (now - last_core_ms) / 60000;
            if (mins < 60) snprintf(b, sizeof(b), "STALE \xC2\xB7 %lum", (unsigned long)(mins ? mins : 1));
            else snprintf(b, sizeof(b), "STALE \xC2\xB7 %luh", (unsigned long)(mins / 60));
            set_section(b, C_AMBER);
        } else set_section(SCREENS[cur]->title, C_DIM);
        return;
    }
    set_section("", C_DIM);
    if (pair_prompt)
        wait_text("Release to pair a new PC", "This forgets the current PC.", "Keep holding (8 s) to power off instead.", C_AMBER);
    else if (!linked && !bonded)
        wait_text("Ready to pair", "Open the OpenRouter Meter app on your PC.", "It finds and pairs with this meter by itself.", C_TEXT);
    else if (!linked)
        wait_text("Waiting for your PC", "Start the OpenRouter Meter app,", "or wake your PC.", C_TEXT);
    else if (M.key_error)
        wait_text("Check your OpenRouter key", "The app's management key is missing or was rejected.", "Open the app's settings from the tray icon.", C_AMBER);
    else
        wait_text("Connected", "Fetching your OpenRouter account", "", C_TEXT);
}

static void show_screen(int i) {
    cur = i;
    layout_page_dots();
    if (view == 1) enter_current();
    update_view();
}

static int step(int dir) {
    int i = cur;
    for (int k = 0; k < N_SCREENS; k++) {
        i = (i + dir + N_SCREENS) % N_SCREENS;
        if (screen_enabled(i)) return i;
    }
    return cur;
}

static void on_tap(lv_event_t*) {
    tap_flag = true;
    if (alert_banner_visible()) {          // first tap dismisses the alert
        alert_start = lv_tick_get() - 12000;
        return;
    }
    if (view == 1) show_screen(step(+1));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void ui_begin(void) {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_tap, LV_EVENT_CLICKED, NULL);

    stage = lv_obj_create(scr);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, W, H);
    lv_obj_remove_flag(stage, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    for (int i = 0; i < N_SCREENS; i++) {
        group[i] = ui_group(stage);
        SCREENS[i]->build(group[i]);
    }
    lbl_empty = ui_label(stage, "Gathering data\xE2\x80\xA6", &font_or_ui16, C_DIM);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, 0);
    ui_show(lbl_empty, false);
    build_wait();
    build_header();
    build_alerts();
    update_view();
    layout_page_dots();
}

void ui_apply(const Model& m, uint16_t parts) {
    M = m;
    if (parts & M_CORE) {
        last_core_ms = lv_tick_get();
        if (m.now_local > 0) {
            clock_base = m.now_local;
            clock_base_ms = last_core_ms;
        } else if (clock_base) {
            clock_base = 0;
            clock_last_min = -1;
            ui_text(lbl_clock, "");
        }
    }
    for (int i = 0; i < N_SCREENS; i++) SCREENS[i]->apply(m, parts, view == 1 && i == cur);
    if (parts & (M_CORE | M_CFG)) check_alerts();
    if (parts & M_CFG) {
        if (!screen_enabled(cur)) cur = step(+1);
        layout_page_dots();
    }
    update_view();
}

void ui_frame(void) {
    uint32_t now = lv_tick_get();

    if (clock_base > 0) {
        time_t t = (time_t)(clock_base + (now - clock_base_ms) / 1000);
        struct tm tmv;
        gmtime_r(&t, &tmv);
        if (tmv.tm_min != clock_last_min) {
            clock_last_min = tmv.tm_min;
            char b[12];
            if (M.clock24) snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            else {
                int h12 = tmv.tm_hour % 12; if (!h12) h12 = 12;
                snprintf(b, sizeof(b), "%d:%02d %s", h12, tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
            }
            ui_text(lbl_clock, b);
        }
    }

    if (now - last_frame_ms < FRAME_MS) return;
    last_frame_ms = now;
    update_view();

    // Burn-in protection: nudge everything by a couple of pixels every 4 min.
    if (now - last_shift_ms > SHIFT_MS) {
        static const int8_t dx[5] = { 0, 2, 1, -2, -1 }, dy[5] = { 0, 1, -2, -1, 2 };
        last_shift_ms = now;
        shift_idx = (shift_idx + 1) % 5;
        lv_obj_set_pos(stage, dx[shift_idx], dy[shift_idx]);
    }

    if (view == 1) ui_opa(live_dot, 110 + (int)(145 * breathe(now, 2400)));
    else ui_opa(live_dot, 255);

    alerts_frame(now);
    if (view == 0) { wait_frame(now); return; }
    if (SCREENS[cur]->has_data(M)) SCREENS[cur]->frame(now);
}

void ui_next(void) { if (view == 1) show_screen(step(+1)); }
void ui_prev(void) { if (view == 1) show_screen(step(-1)); }

void ui_set_link(LinkState s, bool b) {
    linked = s == LINK_CONNECTED;
    bonded = b;
    update_view();
}

void ui_set_pair_prompt(bool armed) {
    pair_prompt = armed;
    update_view();
}

void ui_set_battery(BatteryInfo b) {
    bool has = b.percent >= 0;
    ui_show(batt_body, has || b.usb_power);
    ui_show(batt_tip, has || b.usb_power);
    int pct = has ? b.percent : 100;
    int w = (int)(24 * pct / 100.0f + 0.5f);
    if (w < 2) w = 2;
    lv_obj_set_width(batt_fill, w);
    lv_obj_set_style_bg_color(batt_fill, has && pct <= 15 && !b.charging ? C_CORAL : C_DIM, 0);
    ui_show(batt_bolt, b.charging || (!has && b.usb_power));
}

bool ui_take_wake_request(void) { bool w = wake_request; wake_request = false; return w; }
bool ui_take_tap(void) { bool t = tap_flag; tap_flag = false; return t; }
