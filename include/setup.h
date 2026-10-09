#ifndef GRAPEOS_SETUP_H
#define GRAPEOS_SETUP_H

#include "types.h"

void setup_begin(bool first_run);
void setup_input(int key);
bool setup_active(void);
bool setup_valid_username(const char *name);
bool setup_valid_hostname(const char *name);

#endif
