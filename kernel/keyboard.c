#include "io.h"
#include "platform.h"
#include "mouse.h"

#define QUEUE_SIZE 128
#define PS2_TIMEOUT 100000

static volatile u8 queue[QUEUE_SIZE];
static volatile u32 queue_head;
static volatile u32 queue_tail;
static bool left_shift, right_shift, left_control, right_control, caps_lock;
static bool extended;
static u8 pause_remaining;

static const u8 unshifted[128] = {
    [0x01] = 0x1b,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = '\n', [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x',
    [0x2e] = 'c', [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x37] = '*', [0x39] = ' ', [0x4a] = '-', [0x4e] = '+'
};

static const u8 shifted[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
    [0x1a] = '{', [0x1b] = '}', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2b] = '|', [0x33] = '<', [0x34] = '>',
    [0x35] = '?'
};

static bool wait_write(void)
{
    u32 remaining = PS2_TIMEOUT;
    while (remaining--) {
        if (!(inb(0x64) & 2))
            return true;
        io_wait();
    }
    return false;
}

static int wait_read(void)
{
    u32 remaining = PS2_TIMEOUT;
    while (remaining--) {
        u8 status = inb(0x64);
        if (status & 1) {
            u8 value = inb(0x60);
            if (!(status & 0xe0))
                return value;
        }
        io_wait();
    }
    return -1;
}

static bool controller_command(u8 command)
{
    if (!wait_write())
        return false;
    outb(0x64, command);
    return true;
}

static bool keyboard_command(u8 command)
{
    u32 attempts;
    for (attempts = 0; attempts < 3; ++attempts) {
        int response;
        if (!wait_write())
            return false;
        outb(0x60, command);
        response = wait_read();
        if (response == 0xfa)
            return true;
        if (response != 0xfe)
            return false;
    }
    return false;
}

void keyboard_init(void)
{
    u32 drained;
    int config;
    queue_head = queue_tail = 0;
    left_shift = right_shift = left_control = right_control = caps_lock = false;
    extended = false;
    pause_remaining = 0;
    controller_command(0xad);
    controller_command(0xa7);
    for (drained = 0; drained < 32 && (inb(0x64) & 1); ++drained)
        (void)inb(0x60);
    if (controller_command(0x20)) {
        config = wait_read();
        if (config >= 0 && controller_command(0x60) && wait_write()) {
            /* Enable IRQ1 and set-2 to set-1 translation; disable mouse IRQ. */
            outb(0x60, (u8)((config | 0x41) & ~0x12));
        }
    }
    controller_command(0xae);
    if (keyboard_command(0xf5) && keyboard_command(0xf0))
        keyboard_command(0x02);
    keyboard_command(0xf4);
}

static bool mouse_command(u8 value)
{
    if (!controller_command(0xd4) || !wait_write()) return false;
    outb(0x60, value);
    for (u32 retry = 0; retry < PS2_TIMEOUT; ++retry) {
        u8 status = inb(0x64);
        if (status & 1) {
            u8 response = inb(0x60);
            if ((status & 0xe0) == 0x20) return response == 0xfa;
        }
        io_wait();
    }
    return false;
}

bool ps2_mouse_init(void)
{
    if (!controller_command(0xa8)) return false;
    bool ready = mouse_command(0xf6) && mouse_command(0xf4);
    if (!ready) { controller_command(0xa7); return false; }
    if (!controller_command(0x20)) return false;
    int config = wait_read();
    if (config < 0 || !controller_command(0x60) || !wait_write()) return false;
    outb(0x60, (u8)((config | 2) & ~0x20u));
    return true;
}

static void enqueue(u8 character)
{
    u32 next = (queue_head + 1) & (QUEUE_SIZE - 1);
    if (next == queue_tail)
        return;
    queue[queue_head] = character;
    queue_head = next;
}

static void decode(u8 code)
{
    bool released;
    bool was_extended;
    u8 character;
    if (pause_remaining) {
        --pause_remaining;
        return;
    }
    if (code == 0xe1) {
        pause_remaining = 5;
        extended = false;
        return;
    }
    if (code == 0xe0) {
        extended = true;
        return;
    }
    was_extended = extended;
    extended = false;
    released = (code & 0x80) != 0;
    code &= 0x7f;
    if (code == 0x1d) {
        if (was_extended)
            right_control = !released;
        else
            left_control = !released;
        return;
    }
    if (!was_extended && code == 0x2a) {
        left_shift = !released;
        return;
    }
    if (!was_extended && code == 0x36) {
        right_shift = !released;
        return;
    }
    if (released)
        return;
    if (!was_extended && code == 0x42) { enqueue(KEY_POINTER); return; }
    if (!was_extended && code == 0x3a) {
        caps_lock = !caps_lock;
        return;
    }
    if (was_extended) {
        if (code == 0x48)
            enqueue(KEY_UP);
        else if (code == 0x50)
            enqueue(KEY_DOWN);
        else if (code == 0x4b)
            enqueue(KEY_LEFT);
        else if (code == 0x4d)
            enqueue(KEY_RIGHT);
        else if (code == 0x1c)
            enqueue('\n');
        else if (code == 0x35)
            enqueue('/');
        else if (code == 0x47) enqueue(KEY_HOME);
        else if (code == 0x4f) enqueue(KEY_END);
        else if (code == 0x53) enqueue(KEY_DELETE);
        return;
    }
    character = unshifted[code];
    if (!character)
        return;
    if (character >= 'a' && character <= 'z') {
        if (left_control || right_control)
            character = character - 'a' + 1;
        else if ((left_shift || right_shift) != caps_lock)
            character -= 'a' - 'A';
    } else if ((left_shift || right_shift) && shifted[code]) {
        character = shifted[code];
    }
    enqueue(character);
}

void keyboard_handle_irq(void)
{
    u32 remaining = 32;
    while (remaining--) {
        u8 status = inb(0x64);
        u8 code;
        if (!(status & 1))
            break;
        code = inb(0x60);
        if (status & 0xc0)
            continue;
        if (status & 0x20) { mouse_byte(code); continue; }
        if (code != 0xfa && code != 0xfe)
            decode(code);
    }
}

int keyboard_poll(void)
{
    u8 character;
    if (platform_polling()) keyboard_handle_irq();
    if (queue_head == queue_tail)
        return -1;
    character = queue[queue_tail];
    queue_tail = (queue_tail + 1) & (QUEUE_SIZE - 1);
    return character;
}
