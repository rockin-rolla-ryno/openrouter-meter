// Wire protocol: one small JSON object per Bluetooth write, tagged by "m".
// See docs/protocol.md for the full field list.
#include "model.h"
#include <ArduinoJson.h>
#include <string.h>

static void copy_name(char* dst, size_t n, const char* src) {
    if (!src) src = "";
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int read_rows(JsonArrayConst rows, ShareRow* out) {
    int n = 0;
    for (JsonArrayConst r : rows) {
        if (n >= MAX_ROWS) break;
        copy_name(out[n].name, sizeof(out[n].name), r[0] | "");
        out[n].req = r[1] | 0;
        out[n].cents = r[2] | 0;
        n++;
    }
    return n;
}

uint16_t protocol_apply(const char* json, Model* m) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return 0;
    const char* kind = doc["m"] | "";

    if (!strcmp(kind, "core")) {
        m->bal        = doc["bal"] | 0;
        m->credits    = doc["cr"]  | 0;
        m->day        = doc["day"] | 0;
        m->month      = doc["mon"] | 0;
        m->s30        = doc["s30"] | 0;
        m->rq30       = doc["rq30"] | 0;
        m->yday       = doc["yd"]  | 0;
        m->yday_req   = doc["yrq"] | 0;
        m->yday_tok   = doc["ytk"] | (int64_t)0;
        m->now_local  = doc["now"] | (int64_t)0;
        m->clock24    = (doc["h24"] | 1) != 0;
        m->have_core  = true;
        m->key_error  = false;
        return M_CORE;
    }
    if (!strcmp(kind, "hist")) {
        JsonArrayConst d = doc["d"].as<JsonArrayConst>();
        if (d.isNull()) return 0;
        for (int i = 0; i < HIST_DAYS; i++) m->hist[i] = 0;
        int n = (int)d.size(), skip = n > HIST_DAYS ? n - HIST_DAYS : 0;
        int base = HIST_DAYS - (n - skip), i = 0;
        for (JsonVariantConst v : d) {          // right-aligned: last = today
            if (i >= skip) m->hist[base + i - skip] = v | 0;
            i++;
        }
        m->have_hist = true;
        return M_HIST;
    }
    if (!strcmp(kind, "models")) {
        m->n_models = read_rows(doc["rows"].as<JsonArrayConst>(), m->models);
        m->models_total = doc["rq"] | 0;
        m->have_models = true;
        return M_MODELS;
    }
    if (!strcmp(kind, "prov")) {
        m->n_prov = read_rows(doc["rows"].as<JsonArrayConst>(), m->prov);
        m->prov_total = doc["rq"] | 0;
        m->prov_count = doc["n"] | m->n_prov;
        m->have_prov = true;
        return M_PROV;
    }
    if (!strcmp(kind, "tok")) {
        m->tok_prompt     = doc["p"]  | (int64_t)0;
        m->tok_completion = doc["c"]  | (int64_t)0;
        m->tok_reasoning  = doc["r"]  | (int64_t)0;
        m->tok_cached     = doc["ca"] | (int64_t)0;
        m->tok_requests   = doc["rq"] | 0;
        m->tok_free_req   = doc["fr"] | 0;
        m->tok_cents      = doc["usd"] | 0;
        m->have_tok = true;
        return M_TOK;
    }
    if (!strcmp(kind, "keys")) {
        int n = 0;
        for (JsonArrayConst r : doc["rows"].as<JsonArrayConst>()) {
            if (n >= MAX_KEYS) break;
            KeyRow* k = &m->keys[n++];
            copy_name(k->name, sizeof(k->name), r[0] | "");
            k->day = r[1] | 0;  k->week = r[2] | 0;  k->month = r[3] | 0;
            k->limit = r[4] | -1;  k->remaining = r[5] | -1;
            k->disabled = (r[6] | 0) != 0;
        }
        m->n_keys = n;
        m->have_keys = true;
        return M_KEYS;
    }
    if (!strcmp(kind, "cfg")) {
        m->alert_low_bal    = doc["low"] | 0;
        m->alert_day_budget = doc["bud"] | 0;
        m->screens_mask     = (uint8_t)(doc["scr"] | 0);
        m->dim_after_min    = (uint8_t)(doc["dim"] | 0);
        return M_CFG;
    }
    if (!strcmp(kind, "err")) {
        m->key_error = !strcmp(doc["code"] | "", "key");
        return M_ERR;
    }
    return 0;
}
