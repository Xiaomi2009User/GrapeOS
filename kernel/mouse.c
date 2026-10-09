#include "mouse.h"
static volatile i32 x = 320, y = 240;
static volatile u8 buttons;
static u8 packet[3], position;
extern bool ps2_mouse_init(void);
static bool available;
bool mouse_init(void) { available = ps2_mouse_init(); return available; }
void mouse_byte(u8 byte)
{
    if (!available || (!position && !(byte & 8))) return;
    packet[position++] = byte;
    if (position != 3) return;
    position = 0;
    if (packet[0] & 0xc0) return;
    i32 dx = (i32)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    i32 dy = (i32)packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    x += dx; y -= dy;
    if (x < 0) x = 0;
    if (x > 639) x = 639;
    if (y < 0) y = 0;
    if (y > 479) y = 479;
    buttons = packet[0] & 7;
}
void mouse_poll(struct mouse_state *state)
{
    u32 flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    state->x = x; state->y = y; state->buttons = buttons; state->available = available;
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}
