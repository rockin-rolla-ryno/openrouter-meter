// Account model — everything the screens draw, assembled from the
// messages the OpenRouter Meter app sends over Bluetooth (docs/protocol.md).
// Money is integer US cents; token counts are 64-bit.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define HIST_DAYS   30   // daily spend history, oldest -> today
#define MAX_ROWS     5   // rows on the Routes / Providers screens (incl. "others")
#define MAX_KEYS     4   // API keys on the Keys screen

struct ShareRow {          // one model or provider and its 30-day traffic
    char    name[28];
    int32_t req;
    int32_t cents;
};

struct KeyRow {            // one OpenRouter API key
    char    name[24];
    int32_t day, week, month;  // spend, cents
    int32_t limit;             // spending limit, cents; -1 = none
    int32_t remaining;         // limit remaining, cents; -1 = none
    bool    disabled;
};

struct Model {
    // "core" — every poll (~20 s)
    bool     have_core;
    int32_t  bal, credits, day, month;     // cents
    int32_t  s30;                          // spend over the 30-day window
    int32_t  rq30;                         // requests over completed days
    int32_t  yday, yday_req;               // yesterday (UTC)
    int64_t  yday_tok;
    int64_t  now_local;                    // local wall clock (s); 0 = clock off
    bool     clock24;

    // "hist"
    bool     have_hist;
    int32_t  hist[HIST_DAYS];

    // "models" / "prov"
    bool     have_models, have_prov;
    ShareRow models[MAX_ROWS];  int n_models; int32_t models_total;
    ShareRow prov[MAX_ROWS];    int n_prov;   int32_t prov_total; int prov_count;

    // "tok"
    bool     have_tok;
    int64_t  tok_prompt, tok_completion, tok_reasoning, tok_cached;
    int32_t  tok_requests, tok_free_req, tok_cents;

    // "keys"
    bool     have_keys;
    KeyRow   keys[MAX_KEYS]; int n_keys;

    // "cfg" — user settings from the app
    int32_t  alert_low_bal;     // cents; 0 = off
    int32_t  alert_day_budget;  // cents; 0 = off
    uint8_t  screens_mask;      // bit per screen; 0 = all
    uint8_t  dim_after_min;     // 0 = never

    // "err"
    bool     key_error;         // management key missing / rejected
};

// Which parts of the model a message changed (bitmask).
enum : uint16_t {
    M_CORE   = 1 << 0,
    M_HIST   = 1 << 1,
    M_MODELS = 1 << 2,
    M_PROV   = 1 << 3,
    M_TOK    = 1 << 4,
    M_KEYS   = 1 << 5,
    M_CFG    = 1 << 6,
    M_ERR    = 1 << 7,
};

// Parse one JSON message into the model. Returns the M_* bits touched,
// or 0 if the message is malformed or unknown.
uint16_t protocol_apply(const char* json, Model* m);
