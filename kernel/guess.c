#include "console.h"
#include "guess.h"
#include "platform.h"
#include "store.h"

static bool running;
static bool previous_cr;
static char digits[4];
static u32 length, excess, target, attempts;
static u32 random_state = 0x47524150u;

static void clear_input(void)
{
    while (length) {
        console_putc('\b');
        --length;
    }
    excess = 0;
    digits[0] = '\0';
}

static void new_round(void)
{
    random_state ^= timer_ticks();
    random_state = random_state * 1664525u + 1013904223u;
    target = random_state % 100u + 1u;
    attempts = 0;
    length = excess = 0;
    digits[0] = '\0';
    previous_cr = false;
    console_clear();
    console_write("Guess started\nGuess a number from 1 to 100.\n");
    console_write("Enter submits; R restarts; Q or Escape quits.\n\nGuess> ");
}

bool guess_start(char *arguments)
{
    while (*arguments == ' ' || *arguments == '\t')
        ++arguments;
    if (*arguments) {
        console_write("guess: no arguments expected.\n");
        return false;
    }
    running = true;
    new_round();
    return true;
}

bool guess_active(void)
{
    return running;
}

static void close_game(void)
{
    const struct store_state *profile = store_get();
    running = false;
    console_color(profile->foreground, profile->background);
    console_clear();
    console_write("Guess closed\n");
}

void guess_input(int key)
{
    if (!running)
        return;
    if (key == '\n' && previous_cr) {
        previous_cr = false;
        return;
    }
    previous_cr = key == '\r';
    if (key == 'q' || key == 'Q' || key == 0x1b || key == 3) {
        close_game();
    } else if (key == 'r' || key == 'R') {
        new_round();
    } else if (key == '\b' || key == 127) {
        if (excess)
            --excess;
        else if (length) {
            digits[--length] = '\0';
            console_putc('\b');
        }
    } else if (key == 0x15) {
        clear_input();
    } else if (key >= '0' && key <= '9') {
        if (length == 3) {
            if (excess != 0xffffffffu)
                ++excess;
        } else {
            digits[length++] = (char)key;
            digits[length] = '\0';
            console_putc((char)key);
        }
    } else if (key == '\r' || key == '\n') {
        u32 number = 0;
        for (u32 index = 0; index < length; ++index)
            number = number * 10u + (u32)(digits[index] - '0');
        console_putc('\n');
        if (!length || excess || number < 1 || number > 100) {
            console_write("Enter a number from 1 to 100.\n");
        } else {
            ++attempts;
            if (number < target)
                console_write("Too low.\n");
            else if (number > target)
                console_write("Too high.\n");
            else {
                console_write("Correct! Attempts: ");
                console_u32(attempts);
                console_write(". R restarts; Q quits.\n");
            }
        }
        length = excess = 0;
        digits[0] = '\0';
        console_write("Guess> ");
    }
}
