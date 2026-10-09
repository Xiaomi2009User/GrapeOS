#include "console.h"
#include "setup.h"
#include "store.h"

#define SETUP_LINE_CAPACITY 128u

static struct store_state candidate;
static bool running;
static bool initial_setup;
static u32 step;
static char input[SETUP_LINE_CAPACITY];
static u32 input_length;
static u32 excess;

static bool letter(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

static bool digit(char character)
{
    return character >= '0' && character <= '9';
}

bool setup_valid_username(const char *name)
{
    if (!letter(*name))
        return false;
    u32 length = 0;
    while (name[length]) {
        char character = name[length];
        if (++length > 23 || (!letter(character) && !digit(character)
                             && character != '_' && character != '-'))
            return false;
    }
    return true;
}

bool setup_valid_hostname(const char *name)
{
    if (!*name || *name == '-')
        return false;
    u32 length = 0;
    while (name[length]) {
        char character = name[length];
        if (++length > 23 || (!letter(character) && !digit(character)
                             && character != '-'))
            return false;
    }
    return name[length - 1] != '-';
}

static void copy_name(char destination[24], const char *source)
{
    u32 index = 0;
    while (source[index] && index < 23) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static const char *theme_name(void)
{
    if (candidate.background == 0 && candidate.foreground == 13) return "Purple";
    if (candidate.background == 0 && candidate.foreground == 10) return "Green";
    if (candidate.background == 0 && candidate.foreground == 7) return "Classic";
    return "Custom";
}

static void reset_input(void)
{
    input_length = excess = 0;
    input[0] = '\0';
}

static void field_prompt(void)
{
    console_color(15, 0);
    if (step == 0) {
        console_write("Username [");
        console_write(candidate.username);
        console_write("]: ");
    } else if (step == 1) {
        console_write("Computer name [");
        console_write(candidate.hostname);
        console_write("]: ");
    } else if (step == 2) {
        console_write("Theme [1 purple / 2 green / 3 classic]: ");
    } else {
        console_write("Save setup? [Y/n]: ");
    }
}

static void screen(void)
{
    console_cursor_visible(true);
    console_color(7, 0);
    console_clear();
    console_color(13, 0);
    console_write("\n  G R A P E O S\n");
    console_write("  ===============================================\n");
    console_color(15, 0);
    console_write("  Welcome to GrapeOS\n");
    console_color(7, 0);
    console_write("  Let's make this Grape yours.\n");
    console_write("  Your apps are local. No internet is needed.\n\n");
    console_color(13, 0);
    console_write("  Step ");
    console_u32(step + 1);
    console_write(" of 4 - ");
    static const char *const headings[] = {
        "Your name", "This computer", "Choose a theme", "Ready to go"
    };
    console_write(headings[step]);
    console_color(7, 0);
    console_write("\n\n");
    if (step == 0) {
        console_write("  Letters, digits, _ and -; start with a letter.\n");
        console_write("  Up to 23 characters. Enter keeps the suggested name.\n\n");
    } else if (step == 1) {
        console_write("  Give this computer a name (up to 23 characters).\n");
        console_write("  Letters, digits and -; no - at the beginning or end.\n\n");
    } else if (step == 2) {
        console_write("  1  Purple - the original Grape look\n");
        console_write("  2  Green  - a classic terminal feeling\n");
        console_write("  3  Classic - light gray on black\n\n  Current: ");
        console_write(theme_name());
        console_write(". Enter keeps it.\n\n");
    } else {
        console_write("  User:     ");
        console_write(candidate.username);
        console_write("\n  Computer: ");
        console_write(candidate.hostname);
        console_write("\n  Theme:    ");
        console_write(theme_name());
        console_write("\n\n  Next: Grapemarket -I snake, then snake.\n");
        console_write("  N goes back to the beginning.\n\n");
    }
    field_prompt();
    console_write(input);
}

void setup_begin(bool first_run)
{
    candidate = *store_get();
    if (first_run) {
        candidate.foreground = 13;
        candidate.background = 0;
    }
    initial_setup = first_run;
    running = true;
    step = 0;
    reset_input();
    screen();
}

bool setup_active(void)
{
    return running;
}

static void submit(void)
{
    console_putc('\n');
    if (excess) {
        console_write("Input too long. Please try again.\n");
        reset_input();
        field_prompt();
        return;
    }
    if (step == 0 && *input) {
        if (!setup_valid_username(input)) {
            console_write("Use 1..23 letters, digits, _ or -; start with a letter.\n");
            reset_input();
            field_prompt();
            return;
        }
        copy_name(candidate.username, input);
    } else if (step == 1 && *input) {
        if (!setup_valid_hostname(input)) {
            console_write("Use 1..23 letters, digits or -; no leading or trailing -.\n");
            reset_input();
            field_prompt();
            return;
        }
        copy_name(candidate.hostname, input);
    } else if (step == 2 && *input) {
        if (input[1] || input[0] < '1' || input[0] > '3') {
            console_write("Choose 1, 2 or 3.\n");
            reset_input();
            field_prompt();
            return;
        }
        const u8 themes[] = {13, 10, 7};
        candidate.foreground = themes[input[0] - '1'];
        candidate.background = 0;
    } else if (step == 3) {
        if ((input[0] == 'n' || input[0] == 'N') && !input[1]) {
            step = 0;
            reset_input();
            screen();
            return;
        }
        if (*input && ((input[0] != 'y' && input[0] != 'Y') || input[1])) {
            console_write("Enter Y to save or N to start again.\n");
            reset_input();
            field_prompt();
            return;
        }
        candidate.configured = 1;
        if (!store_save(&candidate) && store_available()) {
            console_write("Could not save setup. Your previous settings were kept.\n");
            reset_input();
            field_prompt();
            return;
        }
        running = false;
        return;
    }
    ++step;
    reset_input();
    screen();
}

void setup_input(int key)
{
    if (!running)
        return;
    if (key == '\n' || key == '\r') {
        submit();
    } else if (key == '\b' || key == 127) {
        if (excess) {
            --excess;
        } else if (input_length) {
            input[--input_length] = '\0';
            console_putc('\b');
        }
    } else if (key == 0x15) {
        while (input_length) {
            --input_length;
            console_putc('\b');
        }
        reset_input();
    } else if (key == '\f') {
        screen();
    } else if (key == 3 || key == 0x1b) {
        if (initial_setup) {
            candidate = *store_get();
            candidate.foreground = 13;
            candidate.background = 0;
            step = 0;
            reset_input();
            screen();
        } else {
            running = false;
        }
    } else if (key >= 32 && key < 127) {
        if (input_length == SETUP_LINE_CAPACITY - 1) {
            if (excess != 0xffffffffu)
                ++excess;
        } else {
            input[input_length++] = (char)key;
            input[input_length] = '\0';
            console_putc((char)key);
        }
    }
}
