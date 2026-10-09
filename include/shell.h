#ifndef GRAPEOS_SHELL_H
#define GRAPEOS_SHELL_H

void shell_init(void);
void shell_input(int key);
void shell_resume(void);
void shell_focus(void);
void shell_calculate(char *expression);

#endif
