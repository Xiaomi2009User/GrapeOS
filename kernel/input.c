#include "console.h"
#include "input.h"
#include "platform.h"

static u8 escape_state;
static u32 escape_started;
static bool serial_cr;
static int pending = -1;

void input_discard(void)
{
    /* Both queues are bounded; do not let continuous input stall boot. */
    for (u32 i = 0; i < 128 && keyboard_poll() >= 0; ++i) { }
    for (u32 i = 0; i < 128 && serial_poll() >= 0; ++i) { }
    escape_state = 0;
    serial_cr = false;
    pending = -1;
}

int input_poll(void)
{
    if (pending >= 0) {
        int key = pending;
        pending = -1;
        return key;
    }
    int keyboard = keyboard_poll();
    if (keyboard >= 0) {
        escape_state = 0;
        serial_cr = false;
        return keyboard;
    }
    /* UART escape sequences may arrive over several timer interrupts. */
    for (u32 attempt = 0; attempt < 8; ++attempt) {
        int key = serial_poll();
        if (key < 0)
            break;
        if (key == '\n' && serial_cr) {
            serial_cr = false;
            continue;
        }
        serial_cr = key == '\r';
        if (key == '\r')
            key = '\n';
        if (escape_state == 1) {
            escape_state = key == '[' || key == 'O' ? 2 : 0;
            if (escape_state)
                continue;
            pending = key;
            return 0x1b;
        }
        if (escape_state == 2) {
            escape_state = 0;
            if (key == 'A') return KEY_UP;
            if (key == 'B') return KEY_DOWN;
            if (key == 'C') return KEY_RIGHT;
            if (key == 'D') return KEY_LEFT;
            /* Ignore the tail of unsupported terminal control sequences. */
            if (key >= '0' && key <= '9') {
                escape_state = 3;
                continue;
            }
            continue;
        }
        if (escape_state == 3) {
            if (key >= '@' && key <= '~')
                escape_state = 0;
            continue;
        }
        if (key == 0x1b) {
            escape_state = 1;
            escape_started = timer_ticks();
            continue;
        }
        return key;
    }
    if (escape_state && (u32)(timer_ticks() - escape_started) >= 3u) {
        escape_state = 0;
        return 0x1b;
    }
    return -1;
}
