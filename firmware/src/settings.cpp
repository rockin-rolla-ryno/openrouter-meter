#include "settings.h"

#ifndef SIMULATOR
#include <Preferences.h>
static Preferences prefs;
void settings_begin(void) { prefs.begin("ormeter", false); }
uint8_t settings_load_brightness(void) { return prefs.getUChar("bright", 0); }
void settings_save_brightness(uint8_t v) { prefs.putUChar("bright", v); }
#else
static uint8_t bright = 0;
void settings_begin(void) {}
uint8_t settings_load_brightness(void) { return bright; }
void settings_save_brightness(uint8_t v) { bright = v; }
#endif
