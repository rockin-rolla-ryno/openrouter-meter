// Bluetooth LE link to the OpenRouter Meter app on the PC.
//
// GATT service 589b0001-eb3f-4774-98f5-45672a450cf3
//   589b0002-…  DATA     write (encrypted): one JSON message per write
//   589b0003-…  REQUEST  notify: device asks the app to poll now ("r")
// plus the standard Battery Service (0x180F) so Windows shows the charge.
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum LinkState : uint8_t {
    LINK_ADVERTISING,   // waiting for the paired PC (or a new pairing)
    LINK_CONNECTED,     // the app is connected
};

void        link_begin(void);
void        link_tick(void);
LinkState   link_state(void);
bool        link_has_bond(void);          // paired with a PC at least once
void        link_forget_pairings(void);   // clear bonds and advertise for a new PC
bool        link_next_message(char* out, int cap);   // pop one received message
void        link_request_refresh(void);   // ask the app for fresh data
void        link_set_battery(int percent);
