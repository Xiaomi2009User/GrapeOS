#ifndef GRAPE_SYSTEM_H
#define GRAPE_SYSTEM_H

#include "types.h"

/* Caller supplies room for 48 CPUID brand bytes and a terminating NUL. */
void system_cpu_name(char name[49]);
u32 system_kernel_bytes(void);

#endif
