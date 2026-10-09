#ifndef GRAPEOS_INPUT_H
#define GRAPEOS_INPUT_H

/* ASCII, KEY_* arrows, or -1 when no complete input event is available. */
int input_poll(void);

/* Drop input received while a boot screen is being shown. */
void input_discard(void);

#endif
