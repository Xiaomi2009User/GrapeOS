#include "apps.h"
#include "console.h"
#include "guess.h"
#include "notes.h"
#include "snake.h"
#include "store.h"

enum active_app { APP_NONE, APP_SNAKE, APP_GUESS };
static enum active_app active;

struct catalog_entry {
    const char *name;
    const char *description;
    u32 bit;
};

static const struct catalog_entry catalog[] = {
    {"snake", "Classic snake with a saved high score", STORE_APP_SNAKE},
    {"notes", "Save and read a personal note", STORE_APP_NOTES},
    {"guess", "Guess a number from 1 to 100", STORE_APP_GUESS}
};

#define CATALOG_COUNT (sizeof(catalog) / sizeof(catalog[0]))

static bool equal(const char *left, const char *right)
{
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static char *skip_space(char *text)
{
    while (*text == ' ' || *text == '\t')
        ++text;
    return text;
}

static char *token(char **remaining)
{
    char *start = skip_space(*remaining);
    char *end = start;
    while (*end && *end != ' ' && *end != '\t')
        ++end;
    if (*end)
        *end++ = '\0';
    *remaining = end;
    return *start ? start : NULL;
}

static const struct catalog_entry *find_app(const char *name)
{
    for (u32 index = 0; index < CATALOG_COUNT; ++index)
        if (equal(catalog[index].name, name))
            return &catalog[index];
    return NULL;
}

static void market_help(void)
{
    console_write("GrapeMarket: local app catalog\n"
                  "  grapemarket -L          List the catalog\n"
                  "  grapemarket -I <app>    Install an app\n"
                  "  grapemarket -R <app>    Remove an app\n"
                  "  grapemarket -S          List installed apps\n"
                  "  grapemarket --help      Show this help\n"
                  "Launch an installed app by typing its name.\n");
}

static void list_catalog(void)
{
    u32 installed = store_get()->installed_apps;
    console_write("Local GrapeMarket catalog:\n");
    for (u32 index = 0; index < CATALOG_COUNT; ++index) {
        console_write(installed & catalog[index].bit ? "  [installed] " : "  [available] ");
        console_write(catalog[index].name);
        console_write(" - ");
        console_write(catalog[index].description);
        console_putc('\n');
    }
    console_write("Use grapemarket -I <app> to install.\n");
}

void apps_list(void)
{
    u32 installed = store_get()->installed_apps;
    bool found = false;
    console_write("Installed apps:\n");
    for (u32 index = 0; index < CATALOG_COUNT; ++index) {
        if (installed & catalog[index].bit) {
            console_write("  ");
            console_write(catalog[index].name);
            console_putc('\n');
            found = true;
        }
    }
    if (!found)
        console_write("  None. Use grapemarket -L to browse.\n");
}

void apps_market(char *arguments)
{
    char *option = token(&arguments);
    char *name = token(&arguments);
    if (!option) {
        list_catalog();
        return;
    }
    if (equal(option, "-L") || equal(option, "-S") || equal(option, "--help")) {
        if (name) {
            console_write("grapemarket: unexpected arguments.\n");
            return;
        }
        if (equal(option, "-L"))
            list_catalog();
        else if (equal(option, "-S"))
            apps_list();
        else
            market_help();
        return;
    }
    bool install = equal(option, "-I");
    if ((!install && !equal(option, "-R")) || !name || token(&arguments)) {
        console_write("grapemarket: expected -L, -S, -I <app>, -R <app>, or --help.\n");
        return;
    }
    const struct catalog_entry *entry = find_app(name);
    if (!entry) {
        console_write("grapemarket: unknown app '");
        console_write(name);
        console_write("'.\n");
        return;
    }
    struct store_state candidate = *store_get();
    bool installed = (candidate.installed_apps & entry->bit) != 0;
    if (install == installed) {
        console_write(entry->name);
        console_write(install ? " is already installed.\n" : " is not installed.\n");
        return;
    }
    if (install)
        candidate.installed_apps |= entry->bit;
    else
        candidate.installed_apps &= ~entry->bit;
    bool saved = store_save(&candidate);
    if (!saved && store_available()) {
        console_write("grapemarket: save failed; installed apps were not changed.\n");
        return;
    }
    console_write(install ? "Installed " : "Removed ");
    console_write(entry->name);
    console_write(install ? " from local catalog." : " from installed apps.");
    if (!saved)
        console_write(" Change is for this session only (storage unavailable).");
    console_putc('\n');
}

bool apps_launch(const char *name, char *arguments)
{
    const struct catalog_entry *entry = find_app(name);
    if (!entry)
        return false;
    if (!(store_get()->installed_apps & entry->bit)) {
        console_write(name);
        console_write(": not installed. Run grapemarket -I ");
        console_write(name);
        console_write(".\n");
        return true;
    }
    if (entry->bit == STORE_APP_SNAKE) {
        if (snake_start(arguments))
            active = APP_SNAKE;
    } else if (entry->bit == STORE_APP_GUESS) {
        if (guess_start(arguments))
            active = APP_GUESS;
    } else {
        notes_command(arguments);
    }
    return true;
}

void apps_input(int key)
{
    if (active == APP_SNAKE)
        snake_input(key);
    else if (active == APP_GUESS)
        guess_input(key);
    if (!apps_active())
        active = APP_NONE;
}

void apps_tick(void)
{
    if (active == APP_SNAKE)
        snake_tick();
}

bool apps_active(void)
{
    return (active == APP_SNAKE && snake_active())
        || (active == APP_GUESS && guess_active());
}
