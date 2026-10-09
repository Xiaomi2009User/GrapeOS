#include "console.h"
#include "notes.h"
#include "store.h"

static char *skip_space(char *text)
{
    while (*text == ' ' || *text == '\t')
        ++text;
    return text;
}

static bool token_is(const char *text, const char *word)
{
    while (*word && *text == *word) {
        ++text;
        ++word;
    }
    return !*word && (!*text || *text == ' ' || *text == '\t');
}

static void save_note(struct store_state *candidate, const char *success)
{
    if (store_save(candidate)) {
        console_write(success);
    } else if (!store_available()) {
        console_write(success);
        console_write("Changes are session only; no data disk is available.\n");
    } else {
        console_write("notes: save failed; the previous note was preserved.\n");
    }
}

void notes_command(char *arguments)
{
    arguments = skip_space(arguments);
    if (!*arguments) {
        const char *note = store_get()->note;
        if (!*note) {
            console_write("Notes are empty.\n");
        } else {
            console_write("Note: ");
            console_write(note);
            console_putc('\n');
        }
        return;
    }
    if (token_is(arguments, "set")) {
        char *text = skip_space(arguments + 3);
        if (!*text) {
            console_write("notes: expected notes set <text> or notes clear.\n");
            return;
        }
        u32 length = 0;
        while (text[length]) {
            if (length == sizeof(store_get()->note) - 1) {
                console_write("notes: maximum 255 characters.\n");
                return;
            }
            ++length;
        }
        struct store_state candidate = *store_get();
        for (u32 index = 0; index <= length; ++index)
            candidate.note[index] = text[index];
        save_note(&candidate, "Note saved.\n");
    } else if (token_is(arguments, "clear") && !*skip_space(arguments + 5)) {
        struct store_state candidate = *store_get();
        candidate.note[0] = '\0';
        save_note(&candidate, "Notes cleared.\n");
    } else {
        console_write("notes: expected notes set <text> or notes clear.\n");
    }
}
