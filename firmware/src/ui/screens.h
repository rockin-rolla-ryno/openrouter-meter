// Screen contract. Each screen owns one full-size group; ui.cpp shows one
// at a time, calls enter() when it becomes visible and frame() at ~30 fps
// while visible. apply() runs for every message, visible or not.
#pragma once
#include "common.h"

struct Screen {
    const char* title;                         // header section label
    void (*build)(lv_obj_t* group);
    void (*apply)(const Model& m, uint16_t parts, bool visible);
    void (*enter)(void);
    void (*frame)(uint32_t now);
    bool (*has_data)(const Model& m);          // false = show "no data yet"
};

extern const Screen SCREEN_CREDITS;
extern const Screen SCREEN_ROUTES;
extern const Screen SCREEN_SPEND;
extern const Screen SCREEN_TOKENS;
extern const Screen SCREEN_PROVIDERS;
extern const Screen SCREEN_KEYS;

// Spend events detected by the Credits screen (used for the burst + alerts).
void credits_note_spend(int32_t cents);
