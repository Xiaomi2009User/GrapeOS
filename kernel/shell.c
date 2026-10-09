#include "console.h"
#include "framebuffer.h"
#include "apps.h"
#include "fetch.h"
#include "gui.h"
#include "io.h"
#include "panic.h"
#include "platform.h"
#include "ramfs.h"
#include "system.h"
#include "shell.h"
#include "setup.h"
#include "store.h"
#ifdef GRAPE_FIRMWARE
#include "firmware.h"
#endif

#define LINE_CAPACITY 128u
#define HISTORY_CAPACITY 8u
#define I32_MINIMUM (-2147483647 - 1)

extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

static char line[LINE_CAPACITY];
static u32 line_length;
static u32 excess_characters;
static char history[HISTORY_CAPACITY][LINE_CAPACITY];
static u32 history_count;
static i32 history_position = -1;
static char draft[LINE_CAPACITY];
static u32 draft_excess_characters;

static int text_equal(const char *left, const char *right)
{
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static void text_copy(char *destination, const char *source)
{
    do {
        *destination++ = *source;
    } while (*source++);
}

static int is_space(char character)
{
    return character == ' ' || character == '\t';
}

static char *skip_space(char *text)
{
    while (is_space(*text))
        ++text;
    return text;
}

static char *next_token(char **remaining)
{
    char *token = skip_space(*remaining);
    if (!*token) {
        *remaining = token;
        return (char *)0;
    }
    char *end = token;
    while (*end && !is_space(*end))
        ++end;
    if (*end)
        *end++ = '\0';
    *remaining = end;
    return token;
}

static int parse_integer(const char *text, i32 *result)
{
    if (!text)
        return 0;
    int negative = *text == '-';
    if (*text == '-' || *text == '+')
        ++text;
    if (!*text)
        return 0;
    u32 value = 0;
    u32 limit = negative ? 2147483648u : 2147483647u;
    while (*text) {
        if (*text < '0' || *text > '9')
            return 0;
        u32 digit = (u32)(*text++ - '0');
        if (value > (limit - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    if (negative)
        *result = value == 2147483648u ? I32_MINIMUM : -(i32)value;
    else
        *result = (i32)value;
    return 1;
}

static void prompt(void)
{
    console_write("grape> ");
}

static void command_help(char *arguments);
static void command_clear(char *arguments);
static void command_echo(char *arguments);
static void command_info(char *arguments);
static void command_neofetch(char *arguments);
static void command_mem(char *arguments);
static void command_uptime(char *arguments);
static void command_calc(char *arguments);
static void command_color(char *arguments);
static void command_history(char *arguments);
static void command_reboot(char *arguments);
static void command_halt(char *arguments);
static void command_del(char *arguments);
static void command_gui(char *arguments);
static void command_setup(char *arguments);
static void command_settings(char *arguments);
static void command_whoami(char *arguments);
static void command_hostname(char *arguments);
static void command_ver(char *arguments);
static void command_apps(char *arguments);
static void command_market(char *arguments);
static void command_snake(char *arguments);
static void command_guess(char *arguments);
static void command_notes(char *arguments);
static void command_ls(char *arguments);
static void command_cat(char *arguments);
static void command_fortune(char *arguments);
static void command_touch(char *arguments);
static void command_write(char *arguments);
static void command_rm(char *arguments);
static void command_cp(char *arguments);
static void command_mv(char *arguments);
static void command_wc(char *arguments);
static void command_pwd(char *arguments);
static void command_clock(char *arguments);
static void command_cpu(char *arguments);
#ifdef GRAPE_FIRMWARE
static void command_date(char *arguments);
static void command_theme(char *arguments);
#endif

struct command {
    const char *name;
    const char *help;
    void (*execute)(char *arguments);
};

static const struct command commands[] = {
    {"help",    "help              List commands", command_help},
    {"clear",   "clear             Clear the screen (Ctrl+L)", command_clear},
    {"echo",    "echo <text>       Print text", command_echo},
    {"info",    "info              Kernel and CPU information", command_info},
    {"neofetch", "neofetch          Grape logo and system information", command_neofetch},
    {"mem",     "mem               Show the kernel memory layout", command_mem},
    {"uptime",  "uptime            Time since boot", command_uptime},
    {"calc",    "calc <a> <op> <b> Signed integer + - * / %", command_calc},
    {"color",   "color <fg> <bg>   VGA colors: fg 0..15, bg 0..7", command_color},
    {"history", "history           Last 8 commands (Up/Down to recall)", command_history},
    {"reboot",  "reboot            Restart the machine", command_reboot},
    {"halt",    "halt              Stop the CPU", command_halt},
    {"del",     "del ram           Simulate missing RAM and kernel panic", command_del},
    {"gui",     "gui               Windowed offline desktop", command_gui},
    {"setup",   "setup             Run the welcome setup", command_setup},
    {"settings", "settings          Show saved settings", command_settings},
    {"whoami", "whoami            Show your name", command_whoami},
    {"hostname", "hostname [name]   Show or change the computer name", command_hostname},
    {"ver", "ver               Show the OS version", command_ver},
    {"apps", "apps              List installed local applications", command_apps},
    {"Grapemarket", "Grapemarket       Offline application catalog", command_market},
    {"grapemarket", "grapemarket       Offline application catalog", command_market},
    {"snake", "snake             Launch the installed Snake game", command_snake},
    {"guess", "guess             Launch the installed guessing game", command_guess},
    {"notes", "notes [set|clear] Local notes", command_notes},
    {"ls", "ls                List built-in system files", command_ls},
    {"cat", "cat <file>        Read a built-in system file", command_cat},
    {"fortune", "fortune           A small thought for the day", command_fortune},
    {"touch", "touch <file>      Create an empty RAM file", command_touch},
    {"write", "write <file> <text> Save text in a RAM file", command_write},
    {"rm", "rm <file>         Remove a RAM file", command_rm},
    {"cp", "cp <from> <to>    Copy a RAM file", command_cp},
    {"mv", "mv <from> <to>    Rename a RAM file", command_mv},
    {"wc", "wc <file>         Count lines, words and bytes", command_wc},
    {"pwd", "pwd               Current virtual folder", command_pwd},
    {"clock", "clock             Clock and input diagnostics", command_clock},
    {"cpu", "cpu               CPU brand reported by CPUID", command_cpu},
#ifdef GRAPE_FIRMWARE
    {"date", "date              Date and time from BIOS/UEFI", command_date},
    {"theme", "theme [name]      Desktop: grape, mint, midnight", command_theme},
#endif
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static int no_arguments(char *arguments, const char *name)
{
    if (!*skip_space(arguments))
        return 1;
    console_write(name);
    console_write(": no arguments expected.\n");
    return 0;
}

static void command_help(char *arguments)
{
    if (!no_arguments(arguments, "help"))
        return;
    console_write("GrapeOS commands\n\n");
    console_write("  System:  info  neofetch  mem  uptime  ver  reboot  halt\n");
    console_write("  Panic:   del ram (simulate missing RAM; reset VM to recover)\n");
    console_write("  Desktop: gui (Notepad, Terminal, Files, Calculator)\n");
    console_write("  Profile: setup  settings  whoami  hostname [name]\n");
    console_write("  Console: help  clear  echo <text>  color <fg> <bg>  history\n");
    console_write("  Tools:   calc <a> <+|-|*|/|%> <b>  fortune\n");
    console_write("  Files:   ls  cat  touch  write  rm  cp  mv  wc  pwd\n");
    console_write("  Hardware: cpu  clock\n");
#ifdef GRAPE_FIRMWARE
    console_write("  Desktop tools: date  theme [grape|mint|midnight]  Ctrl+Y\n");
#endif
    console_write("  Apps:    Grapemarket  apps  snake  guess  notes\n\n");
    console_write("Local app catalog (no internet):\n");
    console_write("  Grapemarket -L          List available apps\n");
    console_write("  Grapemarket -I snake    Install Snake\n");
    console_write("  Grapemarket -S          List installed apps\n");
    console_write("  Grapemarket -R snake    Remove Snake\n");
    console_write("  snake                  Play (arrows/WASD, P pause, Q quit)\n");
    console_write("  notes set <text>       Save a note after installing notes\n\n");
    console_write("Files: write hello.txt Hello Grape!  |  cat hello.txt\n");
#ifdef GRAPE_FIRMWARE
    console_write("USB /GRAPE: 32 text files, 4095 bytes each, saved across reboot.\n\n");
#else
    console_write("8 files, 255 bytes each; RAM files disappear on reboot.\n\n");
#endif
    console_write("Editing: Backspace, Up/Down, Ctrl+U/L/C, Escape; Tab completes commands.\n");
}

static void command_clear(char *arguments)
{
    if (no_arguments(arguments, "clear"))
        console_clear();
}

static void command_echo(char *arguments)
{
    console_write(arguments);
    console_putc('\n');
}

static void command_info(char *arguments)
{
    if (!no_arguments(arguments, "info"))
        return;
#ifdef GRAPE_FIRMWARE
    console_write("GrapeOS Desktop 0.3\nArchitecture: x86_64 / UEFI firmware services\n");
    console_write("Boot: GrapeLoad / native UEFI desktop\nDrivers: GOP, firmware keyboard/pointer, FAT filesystem\n");
#else
    console_write("GrapeOS 0.1\nArchitecture: i386, 32-bit protected mode\n");
    console_write(fb_active() ? "Boot: UEFI x64 / GRUB Multiboot2; GrapeOS kernel\n" :
                  "Boot: custom BIOS floppy loader; no external kernel or libc\n");
    console_write(fb_active() ? "Drivers: GOP framebuffer, polled PS/2, COM1, CPU TSC clock\n" :
                  "Drivers: VGA text, PS/2 keyboard, COM1 serial, PIC, PIT (100 Hz)\n");
#endif
    console_write("Storage: ");
    console_write(ramfs_persistent() ? "Real files on boot USB: /GRAPE\n" :
                  store_available() ? "Local ATA data disk\n" : "Session memory only\n");
}

static void command_neofetch(char *arguments)
{
    if (no_arguments(arguments, "neofetch"))
        fetch_show();
}

static void print_region(const char *name, const char *start, const char *end)
{
    console_write(name);
    console_hex((u32)(usize)start);
    console_write(" .. ");
    console_hex((u32)(usize)end);
    console_write(" (");
    console_u32((u32)((usize)end - (usize)start));
    console_write(" bytes)\n");
}

static void command_mem(char *arguments)
{
    if (!no_arguments(arguments, "mem"))
        return;
    print_region("Kernel: ", __kernel_start, __kernel_end);
    print_region("BSS:    ", __bss_start, __bss_end);
#ifdef GRAPE_FIRMWARE
    console_write("Stack and RAM allocation: managed by UEFI firmware.\n");
    console_write("Graphics: GOP framebuffer, 640x480 logical canvas.\n");
#else
    console_write("Stack:  top 0x00090000, grows downward\n");
    console_write("VGA:    0x000b8000 (80x25 text cells)\n");
    console_write("Physical RAM discovery and heap allocation are not implemented.\n");
#endif
}

static void command_uptime(char *arguments)
{
    if (!no_arguments(arguments, "uptime"))
        return;
    u32 ticks = timer_ticks();
    console_write("Uptime: ");
    console_u32(ticks / 100);
    console_putc('.');
    console_putc((char)('0' + (ticks % 100) / 10));
    console_putc((char)('0' + ticks % 10));
    console_write(" s (");
    console_u32(ticks);
    console_write(" ticks)\n");
}

static void command_calc(char *arguments)
{
    char *left_text = next_token(&arguments);
    char *operation = next_token(&arguments);
    char *right_text = next_token(&arguments);
    i32 left, right, result = 0;
    if (!parse_integer(left_text, &left) || !operation || !operation[0]
        || operation[1] || !parse_integer(right_text, &right)
        || next_token(&arguments)) {
        console_write("calc: expected <integer> <+|-|*|/|%> <integer>\n");
        return;
    }
    int overflow = 0;
    switch (*operation) {
    case '+': overflow = __builtin_add_overflow(left, right, &result); break;
    case '-': overflow = __builtin_sub_overflow(left, right, &result); break;
    case '*': overflow = __builtin_mul_overflow(left, right, &result); break;
    case '/':
    case '%':
        if (right == 0) {
            console_write("calc: division by zero\n");
            return;
        }
        if (left == I32_MINIMUM && right == -1)
            overflow = 1;
        else
            result = *operation == '/' ? left / right : left % right;
        break;
    default:
        console_write("calc: expected <integer> <+|-|*|/|%> <integer>\n");
        return;
    }
    if (overflow) {
        console_write("calc: overflow\n");
        return;
    }
    console_write("= ");
    console_i32(result);
    console_putc('\n');
}

static void command_color(char *arguments)
{
    char *foreground_text = next_token(&arguments);
    char *background_text = next_token(&arguments);
    i32 foreground, background;
    if (!parse_integer(foreground_text, &foreground)
        || !parse_integer(background_text, &background)
        || next_token(&arguments) || foreground < 0 || foreground > 15
        || background < 0 || background > 7) {
        console_write("color: expected foreground 0..15 and background 0..7\n");
        return;
    }
    struct store_state candidate = *store_get();
    candidate.foreground = (u8)foreground;
    candidate.background = (u8)background;
    if (!store_save(&candidate) && store_available()) {
        console_write("color: could not save; previous colors were kept.\n");
        return;
    }
    console_color((u8)foreground, (u8)background);
    console_write(store_available() ? "VGA color saved.\n" : "VGA color updated for this session.\n");
}

static void command_setup(char *arguments)
{
    if (no_arguments(arguments, "setup"))
        setup_begin(false);
}

static void command_settings(char *arguments)
{
    if (!no_arguments(arguments, "settings"))
        return;
    const struct store_state *profile = store_get();
    console_write("User: ");
    console_write(profile->username);
    console_write("\nComputer: ");
    console_write(profile->hostname);
    console_write("\nTheme: ");
    if (profile->background == 0 && profile->foreground == 13)
        console_write("Purple");
    else if (profile->background == 0 && profile->foreground == 10)
        console_write("Green");
    else if (profile->background == 0 && profile->foreground == 7)
        console_write("Classic");
    else
        console_write("Custom");
    console_write(" (");
    console_u32(profile->foreground);
    console_write(" on ");
    console_u32(profile->background);
    console_write(")\nStorage: ");
    console_write(ramfs_persistent() ? "Boot USB /GRAPE\n" : store_available() ? "Local data disk\n" : "Session only\n");
    console_write("Snake best: ");
    console_u32(profile->snake_best);
    console_putc('\n');
}

static void command_whoami(char *arguments)
{
    if (no_arguments(arguments, "whoami")) {
        console_write(store_get()->username);
        console_putc('\n');
    }
}

static void command_hostname(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name) {
        console_write(store_get()->hostname);
        console_putc('\n');
        return;
    }
    if (next_token(&arguments) || !setup_valid_hostname(name)) {
        console_write("hostname: use 1..23 letters, digits or -; no leading or trailing -.\n");
        return;
    }
    struct store_state candidate = *store_get();
    text_copy(candidate.hostname, name);
    if (!store_save(&candidate) && store_available()) {
        console_write("hostname: save failed; previous name was kept.\n");
        return;
    }
    console_write("Computer name updated: ");
    console_write(name);
    console_putc('\n');
}

static void command_ver(char *arguments)
{
    if (no_arguments(arguments, "ver"))
#ifdef GRAPE_FIRMWARE
        console_write("GrapeOS 0.3 - Photos, games and persistent USB files\n");
#else
        console_write("GrapeOS 0.1 - welcome setup and offline apps\n");
#endif
}

static void command_apps(char *arguments)
{
    if (no_arguments(arguments, "apps"))
        apps_list();
}

static void command_market(char *arguments)
{
    apps_market(arguments);
}

static void command_snake(char *arguments)
{
    (void)apps_launch("snake", arguments);
}

static void command_guess(char *arguments)
{
    (void)apps_launch("guess", arguments);
}

static void command_notes(char *arguments)
{
    (void)apps_launch("notes", arguments);
}

static void command_ls(char *arguments)
{
    if (!no_arguments(arguments, "ls")) return;
    console_write("Built-in files:\n  welcome.txt\n  commands.txt\n  profile.txt\n  catalog.txt\n  note.txt\n");
    console_write(ramfs_persistent() ? "USB files (/GRAPE, saved across reboot):\n" : "RAM files (/ram, until reboot):\n");
    for (u32 i = 0; i < RAMFS_FILES; ++i) {
        const struct ramfile *file = ramfs_at(i);
        if (*file->name) { console_write("  "); console_write(file->name); console_putc('\n'); }
    }
}

static void command_cat(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name || next_token(&arguments)) {
        console_write("cat: expected cat <file>; use ls for names.\n");
        return;
    }
    if (text_equal(name, "welcome.txt")) {
        console_write("Welcome to GrapeOS!\n");
        console_write("Your little offline computer. Type help to explore.\n");
        console_write("Install a local game: Grapemarket -I snake, then snake.\n");
        console_write("Run setup to change your profile and theme.\n");
    } else if (text_equal(name, "commands.txt")) {
        command_help(arguments);
    } else if (text_equal(name, "profile.txt")) {
        command_settings(arguments);
    } else if (text_equal(name, "catalog.txt")) {
        apps_market(arguments);
    } else if (text_equal(name, "note.txt")) {
        console_write(store_get()->note);
        console_putc('\n');
    } else if (ramfs_find(name)) {
        console_write(ramfs_find(name)->text);
        console_putc('\n');
    } else {
        console_write("cat: no such file: ");
        console_write(name);
        console_putc('\n');
    }
}

static void file_result(const char *error)
{
    console_write(error ? error : ramfs_persistent() ? "OK (saved to USB)" : "OK (RAM only, until reboot)");
    console_putc('\n');
}
static void command_touch(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name || next_token(&arguments)) { file_result("Usage: touch <file>"); return; }
    file_result(ramfs_find(name) ? NULL : ramfs_write(name, ""));
}
static void command_write(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name) { file_result("Usage: write <file> <text>"); return; }
    file_result(ramfs_write(name, skip_space(arguments)));
}
static void command_rm(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name || next_token(&arguments)) { file_result("Usage: rm <file>"); return; }
    file_result(ramfs_remove(name));
}
static void transfer(char *arguments, bool move)
{
    char *source = next_token(&arguments), *destination = next_token(&arguments);
    if (!source || !destination || next_token(&arguments)) {
        file_result(move ? "Usage: mv <from> <to>" : "Usage: cp <from> <to>"); return;
    }
    file_result(ramfs_copy(source, destination, move));
}
static void command_cp(char *arguments) { transfer(arguments, false); }
static void command_mv(char *arguments) { transfer(arguments, true); }
static void command_pwd(char *arguments)
{
    if (no_arguments(arguments, "pwd")) console_write(ramfs_persistent() ? "/GRAPE (boot USB, FAT files)\n" : "/ram (virtual folder, session only)\n");
}
static void command_wc(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name || next_token(&arguments)) { file_result("Usage: wc <file>"); return; }
    const struct ramfile *file = ramfs_find(name);
    const char *text = text_equal(name, "note.txt") ? store_get()->note : file ? file->text : NULL;
    if (!text) { file_result("no such text file"); return; }
    u32 bytes = 0, words = 0, lines = 0;
    bool word = false;
    while (text[bytes]) {
        char c = text[bytes++];
        if (c == '\n') ++lines;
        if (is_space(c) || c == '\n' || c == '\r') word = false;
        else if (!word) { ++words; word = true; }
    }
    if (bytes && text[bytes - 1] != '\n') ++lines;
    console_u32(lines); console_write(" lines, "); console_u32(words);
    console_write(" words, "); console_u32(bytes); console_write(" bytes\n");
}
#ifdef GRAPE_FIRMWARE
static void command_date(char *arguments)
{
    if (!no_arguments(arguments, "date")) return;
    char date[11], time[9];
    if (!firmware_time_text(date, time)) { console_write("Firmware clock unavailable.\n"); return; }
    console_write("Firmware date: "); console_write(date); console_putc(' ');
    console_write(time); console_write(" (BIOS/UEFI settings)\n");
}
static void command_theme(char *arguments)
{
    char *name = next_token(&arguments);
    if (!name) {
        console_write("Desktop theme: "); console_write(firmware_theme_name());
        console_write("\nAvailable: grape, mint, midnight. Ctrl+Y cycles themes.\n");
        return;
    }
    if (next_token(&arguments)) { console_write("Usage: theme [grape|mint|midnight]\n"); return; }
    console_write(firmware_theme_set(name)); console_putc('\n');
}
#endif
static void command_clock(char *arguments)
{
    if (!no_arguments(arguments, "clock")) return;
    console_write("Clock: "); console_write(platform_clock_source()); console_putc('\n');
    if (platform_clock_khz()) {
        console_write("TSC frequency: "); console_u32(platform_clock_khz()); console_write(" kHz\n");
    }
#ifdef GRAPE_FIRMWARE
    console_write("Input: UEFI keyboard/pointer; idle: firmware Stall\n");
#else
    console_write(platform_polling() ? "Input: polling; legacy IRQs disabled; idle: PAUSE\n" :
                  "Input: IRQ1; timer: IRQ0; idle: HLT\n");
#endif
    command_uptime(arguments);
}
static void command_cpu(char *arguments)
{
    if (!no_arguments(arguments, "cpu")) return;
    char name[49]; system_cpu_name(name); console_write(name); console_putc('\n');
}

static void command_fortune(char *arguments)
{
    if (!no_arguments(arguments, "fortune"))
        return;
    static const char *const thoughts[] = {
        "Every big OS began with a little boot sector.",
        "One grape at a time makes a whole bunch.",
        "Keep your curiosity. Save your work.",
        "Offline can still be a lot of fun.",
        "A tiny kernel, a big playground.",
        "Take a break. Your Snake will wait when paused."
    };
    console_write(thoughts[timer_ticks() % (sizeof(thoughts) / sizeof(thoughts[0]))]);
    console_putc('\n');
}

static void command_history(char *arguments)
{
    if (!no_arguments(arguments, "history"))
        return;
    for (u32 index = 0; index < history_count; ++index) {
        console_u32(index + 1);
        console_write("  ");
        console_write(history[index]);
        console_putc('\n');
    }
}

static void command_reboot(char *arguments)
{
    if (!no_arguments(arguments, "reboot"))
        return;
    gui_stop();
    console_write("Rebooting...\n");
#ifdef GRAPE_FIRMWARE
    firmware_reset();
#else
    __asm__ volatile ("cli");
    for (u32 retry = 0; retry < 100000u; ++retry) {
        if (!(inb(0x64) & 2)) {
            outb(0x64, 0xfe);
            break;
        }
        io_wait();
    }
    /* Fall back to a triple fault if the keyboard controller cannot reset. */
    const struct {
        u16 limit;
        u32 base;
    } __attribute__((packed)) empty_idt = {0, 0};
    __asm__ volatile ("lidt %0; int $3" : : "m"(empty_idt) : "memory");
    for (;;)
        __asm__ volatile ("hlt");
#endif
}

static void command_halt(char *arguments)
{
    if (!no_arguments(arguments, "halt"))
        return;
    console_write("CPU halted. Reset the VM to continue.\n");
    gui_tick();
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

static void command_del(char *arguments)
{
    char *target = next_token(&arguments);
    if (!target || !text_equal(target, "ram") || next_token(&arguments)) {
        console_write("del: expected del ram (kernel panic test; reset VM to recover).\n");
        return;
    }
    kernel_panic("NO_USABLE_RAM", "No usable RAM (simulated)", "del ram");
}

static void command_gui(char *arguments)
{
    if (no_arguments(arguments, "gui")) gui_start();
}

static void remember_line(void)
{
    if (history_count && text_equal(history[history_count - 1], line))
        return;
    if (history_count == HISTORY_CAPACITY) {
        for (u32 index = 1; index < HISTORY_CAPACITY; ++index)
            text_copy(history[index - 1], history[index]);
        --history_count;
    }
    text_copy(history[history_count++], line);
}

static void execute_line(void)
{
    char *arguments = line;
    char *name = next_token(&arguments);
    if (!name)
        return;
    arguments = skip_space(arguments);
    if (gui_active() && (text_equal(name, "gui") || text_equal(name, "setup")
#ifndef GRAPE_FIRMWARE
                        || text_equal(name, "snake") || text_equal(name, "guess") || text_equal(name, "neofetch")
#endif
                        )) {
        console_write("Use this command from the text shell. Press Escape twice to leave GUI.\n");
        return;
    }
    for (u32 index = 0; index < COMMAND_COUNT; ++index) {
        if (text_equal(name, commands[index].name)) {
            commands[index].execute(arguments);
            return;
        }
    }
    console_write("Unknown command: ");
    console_write(name);
    console_write(". Type help.\n");
}

static void erase_line(void)
{
    while (line_length) {
        console_putc('\b');
        --line_length;
    }
    line[0] = '\0';
    excess_characters = 0;
}

static void replace_line(const char *replacement)
{
    erase_line();
    while (*replacement) {
        line[line_length++] = *replacement;
        console_putc(*replacement++);
    }
    line[line_length] = '\0';
}

static void recall_history(int direction)
{
    if (!history_count)
        return;
    if (direction < 0) {
        if (history_position < 0) {
            text_copy(draft, line);
            draft_excess_characters = excess_characters;
            history_position = (i32)history_count - 1;
        } else if (history_position > 0) {
            --history_position;
        }
    } else {
        if (history_position < 0)
            return;
        if (++history_position == (i32)history_count) {
            history_position = -1;
            replace_line(draft);
            excess_characters = draft_excess_characters;
            return;
        }
    }
    replace_line(history[history_position]);
}

static void complete_command(void)
{
    if (!line_length || excess_characters)
        return;
    for (u32 index = 0; index < line_length; ++index)
        if (is_space(line[index]))
            return;
    const char *match = (const char *)0;
    u32 matches = 0;
    for (u32 index = 0; index < COMMAND_COUNT; ++index) {
        const char *name = commands[index].name;
        u32 offset = 0;
        while (offset < line_length && name[offset] && name[offset] == line[offset])
            ++offset;
        if (offset == line_length) {
            match = name;
            ++matches;
        }
    }
    if (matches == 1) {
        while (match[line_length]) {
            line[line_length] = match[line_length];
            console_putc(line[line_length++]);
        }
        line[line_length++] = ' ';
        console_putc(' ');
        line[line_length] = '\0';
    }
}

void shell_init(void)
{
    prompt();
}

void shell_resume(void)
{
    line_length = excess_characters = 0;
    line[0] = '\0';
    history_position = -1;
    prompt();
}

void shell_focus(void) { console_serial_write("grape> "); }
void shell_calculate(char *expression) { command_calc(expression); }

void shell_input(int key)
{
    if (key == KEY_UP || key == KEY_DOWN) {
        recall_history(key == KEY_UP ? -1 : 1);
        return;
    }
    if (key == '\n' || key == '\r') {
        console_putc('\n');
        if (excess_characters) {
            console_write("Input too long (maximum 127 characters).\n");
        } else if (*skip_space(line)) {
            remember_line();
            execute_line();
        }
        line_length = 0;
        excess_characters = 0;
        history_position = -1;
        line[0] = '\0';
        if (!setup_active() && !apps_active() && (!gui_active() || gui_terminal_active()))
            prompt();
    } else if (key == '\b' || key == 127) {
        if (excess_characters) {
            --excess_characters;
        } else if (line_length) {
            line[--line_length] = '\0';
            console_putc('\b');
        }
    } else if (key == '\f') {
        console_clear();
        prompt();
        console_write(line);
    } else if (key == 0x15) {
        erase_line();
    } else if (key == 0x03 || key == 0x1b) {
        console_write("^C\n");
        line_length = 0;
        excess_characters = 0;
        line[0] = '\0';
        history_position = -1;
        prompt();
    } else if (key == '\t') {
        complete_command();
    } else if (key >= 32 && key < 127) {
        if (line_length == LINE_CAPACITY - 1) {
            if (excess_characters < 0xffffffffu)
                ++excess_characters;
        } else {
            line[line_length++] = (char)key;
            line[line_length] = '\0';
            console_putc((char)key);
        }
    }
}
