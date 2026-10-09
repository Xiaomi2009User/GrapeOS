#ifndef GRAPE_PLATFORM_H
#define GRAPE_PLATFORM_H

#include "types.h"

enum {
    KEY_UP = 0x80,
    KEY_DOWN = 0x81,
    KEY_LEFT = 0x82,
    KEY_RIGHT = 0x83,
    KEY_HOME = 0x84,
    KEY_END = 0x85,
    KEY_DELETE = 0x86,
    KEY_POINTER = 0x87,
    TIMER_HZ = 100
};

void platform_init(void);
u32 timer_ticks(void);
int keyboard_poll(void);
bool platform_polling(void);
void platform_idle(void);
const char *platform_clock_source(void);
u32 platform_clock_khz(void);

#endif
