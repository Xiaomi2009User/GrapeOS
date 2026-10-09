#include "console.h"
#include "platform.h"
#include "snake.h"
#include "store.h"

#define BOARD_WIDTH 40u
#define BOARD_HEIGHT 16u
#define BOARD_CELLS (BOARD_WIDTH * BOARD_HEIGHT)
#define BOARD_LEFT 19u
#define BOARD_TOP 4u
#define STEP_TICKS 20u

enum direction { UP, DOWN, LEFT, RIGHT };
static u16 body[BOARD_CELLS];
static u32 length, score, best, last_tick, random_state;
static u16 food;
static enum direction direction, next_direction;
static bool active, paused, over, won;

static void draw_text(u32 x, u32 y, const char *text, u8 color)
{
    while (*text && x < 80)
        console_draw(x++, y, *text++, color, 0);
}

static void draw_number(u32 x, u32 y, u32 value, u8 color)
{
    char digits[10];
    u32 count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count)
        console_draw(x++, y, digits[--count], color, 0);
}

static void serial_number(u32 value)
{
    char text[11];
    u32 count = 0;
    do {
        text[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    for (u32 left = 0, right = count - 1; left < right; ++left, --right) {
        char temporary = text[left];
        text[left] = text[right];
        text[right] = temporary;
    }
    text[count] = '\0';
    console_serial_write(text);
}

static bool occupied(u16 cell, u32 count)
{
    for (u32 index = 0; index < count; ++index)
        if (body[index] == cell)
            return true;
    return false;
}

static void new_food(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    u32 start = random_state % BOARD_CELLS;
    /* Bounded linear search also works when only one empty cell remains. */
    for (u32 offset = 0; offset < BOARD_CELLS; ++offset) {
        u16 cell = (u16)((start + offset) % BOARD_CELLS);
        if (!occupied(cell, length)) {
            food = cell;
            return;
        }
    }
}

static void draw_status(void)
{
    for (u32 x = 0; x < 80; ++x) {
        console_draw(x, 1, ' ', 7, 0);
        console_draw(x, 23, ' ', 7, 0);
    }
    draw_text(19, 1, "Score: ", 10);
    draw_number(26, 1, score, 10);
    draw_text(39, 1, "Best: ", 14);
    draw_number(45, 1, best, 14);
    if (won)
        draw_text(19, 23, "Board complete! R restarts; Q quits.", 14);
    else if (over)
        draw_text(19, 23, "Game over. R restarts; Q quits.", 12);
    else if (paused)
        draw_text(19, 23, "Paused. Press P to continue.", 14);
    else
        draw_text(19, 23, "Eat * and avoid walls and your tail.", 7);
}

static void draw_board(void)
{
    for (u32 y = 0; y < BOARD_HEIGHT; ++y)
        for (u32 x = 0; x < BOARD_WIDTH; ++x)
            console_draw(BOARD_LEFT + 1 + x, BOARD_TOP + 1 + y, ' ', 7, 0);
    for (u32 x = 0; x < BOARD_WIDTH + 2; ++x) {
        console_draw(BOARD_LEFT + x, BOARD_TOP, '#', 8, 0);
        console_draw(BOARD_LEFT + x, BOARD_TOP + BOARD_HEIGHT + 1, '#', 8, 0);
    }
    for (u32 y = 1; y <= BOARD_HEIGHT; ++y) {
        console_draw(BOARD_LEFT, BOARD_TOP + y, '#', 8, 0);
        console_draw(BOARD_LEFT + BOARD_WIDTH + 1, BOARD_TOP + y, '#', 8, 0);
    }
    if (!won)
        console_draw(BOARD_LEFT + 1 + food % BOARD_WIDTH,
                     BOARD_TOP + 1 + food / BOARD_WIDTH, '*', 12, 0);
    for (u32 index = 0; index < length; ++index)
        console_draw(BOARD_LEFT + 1 + body[index] % BOARD_WIDTH,
                     BOARD_TOP + 1 + body[index] / BOARD_WIDTH,
                     index ? 'O' : '@', index ? 10 : 14, 0);
    draw_status();
}

static void save_best(void)
{
    if (best <= store_get()->snake_best)
        return;
    struct store_state candidate = *store_get();
    candidate.snake_best = best;
    if (store_save(&candidate))
        console_serial_write("Snake high score saved.\n");
    else if (!store_available())
        console_serial_write("Snake high score kept for this session.\n");
    else
        console_serial_write("Snake high score could not be saved.\n");
}

static void restart(void)
{
    length = 3;
    score = 0;
    direction = next_direction = RIGHT;
    paused = over = won = false;
    body[0] = (BOARD_HEIGHT / 2) * BOARD_WIDTH + 10;
    body[1] = body[0] - 1;
    body[2] = body[0] - 2;
    food = body[0] + 5;
    random_state = 0x47524150u;
    last_tick = timer_ticks();
    draw_board();
}

bool snake_start(char *arguments)
{
    while (*arguments == ' ' || *arguments == '\t')
        ++arguments;
    if (*arguments) {
        console_write("snake: no arguments expected.\n");
        return false;
    }
    active = true;
    best = store_get()->snake_best;
    console_color(7, 0);
    console_clear();
    console_cursor_visible(false);
    console_serial_write("Snake started\n"
                         "Arrows/WASD move; P pauses; R restarts; Q/Esc quits.\n");
    draw_text(19, 0, "G R A P E M A R K E T   /   S N A K E", 13);
    draw_text(4, 2, "Arrows/WASD move | P pause | R restart | Q/Esc quit", 7);
    restart();
    return true;
}

void snake_input(int key)
{
    if (!active)
        return;
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';
    if (key == 'q' || key == 0x1b || key == 0x03) {
        save_best();
        active = false;
        console_color(store_get()->foreground, store_get()->background);
        console_clear();
        console_cursor_visible(true);
        console_write("Snake closed\n");
        return;
    }
    if (key == 'r') {
        save_best();
        restart();
        console_serial_write("Snake restarted\n");
        return;
    }
    if (over)
        return;
    if (key == 'p') {
        paused = !paused;
        last_tick = timer_ticks();
        console_serial_write(paused ? "Snake paused\n" : "Snake resumed\n");
        draw_status();
        return;
    }
    enum direction requested;
    if (key == KEY_UP || key == 'w')
        requested = UP;
    else if (key == KEY_DOWN || key == 's')
        requested = DOWN;
    else if (key == KEY_LEFT || key == 'a')
        requested = LEFT;
    else if (key == KEY_RIGHT || key == 'd')
        requested = RIGHT;
    else
        return;
    /* Compare to the actual last step, so rapid turns cannot reverse it. */
    if ((requested == UP && direction != DOWN)
        || (requested == DOWN && direction != UP)
        || (requested == LEFT && direction != RIGHT)
        || (requested == RIGHT && direction != LEFT))
        next_direction = requested;
}

static void game_over(bool completed)
{
    over = true;
    won = completed;
    save_best();
    console_serial_write(completed ? "Snake board complete! Score: " : "Game over. Score: ");
    serial_number(score);
    console_serial_write(". R restarts; Q quits.\n");
    draw_board();
}

void snake_tick(void)
{
    if (!active || paused || over)
        return;
    u32 now = timer_ticks();
    if (now - last_tick < STEP_TICKS)
        return;
    last_tick = now;
    i32 x = (i32)(body[0] % BOARD_WIDTH);
    i32 y = (i32)(body[0] / BOARD_WIDTH);
    direction = next_direction;
    if (direction == UP)
        --y;
    else if (direction == DOWN)
        ++y;
    else if (direction == LEFT)
        --x;
    else
        ++x;
    if (x < 0 || x >= (i32)BOARD_WIDTH || y < 0 || y >= (i32)BOARD_HEIGHT) {
        game_over(false);
        return;
    }
    u16 next = (u16)((u32)y * BOARD_WIDTH + (u32)x);
    bool growing = next == food;
    if (occupied(next, length - (growing ? 0u : 1u))) {
        game_over(false);
        return;
    }
    if (growing)
        ++length;
    for (u32 index = length - 1; index > 0; --index)
        body[index] = body[index - 1];
    body[0] = next;
    if (growing) {
        ++score;
        if (score > best)
            best = score;
        console_serial_write("Snake score: ");
        serial_number(score);
        console_serial_write("\n");
        if (length == BOARD_CELLS) {
            game_over(true);
            return;
        }
        new_food();
    }
    draw_board();
}

bool snake_active(void)
{
    return active;
}
