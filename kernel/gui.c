#include "console.h"
#include "gfx.h"
#include "gui.h"
#include "mouse.h"
#include "platform.h"
#include "ramfs.h"
#include "shell.h"
#include "store.h"
#include "apps.h"
#ifdef GRAPE_FIRMWARE
#include "firmware.h"
#include "photo.h"
#include "snake.h"
#endif

#ifdef GRAPE_FIRMWARE
#define COLS 80u
#define ROWS 25u
#define APP_COUNT 6u
#else
#define COLS 66u
#define ROWS 17u
#define APP_COUNT 4u
#endif
#define NOTE_COLS 66u
#define NOTE_ROWS 16u
enum view { DESKTOP, NOTEPAD, TERMINAL, FILES, CALCULATOR, PHOTOS, SNAKE };
struct window { u32 x, y, w, h; bool visible; };
static struct window windows[APP_COUNT + 1];
static enum view order[APP_COUNT], view;
static bool running, dirty, changed, terminal_started, capturing;
static u32 selected, length, cursor, row, column, blink, last_draw, file_selection;
static char note[RAMFS_TEXT], filename[RAMFS_NAME], terminal[ROWS][COLS];
static bool keyboard_pointer, keyboard_click;
static u32 file_dialog, dialog_length;
static char dialog_name[RAMFS_NAME];
static char expression[64], answer[128];
static u32 expression_length, answer_length;
static const char *note_status, *files_status;
static struct mouse_state pointer;
static enum view dragging;
static i32 drag_x, drag_y;
static const char *const names[] = {"Desktop", "Notepad", "Terminal", "Files", "Calculator", "Photos", "Snake"};
static const char *const shortcuts[] = {"Ctrl+N", "Ctrl+T", "Ctrl+F", "Ctrl+K", "Ctrl+P", "Ctrl+G"};
static u8 terminal_fg[ROWS][COLS], terminal_bg[ROWS][COLS];
#ifdef GRAPE_FIRMWARE
static u32 photo_selection;
static const char *photo_status;
static const char *theme_status = "Click Theme or Ctrl+Y to change desktop colours.";
static u32 clock_second;
#endif

static void copy(char *to, const char *from) { do { *to++ = *from; } while (*from++); }
static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void number(char text[12], u32 value)
{
    char reverse[12]; u32 n = 0, i = 0;
    do { reverse[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) text[i++] = reverse[--n];
    text[i] = 0;
}

void gui_console_clear(void)
{
    for (u32 y = 0; y < ROWS; ++y)
        for (u32 x = 0; x < COLS; ++x) {
            terminal[y][x] = ' '; terminal_fg[y][x] = 7; terminal_bg[y][x] = 0;
        }
    row = column = 0; dirty = true;
}
void gui_console_draw(u32 x, u32 y, char c, u8 fg, u8 bg)
{
    if (!running || x >= COLS || y >= ROWS) return;
    terminal[y][x] = c; terminal_fg[y][x] = fg & 15; terminal_bg[y][x] = bg & 15; dirty = true;
}
void gui_console_putc(char c)
{
    if (capturing) {
        if (c != '\n' && c != '\r' && answer_length < sizeof(answer) - 1)
            answer[answer_length++] = c;
        answer[answer_length] = 0;
        return;
    }
    if (c == '\n') { column = 0; ++row; }
    else if (c == '\r') column = 0;
    else if (c == '\b') {
        if (column) --column;
        else if (row) { --row; column = COLS - 1; }
        terminal[row][column] = ' ';
    } else if (c == '\t') {
        do { gui_console_putc(' '); } while (column % 4);
        return;
    } else if ((u8)c >= 32 && (u8)c < 127) {
        terminal[row][column] = c;
        terminal_fg[row][column] = console_get_color() & 15; terminal_bg[row][column] = console_get_color() >> 4;
        if (++column == COLS) { column = 0; ++row; }
    }
    if (row >= ROWS) {
        for (u32 y = 1; y < ROWS; ++y)
            for (u32 x = 0; x < COLS; ++x) {
                terminal[y - 1][x] = terminal[y][x]; terminal_fg[y-1][x] = terminal_fg[y][x]; terminal_bg[y-1][x] = terminal_bg[y][x];
            }
        for (u32 x = 0; x < COLS; ++x) { terminal[ROWS-1][x] = ' '; terminal_fg[ROWS-1][x] = 7; terminal_bg[ROWS-1][x] = 0; }
        row = ROWS - 1;
    }
    dirty = true;
}
bool gui_active(void) { return running; }
bool gui_terminal_active(void) { return running && view == TERMINAL; }

static void load_note(const char *name, const char *text)
{
    copy(filename, name); copy(note, text);
    length = 0; while (note[length]) ++length;
    cursor = length; changed = false;
    note_status = "Arrows: move cursor | Ctrl+S: save | Ctrl+U: clear";
}
void gui_start(void)
{
    if (running) return;
    load_note("note.txt", store_get()->note);
    windows[NOTEPAD] = (struct window){48, 60, 576, 352, false};
    windows[TERMINAL] = (struct window){24, 78, 576, 352, false};
    windows[FILES] = (struct window){40, 64, 560, 340, false};
    windows[CALCULATOR] = (struct window){152, 116, 376, 248, false};
#ifdef GRAPE_FIRMWARE
    windows[PHOTOS] = (struct window){36, 64, 568, 356, false};
    photo_refresh(); photo_selection = 0; photo_status = photo_open(0);
#endif
    for (u32 i = 0; i < APP_COUNT; ++i) order[i] = (enum view)(i + 1);
    terminal_started = capturing = false;
    expression_length = answer_length = file_selection = 0;
    expression[0] = answer[0] = 0;
    files_status = "Enter: edit | Delete: remove file";
    dragging = DESKTOP;
    keyboard_pointer = keyboard_click = false; file_dialog = dialog_length = 0;
    mouse_poll(&pointer);
    gui_console_clear(); gfx_enter();
    running = dirty = true; view = DESKTOP; selected = 0;
    blink = timer_ticks() / 50; last_draw = timer_ticks() - 5;
    console_serial_write("[GUI] Desktop ready. Ctrl+N Note; Ctrl+T Terminal; Ctrl+F Files; Ctrl+K Calculator.\n");
}
void gui_stop(void)
{
    if (!running) return;
    if (apps_active()) apps_input('q');
    running = false; gfx_leave(); console_serial_write("[GUI] Closed.\n");
}
static void focus(enum view next)
{
    view = next; windows[next].visible = true;
    u32 index = 0;
    while (index < APP_COUNT && order[index] != next) ++index;
    while (index + 1 < APP_COUNT) { order[index] = order[index + 1]; ++index; }
    order[APP_COUNT - 1] = next; dirty = true;
}
static void open_view(enum view next)
{
#ifdef GRAPE_FIRMWARE
    if (next == SNAKE) {
        open_view(TERMINAL);
        if (apps_active()) return;
        char install[] = "-I snake", empty[] = "";
        if (!(store_get()->installed_apps & STORE_APP_SNAKE)) apps_market(install);
        (void)apps_launch("snake", empty);
        return;
    }
#endif
    if (next == view && windows[next].visible) return;
    focus(next);
    if (next == NOTEPAD) {
        if (!changed && equal(filename, "note.txt")) load_note("note.txt", store_get()->note);
        console_serial_write("[GUI] Notepad opened.\n");
    } else if (next == TERMINAL) {
        console_serial_write("[GUI] Terminal opened.\n");
        if (!terminal_started) {
            terminal_started = true; gui_console_clear();
            console_write(ramfs_persistent() ? "GrapeOS Terminal\nType help. Files are saved on USB in /GRAPE.\n\n" :
                          "GrapeOS Terminal\nType help. Files live in /ram until reboot.\n\n");
            shell_resume();
        } else if (!apps_active()) shell_focus();
    } else if (next == FILES) console_serial_write("[GUI] Files opened.\n");
#ifdef GRAPE_FIRMWARE
    else if (next == PHOTOS) { photo_refresh(); if (photo_selection >= photo_count()) photo_selection = 0; photo_status = photo_open(photo_selection); console_serial_write("[GUI] Photos opened.\n"); }
#endif
    else console_serial_write("[GUI] Calculator opened.\n");
}
#ifdef GRAPE_FIRMWARE
static void run_terminal_command(const char *command)
{
    open_view(TERMINAL);
    if (apps_active()) return;
    shell_input(0x15);
    while (*command) shell_input(*command++);
    shell_input('\n');
}
#endif
static void home(void)
{
    for (u32 i = 1; i <= APP_COUNT; ++i) windows[i].visible = false;
    view = dragging = DESKTOP; dirty = true;
    console_serial_write("[GUI] Desktop ready.\n");
}
static void minimize(enum view which)
{
    windows[which].visible = false; view = DESKTOP;
    for (int i = APP_COUNT - 1; i >= 0; --i)
        if (windows[order[i]].visible) { view = order[i]; break; }
    dirty = true;
    console_serial_write(view == DESKTOP ? "[GUI] Desktop ready.\n" : "[GUI] Window minimized.\n");
}
static void save_note(void)
{
    bool saved = false;
    if (equal(filename, "note.txt")) {
        struct store_state candidate = *store_get(); copy(candidate.note, note);
        saved = store_save(&candidate);
        if (saved || !store_available()) {
            changed = false;
            note_status = saved ? (ramfs_persistent() ? "Saved to USB /GRAPE/note.txt" : "Saved to local disk") : "Saved for this session only";
        } else note_status = "Save failed. Draft kept.";
        console_serial_write(saved ? "[GUI] Note saved to local disk.\n" :
            store_available() ? "[GUI] Note save failed; draft kept.\n" :
            "[GUI] Note saved for this session only.\n");
    } else {
        const char *error = ramfs_write(filename, note);
        if (!error) changed = false;
        note_status = error ? error : ramfs_persistent() ? "Saved to USB /GRAPE" : "Saved in RAM; disappears on reboot";
        console_serial_write(error ? "[GUI] File save failed.\n" : ramfs_persistent() ? "[GUI] USB file saved.\n" : "[GUI] RAM file saved.\n");
    }
    dirty = true;
}
static u32 note_row(u32 index, u32 *col)
{
    u32 r = 0; *col = 0;
    for (u32 i = 0; i < index; ++i) {
        if (note[i] == '\n') { ++r; *col = 0; }
        else if (++*col == NOTE_COLS) { ++r; *col = 0; }
    }
    return r;
}
static u32 note_index(u32 target_row, u32 target_col)
{
    u32 r = 0, c = 0, i = 0;
    for (; i < length; ++i) {
        if (r == target_row && (c >= target_col || note[i] == '\n')) break;
        if (r > target_row) break;
        if (note[i] == '\n') { ++r; c = 0; }
        else if (++c == NOTE_COLS) { ++r; c = 0; }
    }
    return i;
}
static void edit_note(int key)
{
    u32 col, r = note_row(cursor, &col);
    if (key == KEY_LEFT) { if (cursor) --cursor; }
    else if (key == KEY_RIGHT) { if (cursor < length) ++cursor; }
    else if (key == KEY_UP) cursor = note_index(r ? r - 1 : 0, col);
    else if (key == KEY_DOWN) cursor = note_index(r + 1, col);
    else if (key == KEY_HOME) cursor = note_index(r, 0);
    else if (key == KEY_END) cursor = note_index(r, NOTE_COLS - 1);
    else if (key == 0x13) { save_note(); return; }
    else if (key == '\b' || key == 127 || key == KEY_DELETE) {
        if (key == KEY_DELETE) { if (cursor == length) return; }
        else { if (!cursor) return; --cursor; }
        for (u32 i = cursor; i < length; ++i) note[i] = note[i + 1];
        --length; changed = true;
    } else if (key == 0x15) {
        length = cursor = 0; note[0] = 0; changed = true;
    } else if (key == '\n' || key == '\r' || (key >= 32 && key < 127)) {
        if (length == sizeof(note) - 1) {
            note_status = ramfs_persistent() ? "Limit: 4095 bytes" : "Limit: 255 characters";
            dirty = true; return;
        }
        for (u32 i = length + 1; i > cursor; --i) note[i] = note[i - 1];
        note[cursor++] = key == '\r' ? '\n' : (char)key;
        ++length; changed = true;
    } else return;
    dirty = true;
}
static const struct ramfile *file_at(u32 selection)
{
    if (!selection) return NULL;
    for (u32 i = 0; i < RAMFS_FILES; ++i) {
        const struct ramfile *file = ramfs_at(i);
        if (*file->name && !--selection) return file;
    }
    return NULL;
}
static u32 file_count(void)
{
    u32 n = 1;
    for (u32 i = 0; i < RAMFS_FILES; ++i) if (*ramfs_at(i)->name) ++n;
    return n;
}
static void file_open(void)
{
    if (changed) { files_status = "Save your open draft with Ctrl+S first"; dirty = true; return; }
    const struct ramfile *file = file_at(file_selection);
    load_note(file ? file->name : "note.txt", file ? file->text : store_get()->note);
    open_view(NOTEPAD);
}
static void file_prompt(u32 action)
{
    if (action == 2 && !file_selection) { files_status = "The profile note cannot be renamed"; dirty = true; return; }
    if (changed) { files_status = "Save your open draft with Ctrl+S first"; dirty = true; return; }
    file_dialog = action; dialog_name[0] = 0; dialog_length = 0;
    files_status = "Letters, digits, dot, dash, underscore";
    if (action == 2) {
        copy(dialog_name, file_at(file_selection)->name);
        while (dialog_name[dialog_length]) ++dialog_length;
    }
    dirty = true;
    console_serial_write(action == 1 ? "[GUI] New file name: \n" : "[GUI] Rename file: \n");
}
static void file_confirm(void)
{
    const char *error;
    char old[RAMFS_NAME]; old[0] = 0;
    if (file_dialog == 1) {
        error = ramfs_find(dialog_name) ? "A file with this name already exists" : ramfs_write(dialog_name, "");
    } else {
        copy(old, file_at(file_selection)->name);
        error = ramfs_copy(old, dialog_name, true);
    }
    if (error) { files_status = error; dirty = true; return; }
    if (file_dialog == 2 && equal(filename, old)) copy(filename, dialog_name);
    file_selection = 0;
    for (u32 i = 1; i < file_count(); ++i)
        if (equal(file_at(i)->name, dialog_name)) { file_selection = i; break; }
    file_dialog = 0;
    files_status = ramfs_persistent() ? "Saved on boot USB: /GRAPE" : "Saved in RAM until reboot";
    console_serial_write("[GUI] File operation completed.\n");
    dirty = true;
}
static void calculate(void)
{
    char scratch[sizeof(expression)]; copy(scratch, expression);
    capturing = true; answer_length = 0; answer[0] = 0;
    console_serial_write("[GUI] Calculator: "); shell_calculate(scratch);
    capturing = false; dirty = true;
}
#ifdef GRAPE_FIRMWARE
static void cycle_theme(void)
{
    theme_status = firmware_theme_next(); dirty = true;
    console_serial_write("[GUI] Theme: "); console_serial_write(firmware_theme_name());
    console_serial_write("\n");
}
#endif
void gui_input(int key)
{
    if (key == KEY_POINTER) {
        keyboard_pointer = !keyboard_pointer; pointer.buttons = 0; dirty = true;
        console_serial_write(keyboard_pointer ? "[GUI] Keyboard cursor on.\n" : "[GUI] Keyboard cursor off.\n");
        return;
    }
    if (keyboard_pointer && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN || key == ' ' || key == '\n')) {
        if (key == KEY_LEFT && pointer.x >= 8) pointer.x -= 8;
        if (key == KEY_RIGHT && pointer.x < 632) pointer.x += 8;
        if (key == KEY_UP && pointer.y >= 8) pointer.y -= 8;
        if (key == KEY_DOWN && pointer.y < 472) pointer.y += 8;
        if (key == ' ' || key == '\n') keyboard_click = true;
        dirty = true; return;
    }
    if (file_dialog) {
        if (key == 0x1b) { file_dialog = 0; dirty = true; }
        else if (key == '\n' || key == '\r') file_confirm();
        else if (key == '\b' && dialog_length) { dialog_name[--dialog_length] = 0; dirty = true; }
        else if (key == 0x15) { dialog_length = 0; dialog_name[0] = 0; dirty = true; }
        else if (key >= 32 && key < 127 && dialog_length < RAMFS_NAME - 1) {
            dialog_name[dialog_length++] = (char)key; dialog_name[dialog_length] = 0; dirty = true;
        }
        return;
    }
    if (key == 0x0e) { open_view(NOTEPAD); return; }
    if (key == 0x14) { open_view(TERMINAL); return; }
    if (key == 0x06) { open_view(FILES); return; }
    if (key == 0x0b) { open_view(CALCULATOR); return; }
#ifdef GRAPE_FIRMWARE
    if (key == 0x19) { cycle_theme(); return; }
    if (key == 0x10) { open_view(PHOTOS); return; }
    if (key == 0x07) { open_view(SNAKE); return; }
    if (key == 0x05) { run_terminal_command("neofetch"); return; }
    if (view == TERMINAL && apps_active()) { apps_input(key); return; }
#endif
    if (key == 0x02 && view != DESKTOP) { minimize(view); return; }
    if (key == 0x1b || key == 0x17) {
        if (view == DESKTOP) gui_stop(); else home();
        return;
    }
    if (view == DESKTOP) {
        if (key == '\t' || key == KEY_RIGHT || key == KEY_DOWN) { selected = (selected + 1) % APP_COUNT; dirty = true; }
        else if (key == KEY_LEFT || key == KEY_UP) { selected = (selected + APP_COUNT - 1) % APP_COUNT; dirty = true; }
        else if (key == '\n' || key == '\r') open_view((enum view)(selected + 1));
    } else if (view == TERMINAL) shell_input(key);
    else if (view == NOTEPAD) edit_note(key);
    else if (view == FILES) {
        u32 count = file_count();
        if (file_selection >= count) file_selection = count - 1;
        if (key == 0x01) file_prompt(1);
        else if (key == 0x12) file_prompt(2);
        else if (key == KEY_UP && file_selection) --file_selection;
        else if (key == KEY_DOWN && file_selection + 1 < count) ++file_selection;
        else if (key == '\n' || key == '\r') file_open();
        else if (key == KEY_DELETE && file_selection) {
            const struct ramfile *file = file_at(file_selection);
            files_status = ramfs_remove(file->name);
            if (!files_status) files_status = "File removed";
            if (file_selection >= file_count()) --file_selection;
        }
        dirty = true;
    }
#ifdef GRAPE_FIRMWARE
    else if (view == PHOTOS) {
        bool reload = false;
        if (key == KEY_UP && photo_selection) { --photo_selection; reload = true; }
        if (key == KEY_DOWN && photo_selection + 1 < photo_count()) { ++photo_selection; reload = true; }
        if (key == 'r' || key == 'R') { photo_refresh(); if (photo_selection >= photo_count()) photo_selection = 0; reload = true; }
        if (reload) photo_status = photo_open(photo_selection);
        dirty = true;
    }
#endif
    else {
        if (key == '\n' || key == '\r') calculate();
        else if (key == '\b' || key == 127) { if (expression_length) expression[--expression_length] = 0; }
        else if (key == 0x15) expression[expression_length = 0] = 0;
        else if (key >= 32 && key < 127 && expression_length < sizeof(expression) - 1) {
            expression[expression_length++] = (char)key; expression[expression_length] = 0;
        }
        dirty = true;
    }
}
static bool inside(i32 x, i32 y, u32 left, u32 top, u32 width, u32 height)
{
    return x >= (i32)left && y >= (i32)top && x < (i32)(left + width) && y < (i32)(top + height);
}
static u32 dock_x(u32 index)
{
#ifdef GRAPE_FIRMWARE
    return 16 + index * 102;
#else
    return 16 + index * 152;
#endif
}
static u32 dock_width(void)
{
#ifdef GRAPE_FIRMWARE
    return 96;
#else
    return 144;
#endif
}
static u32 card_x(u32 index)
{
#ifdef GRAPE_FIRMWARE
    return 28 + (index % 3) * 200;
#else
    return 24 + index * 152;
#endif
}
static u32 card_y(u32 index)
{
#ifdef GRAPE_FIRMWARE
    return 176 + (index / 3) * 100;
#else
    (void)index; return 176;
#endif
}
static void mouse_tick(void)
{
    struct mouse_state next;
    if (keyboard_pointer) {
        next = pointer; next.available = true; next.buttons = keyboard_click ? 1 : 0; keyboard_click = false;
    } else mouse_poll(&next);
    if (!next.available) return;
    bool press = (next.buttons & 1) && !(pointer.buttons & 1);
    if (next.x != pointer.x || next.y != pointer.y || next.buttons != pointer.buttons) dirty = true;
    pointer = next;
    if (!(pointer.buttons & 1)) dragging = DESKTOP;
    if (dragging != DESKTOP) {
        struct window *w = &windows[dragging];
        i32 x = pointer.x - drag_x, y = pointer.y - drag_y;
        if (x < 0) x = 0;
        if (x > (i32)(640 - w->w)) x = (i32)(640 - w->w);
        if (y < 36) y = 36;
        if (y > (i32)(440 - w->h)) y = (i32)(440 - w->h);
        w->x = (u32)x; w->y = (u32)y;
        return;
    }
    if (!press) return;
    if (file_dialog) {
        if (inside(pointer.x, pointer.y, 156, 262, 144, 30)) file_confirm();
        else if (inside(pointer.x, pointer.y, 324, 262, 144, 30)) { file_dialog = 0; dirty = true; }
        return;
    }
    if (pointer.y >= 444) {
        for (u32 i = 0; i < APP_COUNT; ++i)
            if (inside(pointer.x, pointer.y, dock_x(i), 444, dock_width(), 30)) {
                open_view((enum view)(i + 1)); return;
            }
    }
#ifdef GRAPE_FIRMWARE
    if (inside(pointer.x, pointer.y, 332, 8, 164, 32)) { cycle_theme(); return; }
#endif
    for (int i = APP_COUNT - 1; i >= 0; --i) {
        enum view which = order[i]; struct window *w = &windows[which];
        if (!w->visible || !inside(pointer.x, pointer.y, w->x, w->y, w->w, w->h)) continue;
        open_view(which);
        if (inside(pointer.x, pointer.y, w->x + w->w - 28, w->y, 28, 28) ||
            inside(pointer.x, pointer.y, w->x + w->w - 56, w->y, 28, 28)) {
            minimize(which); return;
        }
        if (pointer.y < (i32)(w->y + 28)) {
            dragging = which; drag_x = pointer.x - (i32)w->x; drag_y = pointer.y - (i32)w->y;
        } else if (which == FILES && inside(pointer.x, pointer.y, w->x + 388, w->y + 36, 144, 24)) {
            file_prompt(1);
        } else if (which == FILES && inside(pointer.x, pointer.y, w->x + 16, w->y + 88, w->w - 32, 9 * 24)) {
            u32 first = file_selection > 8 ? file_selection - 8 : 0;
            u32 choice = first + ((u32)pointer.y - w->y - 88) / 24;
            if (choice < file_count()) { file_selection = choice; file_open(); }
        } else if (which == CALCULATOR && inside(pointer.x, pointer.y, w->x + 20, w->y + 192, 176, 28)) calculate();
#ifdef GRAPE_FIRMWARE
        else if (which == PHOTOS && inside(pointer.x, pointer.y, w->x + 12, w->y + 82, 176, 8 * 24)) {
            u32 first = photo_selection > 7 ? photo_selection - 7 : 0;
            u32 choice = first + ((u32)pointer.y - w->y - 82) / 24;
            if (choice < photo_count()) { photo_selection = choice; photo_status = photo_open(choice); dirty = true; }
        }
#endif
        return;
    }
    if (view == DESKTOP)
        for (u32 i = 0; i < APP_COUNT; ++i)
            if (inside(pointer.x, pointer.y, card_x(i), card_y(i),
#ifdef GRAPE_FIRMWARE
                       184, 84
#else
                       136, 148
#endif
                       )) {
                selected = i; open_view((enum view)(i + 1)); return;
            }
}
static void circle(u32 x, u32 y, u32 radius, u8 color)
{
    for (i32 dy = -(i32)radius; dy <= (i32)radius; ++dy) {
        u32 half = 0;
        while ((half + 1) * (half + 1) + (u32)(dy * dy) <= radius * radius) ++half;
        gfx_rect(x - half, (u32)((i32)y + dy), half * 2 + 1, 1, color);
    }
}
static void app_icon(u32 x, u32 y, enum view app)
{
    if (app == NOTEPAD) {
        gfx_rect(x, y, 44, 54, 20); gfx_rect(x, y, 44, 8, 19);
        for (u32 r = 14; r < 48; r += 8) gfx_rect(x + 8, y + r, 28, 2, 22);
    } else if (app == TERMINAL) {
        gfx_rect(x - 4, y + 4, 56, 42, 0); gfx_rect(x - 4, y + 4, 56, 8, 18);
        gfx_text(x + 4, y + 18, ">_", 23);
    } else if (app == FILES) {
        gfx_rect(x, y + 8, 24, 12, 19); gfx_rect(x - 4, y + 18, 56, 32, 14);
        gfx_rect(x, y + 22, 48, 2, 14);
    }
#ifdef GRAPE_FIRMWARE
    else if (app == PHOTOS) { gfx_rect(x - 2, y - 2, 48, 58, 20); photo_art(x, y, 44, 54); }
    else if (app == SNAKE) {
        for (u32 i = 0; i < 4; ++i) circle(x + 8 + i * 9, y + 20, 6, 10);
        for (u32 i = 0; i < 3; ++i) circle(x + 8, y + 20 + i * 9, 6, 10);
        gfx_rect(x + 34, y + 17, 2, 2, 0); circle(x + 42, y + 42, 5, 12);
    }
#endif
    else {
        gfx_rect(x, y, 44, 54, 22); gfx_rect(x + 5, y + 6, 34, 12, 17);
        for (u32 r = 26; r < 48; r += 10)
            for (u32 c = 8; c < 36; c += 10) gfx_rect(x + c, y + r, 6, 6, 20);
    }
}
static void rounded(u32 x, u32 y, u32 w, u32 h, u8 ink)
{
    gfx_rect(x + 6, y, w - 12, h, ink);
    gfx_rect(x, y + 6, w, h - 12, ink);
    for (u32 dy = 0; dy < 6; ++dy) {
        u32 inset = dy < 1 ? 4 : dy < 2 ? 2 : 1;
        gfx_rect(x + inset, y + dy, w - inset * 2, 1, ink);
        gfx_rect(x + inset, y + h - dy - 1, w - inset * 2, 1, ink);
    }
}
static void render_desktop(void)
{
#ifdef GRAPE_FIRMWARE
    gfx_wallpaper();
    rounded(16, 8, 608, 32, 17);
    gfx_text(30, 16, "GrapeOS", 15); gfx_text(120, 16, "Desktop", 19);
    char host[15], date[11], time[9];
    u32 h = 0; while (h < 14 && store_get()->hostname[h]) { host[h] = store_get()->hostname[h]; ++h; }
    host[h] = 0;
    gfx_text(208, 16, host, 7);
    gfx_text(340, 16, "Theme", 7); gfx_text(388, 16, firmware_theme_name(), 19);
    firmware_time_text(date, time); gfx_text(528, 16, time, 15);
    gfx_text(28, 68, "Your space. Your Grape.", 15);
    gfx_text(28, 96, "Create, play and keep everything offline.", 7);
    gfx_text(28, 130, "APPS", 19);
    rounded(522, 54, 90, 96, 20); photo_art(532, 64, 70, 76);
    for (u32 i = 0; i < APP_COUNT; ++i) {
        u32 x = card_x(i), y = card_y(i);
        rounded(x + 2, y + 4, 184, 84, 0);
        rounded(x, y, 184, 84, selected == i ? 19 : 22);
        rounded(x + 1, y + 1, 182, 82, 17);
        app_icon(x + 14, y + 15, (enum view)(i + 1));
        gfx_text(x + 72, y + 22, names[i+1], 15);
        gfx_text(x + 72, y + 48, shortcuts[i], 19);
    }
    gfx_text(28, 382, theme_status, 7);
    gfx_text(28, 410, keyboard_pointer ? "CURSOR: arrows move | Enter click | F8 exit" : "F8: keyboard cursor   Esc: home   Ctrl+B: minimize", 19);
#else
    gfx_rect(0, 0, 640, 480, 16);
    gfx_rect(0, 96, 640, 272, 17);
    for (u32 i = 0; i < 7; ++i) {
        gfx_rect(0, 144 + i * 24, 160 - i * 16, 2, 22);
        gfx_rect(496 + i * 16, 136 + i * 28, 144 - i * 16, 2, 22);
    }
    gfx_rect(0, 328, 640, 112, 16);
    gfx_rect(0, 0, 640, 36, 17); gfx_rect(0, 35, 640, 1, 18);
    gfx_text(20, 10, "GrapeOS", 19); gfx_text(400, 10, store_get()->hostname, 7);
    gfx_text(28, 62, "Welcome to your offline workspace", 15);
    gfx_text(28, 88, "Four apps. Your files. No internet needed.", 7);
    gfx_rect(558, 64, 4, 14, 19);
    circle(550, 84, 10, 18); circle(570, 84, 10, 19);
    circle(540, 99, 10, 19); circle(559, 101, 12, 18); circle(577, 99, 10, 19);
    circle(550, 118, 10, 18); circle(568, 118, 10, 19); circle(559, 135, 10, 18);
    for (u32 i = 0; i < APP_COUNT; ++i) {
        u32 x = 24 + i * 152;
        rounded(x + 3, 179, 136, 148, 0);
        rounded(x, 176, 136, 148, selected == i ? 18 : 22);
        rounded(x + 1, 177, 134, 146, 17);
        app_icon(x + 44, 194, (enum view)(i + 1));
        gfx_text(x + (136 - 8 * (i == 0 ? 7 : i == 1 ? 8 : i == 2 ? 5 : 10)) / 2, 266, names[i + 1], 15);
        gfx_text(x + 44, 298, shortcuts[i], 19);
    }
    gfx_text(28, 356, ramfs_persistent() ? "Your documents stay on the USB after restart." : "Tab / arrows: select     Enter: open", 7);
    gfx_text(28, 382, "Click the dock. Drag titles. Create files in Files.", 7);
    gfx_text(28, 408, keyboard_pointer ? "CURSOR: arrows move | Enter click | F8 exit" : "F8: keyboard cursor   Esc: home   Ctrl+B: minimize", 19);
#endif
}
#ifdef GRAPE_FIRMWARE
static void render_photos(struct window *w)
{
    gfx_text(w->x + 16, w->y + 44, "Your photos", 15);
    gfx_text(w->x + 16, w->y + 64, "R: refresh", 19);
    u32 first = photo_selection > 7 ? photo_selection - 7 : 0;
    for (u32 i = first; i < photo_count() && i < first + 8; ++i) {
        u32 y = w->y + 82 + (i-first) * 24;
        if (i == photo_selection) rounded(w->x + 10, y, 178, 23, 18);
        char label[21]; u32 n = 0;
        const char *name = photo_name(i); while (name[n] && n < 20) { label[n] = name[n]; ++n; } label[n] = 0;
        gfx_text(w->x + 16, y + 4, label, 15);
    }
    char number_text[12];
    number(number_text, photo_width()); gfx_text(w->x + 204, w->y + 44, number_text, 7);
    gfx_text(w->x + 256, w->y + 44, "x", 7);
    number(number_text, photo_height()); gfx_text(w->x + 276, w->y + 44, number_text, 7);
    rounded(w->x + 196, w->y + 72, 356, 252, 20);
    if (!photo_status) photo_draw(w->x + 204, w->y + 80, 340, 236);
    gfx_text(w->x + 16, w->y + w->h - 22, photo_status ? photo_status : "USB /GRAPE | Up/Down: select | R: refresh", 7);
}
#endif
static void render_note(struct window *w)
{
    u32 cursor_col, cursor_row = note_row(cursor, &cursor_col);
    u32 first = cursor_row >= NOTE_ROWS ? cursor_row - NOTE_ROWS + 1 : 0;
    u32 y = 0, x = 0;
    for (u32 i = 0; i < length; ++i) {
        if (note[i] == '\n') { ++y; x = 0; continue; }
        if (y >= first && y - first < NOTE_ROWS)
            gfx_char(w->x + 20 + x * 8, w->y + 48 + (y - first) * 16, note[i], 21);
        if (++x == NOTE_COLS) { x = 0; ++y; }
    }
    if (view == NOTEPAD && !(blink & 1))
        gfx_rect(w->x + 20 + cursor_col * 8, w->y + 48 + (cursor_row - first) * 16, 2, 16, 18);
    gfx_rect(w->x + 1, w->y + w->h - 28, w->w - 2, 27, 22);
    gfx_text(w->x + 12, w->y + w->h - 22, note_status, 21);
}
static void render_files(struct window *w)
{
    gfx_text(w->x + 20, w->y + 44, ramfs_persistent() ? "USB /GRAPE - real files" : "/ram - editable text files", 15);
    gfx_text(w->x + 20, w->y + 64, ramfs_persistent() ? "Saved across restart | Ctrl+R: rename" : "RAM files disappear on reboot", 19);
    gfx_rect(w->x + 388, w->y + 36, 144, 24, 18);
    gfx_text(w->x + 396, w->y + 40, "New [Ctrl+A]", 15);
    if (file_selection >= file_count()) file_selection = file_count() - 1;
    u32 first = file_selection > 8 ? file_selection - 8 : 0;
    for (u32 i = first; i < file_count() && i < first + 9; ++i) {
        const struct ramfile *file = file_at(i);
        u32 top = w->y + 88 + (i - first) * 24;
        if (i == file_selection) gfx_rect(w->x + 12, top, w->w - 24, 23, 18);
        gfx_text(w->x + 24, top + 4, file ? file->name : "note.txt", 15);
        gfx_text(w->x + 304, top + 4, file ? (ramfs_persistent() ? "USB file" : "RAM file") : "Profile note", 7);
        const char *text = file ? file->text : store_get()->note;
        u32 bytes = 0; while (text[bytes]) ++bytes;
        char count[12]; number(count, bytes);
        gfx_text(w->x + 440, top + 4, count, 7);
    }
    gfx_rect(w->x + 1, w->y + w->h - 28, w->w - 2, 27, 22);
    gfx_text(w->x + 12, w->y + w->h - 22, files_status, 15);
}
static void render_calculator(struct window *w)
{
    gfx_text(w->x + 20, w->y + 44, "Integer calculator", 15);
    gfx_text(w->x + 20, w->y + 68, "Example: 7 * 8", 7);
    gfx_rect(w->x + 20, w->y + 94, w->w - 40, 32, 0);
    u32 first = expression_length > 39 ? expression_length - 39 : 0;
    gfx_text(w->x + 28, w->y + 102, expression + first, 15);
    if (view == CALCULATOR && !(blink & 1))
        gfx_rect(w->x + 28 + (expression_length - first) * 8, w->y + 119, 7, 2, 19);
    for (u32 line = 0; line < 3; ++line) {
        char text[42]; u32 start = line * 41, n = 0;
        while (start + n < answer_length && n < 41) { text[n] = answer[start + n]; ++n; }
        text[n] = 0; gfx_text(w->x + 20, w->y + 136 + line * 16, text, 23);
    }
    gfx_rect(w->x + 20, w->y + 192, 176, 28, 18);
    gfx_text(w->x + 32, w->y + 198, "Enter: calculate", 15);
    gfx_text(w->x + 208, w->y + 198, "Ctrl+U: clear", 7);
}
static void render_window(enum view app)
{
    struct window *w = &windows[app];
    rounded(w->x + 4, w->y + 4, w->w, w->h, 0);
    rounded(w->x, w->y, w->w, w->h, view == app ? 19 : 22);
    gfx_rect(w->x + 1, w->y + 28, w->w - 2, w->h - 29, app == NOTEPAD ? 20 : app == TERMINAL ? 0 : 17);
    gfx_rect(w->x + 1, w->y + 1, w->w - 2, 27, view == app ? 18 : 22);
    gfx_text(w->x + 12, w->y + 6, names[app], 15);
    if (app == NOTEPAD) {
        gfx_text(w->x + 88, w->y + 6, filename, 15);
        if (changed) gfx_text(w->x + 288, w->y + 6, "* unsaved", 13);
    }
    gfx_rect(w->x + w->w - 55, w->y + 4, 24, 20, 17);
    gfx_rect(w->x + w->w - 27, w->y + 4, 23, 20, 17);
    gfx_text(w->x + w->w - 48, w->y + 6, "_  X", 15);
    if (app == NOTEPAD) render_note(w);
    else if (app == FILES) render_files(w);
    else if (app == CALCULATOR) render_calculator(w);
#ifdef GRAPE_FIRMWARE
    else if (app == PHOTOS) render_photos(w);
#endif
    else {
        for (u32 y = 0; y < ROWS; ++y)
            for (u32 x = 0; x < COLS; ++x)
            {
#ifdef GRAPE_FIRMWARE
                gfx_terminal_char(w->x + 8 + x * 7, w->y + 40 + y * 12, terminal[y][x], terminal_fg[y][x], terminal_bg[y][x]);
                if (snake_active() && y >= 4 && y <= 21 && x >= 19 && x <= 60) {
                    u32 px = w->x + 8 + x * 7, py = w->y + 40 + y * 12;
                    char c = terminal[y][x];
                    gfx_rect(px, py, 7, 12, 0);
                    if (c == '#') gfx_rect(px, py, 7, 12, 22);
                    else if (c == 'O' || c == '@') {
                        gfx_rect(px, py + 1, 6, 10, c == '@' ? 23 : 10);
                        if (c == '@') gfx_rect(px + 4, py + 3, 1, 2, 0);
                    } else if (c == '*') circle(px + 3, py + 6, 3, 12);
                }
#else
                gfx_char(w->x + 20 + x * 8, w->y + 44 + y * 16, terminal[y][x], terminal_fg[y][x]);
#endif
            }
        if (view == TERMINAL && !apps_active() && !(blink & 1))
#ifdef GRAPE_FIRMWARE
            gfx_rect(w->x + 8 + column * 7, w->y + 50 + row * 12, 6, 2, 23);
#else
            gfx_rect(w->x + 20 + column * 8, w->y + 58 + row * 16, 7, 2, 23);
#endif
    }
}
static void render_dialog(void)
{
    rounded(132, 148, 384, 184, 0);
    rounded(128, 144, 384, 184, 22);
    gfx_rect(132, 180, 376, 144, 17);
    gfx_text(148, 154, file_dialog == 1 ? "Create a new text file" : "Rename selected file", 15);
    gfx_text(148, 188, "Name (up to 23 characters):", 7);
    gfx_rect(148, 214, 336, 30, 0);
    gfx_text(156, 221, dialog_name, 15);
    if (!(blink & 1)) gfx_rect(156 + dialog_length * 8, 237, 7, 2, 19);
    rounded(156, 262, 144, 30, 18); gfx_text(176, 270, "Enter: save", 15);
    rounded(324, 262, 144, 30, 22); gfx_text(340, 270, "Esc: cancel", 15);
    gfx_text(144, 305, files_status, 7);
}
static void render_pointer(void)
{
    u32 x = (u32)pointer.x, y = (u32)pointer.y;
    struct window *w = &windows[view];
    if (view == NOTEPAD && inside(pointer.x, pointer.y, w->x + 16, w->y + 40, w->w - 32, w->h - 80)) {
        gfx_rect(x, y, 9, 3, 0); gfx_rect(x, y + 18, 9, 3, 0);
        gfx_rect(x + 3, y, 3, 21, 0);
        gfx_rect(x + 1, y + 1, 7, 1, 15); gfx_rect(x + 1, y + 19, 7, 1, 15);
        gfx_rect(x + 4, y + 1, 1, 19, 15);
        return;
    }
    static const u16 arrow[22] = {
        0x8000,0xc000,0xe000,0xf000,0xf800,0xfc00,0xfe00,0xff00,
        0xff80,0xffc0,0xffe0,0xfff0,0xfff8,0xffc0,0xdc00,0x8e00,
        0x0600,0x0700,0x0300,0x0380,0x0180,0x0080
    };
    for (u32 r = 0; r < 22; ++r)
        for (u32 c = 0; c < 16; ++c)
            if (arrow[r] & (0x8000u >> c)) gfx_rect(x + c + 1, y + r + 1, 1, 1, 0);
    for (u32 r = 0; r < 22; ++r)
        for (u32 c = 0; c < 16; ++c) {
            u16 bit = (u16)(0x8000u >> c);
            if (!(arrow[r] & bit)) continue;
            bool inner = r && r < 21 && c && c < 15 &&
                (arrow[r - 1] & bit) && (arrow[r + 1] & bit) &&
                (arrow[r] & (bit << 1)) && (arrow[r] & (bit >> 1));
            gfx_rect(x + c, y + r, 1, 1, inner ? 15 : 0);
        }
}
void gui_tick(void)
{
    if (!running) return;
    mouse_tick();
    u32 now = timer_ticks() / 50;
    if (now != blink) { blink = now; if (view != DESKTOP) dirty = true; }
#ifdef GRAPE_FIRMWARE
    u32 second = timer_ticks() / TIMER_HZ;
    if (second != clock_second) { clock_second = second; dirty = true; }
    if (!dirty || (u32)(timer_ticks() - last_draw) < 2) return;
#else
    if (!dirty || (u32)(timer_ticks() - last_draw) < 5) return;
#endif
    last_draw = timer_ticks(); dirty = false;
    render_desktop();
    for (u32 i = 0; i < APP_COUNT; ++i) if (windows[order[i]].visible) render_window(order[i]);
    gfx_rect(0, 440, 640, 40, 17); gfx_rect(0, 440, 640, 1, 18);
    for (u32 i = 0; i < APP_COUNT; ++i) {
        enum view app = (enum view)(i + 1); u32 x = dock_x(i), width = dock_width();
        rounded(x, 445, width, 29, view == app ? 18 : 22);
        gfx_text(x + 8, 452, names[app], 15);
        if (windows[app].visible) gfx_rect(x + 12, 472, width - 24, 2, 19);
    }
    if (file_dialog) render_dialog();
    render_pointer();
    gfx_present();
}
