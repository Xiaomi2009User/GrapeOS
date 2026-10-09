#ifndef GRAPE_APPS_H
#define GRAPE_APPS_H

#include "types.h"

void apps_market(char *arguments);
void apps_list(void);
bool apps_launch(const char *name, char *arguments);
void apps_input(int key);
void apps_tick(void);
bool apps_active(void);

#endif
