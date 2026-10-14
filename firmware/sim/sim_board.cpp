// Simulator board: an SDL2 window stands in for the 480x480 AMOLED.
//
//   mouse click      touch               right / left arrow   BOOT / KEY button
//   p                PWR short press     P (shift+p, hold)    PWR hold (pairing gesture)
//   d                toggle Bluetooth link                     space   pause scenario
//   s                screenshot (BMP)    esc                  quit
//
// Headless (CI / previews):
//   SDL_VIDEODRIVER=dummy
//   SIM_SHOT=<path> SIM_SHOT_MS=<ms>        one screenshot, then exit
//   SIM_FRAMES=<dir> SIM_FRAME_MS=<ms>      dump a frame sequence
//   SIM_TAPS=3000,6000                      scripted taps (ms)
//   SIM_EXIT_MS=<ms>                        quit after this time
#include "../src/board/board.h"
#include "sim.h"
#include <Arduino.h>
#include <SDL.h>
#include <stdlib.h>
#include <vector>

SimSerial Serial;

static SDL_Window*   win;
static SDL_Renderer* ren;
static SDL_Texture*  tex;
static uint16_t      fb[SCREEN_W * SCREEN_H];
static uint8_t       brightness = 255;
static bool          dirty = true, quit = false;
static uint32_t      t0 = 0;

static bool mouse_down = false;
static Button btn_edge = BTN_NONE;
static std::vector<PowerKeyEvent> pkeys;
static bool pwr_holding = false;
static uint32_t pwr_down_ms = 0;
static bool pwr_hold_sent = false;
static std::vector<long> taps;
static int  battery = 87;
static bool charging = false;

uint32_t millis(void) { return SDL_GetTicks() - t0; }
void delay(uint32_t ms) { SDL_Delay(ms); }

static void present(void) {
    SDL_UpdateTexture(tex, NULL, fb, SCREEN_W * 2);
    SDL_SetTextureColorMod(tex, brightness, brightness, brightness);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    dirty = false;
}

void sim_screenshot(const char* path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(fb, SCREEN_W, SCREEN_H, 16, SCREEN_W * 2,
                                                        SDL_PIXELFORMAT_RGB565);
    if (s) { SDL_SaveBMP(s, path); SDL_FreeSurface(s); }
}

bool sim_should_quit(void) { return quit; }

void sim_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_MOUSEBUTTONDOWN) mouse_down = true;
        if (e.type == SDL_MOUSEBUTTONUP) mouse_down = false;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;
            bool shift = e.key.keysym.mod & KMOD_SHIFT;
            if (k == SDLK_ESCAPE) quit = true;
            else if (k == SDLK_RIGHT) btn_edge = BTN_BOOT;
            else if (k == SDLK_LEFT) btn_edge = BTN_KEY;
            else if (k == SDLK_p && !shift) pkeys.push_back(PKEY_SHORT);
            else if (k == SDLK_p && shift) { pwr_holding = true; pwr_down_ms = millis(); pwr_hold_sent = false; }
            else if (k == SDLK_d) sim_toggle_link();
            else if (k == SDLK_SPACE) sim_toggle_pause();
            else if (k == SDLK_c) charging = !charging;
            else if (k == SDLK_s) { sim_screenshot("sim-shot.bmp"); puts("saved sim-shot.bmp"); }
        }
        if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_p && pwr_holding) {
            pwr_holding = false;
            pkeys.push_back(PKEY_RELEASE);
        }
    }
    if (pwr_holding && !pwr_hold_sent && millis() - pwr_down_ms > 1500) {
        pwr_hold_sent = true;
        pkeys.push_back(PKEY_HOLD);
    }

    uint32_t now = millis();
    static long shot_ms = -2, exit_ms = -2;
    static const char* frames = nullptr;
    static uint32_t frame_ms = 40, next_frame = 0;
    static int frame_n = 0;
    if (shot_ms == -2) {
        const char* v = getenv("SIM_SHOT_MS"); shot_ms = v ? atol(v) : -1;
        v = getenv("SIM_EXIT_MS"); exit_ms = v ? atol(v) : -1;
        frames = getenv("SIM_FRAMES");
        v = getenv("SIM_FRAME_MS"); if (v) frame_ms = (uint32_t)atol(v);
        v = getenv("SIM_FRAMES_FROM"); if (v) next_frame = (uint32_t)atol(v);
    }
    if (frames && now >= next_frame) {
        char p[512];
        snprintf(p, sizeof(p), "%s/f%05d.bmp", frames, frame_n++);
        sim_screenshot(p);
        next_frame += frame_ms;
    }
    if (shot_ms >= 0 && now >= (uint32_t)shot_ms) {
        const char* p = getenv("SIM_SHOT");
        sim_screenshot(p ? p : "sim-shot.bmp");
        quit = true;
    }
    if (exit_ms >= 0 && now >= (uint32_t)exit_ms) quit = true;
    if (dirty) present();
}

void board_begin(void) {
    SDL_Init(SDL_INIT_VIDEO);
    t0 = SDL_GetTicks();
    win = SDL_CreateWindow("OpenRouter Meter (simulator)", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, 0);
    ren = SDL_CreateRenderer(win, -1, 0);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    const char* v = getenv("SIM_TAPS");
    while (v && *v) { taps.push_back(atol(v)); v = strchr(v, ','); if (v) v++; }
}

void board_tick(void) {}

void display_flush(int x, int y, int w, int h, const uint16_t* px) {
    for (int r = 0; r < h; r++) {
        if (y + r < 0 || y + r >= SCREEN_H) continue;
        memcpy(&fb[(y + r) * SCREEN_W + x], &px[r * w], (size_t)w * 2);
    }
    dirty = true;
}

void display_round_area(int32_t*, int32_t*, int32_t*, int32_t*) {}
void display_set_brightness(uint8_t level) { brightness = level; dirty = true; }
bool display_orientation_changed(void) { return false; }

bool touch_read(uint16_t* x, uint16_t* y) {
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    *x = (uint16_t)(mx < 0 ? 0 : mx);
    *y = (uint16_t)(my < 0 ? 0 : my);
    uint32_t now = millis();
    for (long t : taps) if (now >= (uint32_t)t && now < (uint32_t)t + 80) { *x = 240; *y = 240; return true; }
    return mouse_down;
}

Button button_pressed(void) { Button b = btn_edge; btn_edge = BTN_NONE; return b; }

PowerKeyEvent power_key_event(void) {
    if (pkeys.empty()) return PKEY_NONE;
    PowerKeyEvent e = pkeys.front();
    pkeys.erase(pkeys.begin());
    return e;
}

BatteryInfo battery_info(void) { return BatteryInfo{ battery, charging, charging }; }
uint32_t board_free_heap(void) { return 0; }
