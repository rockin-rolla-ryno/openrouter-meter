// Simulator link: replays a JSONL scenario instead of Bluetooth.
// Each line is one protocol message plus optional "after_ms" (delay before
// it is delivered, default 0). Lines starting with # are comments.
// SIM_SCENARIO=<path> overrides the default sim/scenario.jsonl.
#include "../src/link/link.h"
#include "sim.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>

struct Step { uint32_t at; std::string json; };
static std::vector<Step> steps;
static size_t next_step = 0;
static bool connected = true, paused = false;
static uint32_t pause_offset = 0, paused_at = 0;

void link_begin(void) {
    const char* path = getenv("SIM_SCENARIO");
    const char* tries[] = { path, "sim/scenario.jsonl", "scenario.jsonl", "../sim/scenario.jsonl" };
    FILE* f = nullptr;
    for (const char* t : tries) if (t && (f = fopen(t, "r"))) break;
    if (!f) { puts("[sim] no scenario file"); return; }
    char line[2048];
    uint32_t at = 0;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        JsonDocument d;
        if (!deserializeJson(d, s)) at += (uint32_t)(d["after_ms"] | 0);
        steps.push_back({ at, s });
    }
    fclose(f);
    printf("[sim] %zu scenario messages\n", steps.size());
}

void link_tick(void) {}
LinkState link_state(void) { return connected ? LINK_CONNECTED : LINK_ADVERTISING; }
bool link_has_bond(void) { return getenv("SIM_UNPAIRED") == nullptr; }
void link_forget_pairings(void) { puts("[sim] pairings cleared"); connected = false; }
void link_request_refresh(void) {}
void link_set_battery(int) {}

bool link_next_message(char* out, int cap) {
    if (!connected || paused || next_step >= steps.size()) return false;
    uint32_t now = millis() - pause_offset;
    if (now < steps[next_step].at) return false;
    strncpy(out, steps[next_step].json.c_str(), cap - 1);
    out[cap - 1] = 0;
    next_step++;
    return true;
}

void sim_toggle_link(void) { connected = !connected; }
void sim_toggle_pause(void) {
    paused = !paused;
    if (paused) paused_at = millis(); else pause_offset += millis() - paused_at;
}
