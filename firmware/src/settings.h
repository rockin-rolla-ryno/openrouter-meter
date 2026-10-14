// Small persistent settings stored on the device (flash / NVS).
#pragma once
#include <stdint.h>

void    settings_begin(void);
uint8_t settings_load_brightness(void);
void    settings_save_brightness(uint8_t level_index);
