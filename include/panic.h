#ifndef GRAPEOS_PANIC_H
#define GRAPEOS_PANIC_H

_Noreturn void kernel_panic(const char *code, const char *reason, const char *trigger);

#endif
