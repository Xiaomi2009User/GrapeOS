#ifndef GRAPE_MOUSE_H
#define GRAPE_MOUSE_H
#include "types.h"
struct mouse_state { i32 x, y; u8 buttons; bool available; };
bool mouse_init(void);
void mouse_byte(u8 byte);
void mouse_poll(struct mouse_state *state);
#endif
