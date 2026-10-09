#ifndef GRAPEOS_MEMORY_H
#define GRAPEOS_MEMORY_H

#include "types.h"

void *memcpy(void *destination, const void *source, usize count);
void *memmove(void *destination, const void *source, usize count);
void *memset(void *destination, int value, usize count);
int memcmp(const void *left, const void *right, usize count);

#endif
