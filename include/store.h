#ifndef GRAPE_STORE_H
#define GRAPE_STORE_H

#include "types.h"

#define STORE_APP_SNAKE 1u
#define STORE_APP_NOTES 2u
#define STORE_APP_GUESS 4u

struct store_state {
    u8 configured;
    char username[24];
    char hostname[24];
    u8 foreground;
    u8 background;
    u32 installed_apps;
#ifdef GRAPE_FIRMWARE
    char note[4096];
#else
    char note[256];
#endif
    u32 snake_best;
};

void store_init(void);
const struct store_state *store_get(void);
bool store_available(void);

/* A successful disk save commits RAM after write, flush, and readback.
 * With no disk, valid changes apply to this session and return false.
 * On an available disk's I/O failure, RAM retains the previous state.
 */
bool store_save(const struct store_state *candidate);

#endif
