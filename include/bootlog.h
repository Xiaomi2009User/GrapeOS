#ifndef GRAPEOS_BOOTLOG_H
#define GRAPEOS_BOOTLOG_H

void bootlog_begin(void);
void bootlog_info(const char *message);
void bootlog_ok(const char *message);
void bootlog_warn(const char *message);
void bootlog_finish(void);

#endif
