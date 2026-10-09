#ifndef GRAPE_SNAKE_H
#define GRAPE_SNAKE_H

#include "types.h"

bool snake_start(char *arguments);
void snake_input(int key);
void snake_tick(void);
bool snake_active(void);

#endif
