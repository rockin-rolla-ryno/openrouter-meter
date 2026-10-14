// Minimal Arduino API for the desktop simulator.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

uint32_t millis(void);
void     delay(uint32_t ms);

struct SimSerial {
    void begin(unsigned long) {}
    void println(const char* s) { puts(s); }
    int printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        fflush(stdout);
        return n;
    }
};
extern SimSerial Serial;
