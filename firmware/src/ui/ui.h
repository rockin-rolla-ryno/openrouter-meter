#pragma once
#include "../model.h"
#include "../board/board.h"
#include "../link/link.h"

void ui_begin(void);
void ui_apply(const Model& m, uint16_t parts);   // after every received message
void ui_frame(void);                              // every loop iteration
void ui_next(void);
void ui_prev(void);
void ui_set_link(LinkState s, bool bonded);
void ui_set_battery(BatteryInfo b);
void ui_set_pair_prompt(bool armed);             // PWR held: "release to pair"
bool ui_take_wake_request(void);                  // an alert wants the panel bright
bool ui_take_tap(void);                           // a tap happened (for idle tracking)
