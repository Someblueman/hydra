#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Keyboard help: one "key  what it does" entry per row, grouped by where the
 * keys apply. Descriptions wrap under themselves instead of being cut, wide
 * views set two entries side by side, and j/k, Space/b scroll what does not
 * fit (any other key closes help and acts, as before). */

#define HELP_SECTION "#"
static const char *const help_rows[][2] = {
    {HELP_SECTION, "MOVE AROUND"},
    {"Tab / Shift-Tab", "next / previous tab; inside Workspace, next / previous pane"},
    {"Left / Right", "previous / next tab"},
    {"1-9", "jump to a tab"},
    {"Up / Down, j / k", "select"},
    {"Enter", "open the selection"},
    {"Esc", "one step back (details to list, close help, clear search)"},
    {HELP_SECTION, "WORK, LOCAL AND REMOTE"},
    {"n", "start a new task"},
    {"a", "talk to the selected agent (local or remote)"},
    {"x", "remove selected or marked heads; on a remote head, interrupt its agent (confirmed)"},
    {"L", "location: all / local / remote / one host"},
    {"r", "retry a disconnected host"},
    {"/", "search"},
    {":", "more actions (diff, switch, approvals...)"},
    {"p", "recent terminal output"},
    {"d", "technical details"},
    {"c", "coordination"},
    {"Enter / l / h", "expand or collapse a run; its heads are listed inside it"},
    {"Y / N / R / X", "on a remote task row: approve, reject, resume, cancel (fresh evidence only)"},
    {HELP_SECTION, "ATTENTION AND REVIEW"},
    {"I", "attention: what needs a decision or a look from you"},
    {"Enter", "an item's detail; Esc returns to the list with the selection kept"},
    {"r", "review the item's exact evidence (read-only)"},
    {"s", "mark seen or new again; seen never approves anything"},
    {"n / N", "next / previous section of a review"},
    {"Space / b", "page down / up"},
    {"L", "the full log"},
    {"f", "references"},
    {"i", "the selected identity and its digests"},
    {HELP_SECTION, "WORKSPACE"},
    {"A", "conversation"},
    {"B", "plan overview"},
    {"C", "monitor"},
    {"z", "zoom the focused pane"},
    {"S", "two agents"},
    {"Ctrl-B Tab", "leave agent input (while typing, Esc and Tab go to the agent)"},
    {"Ctrl-B x", "close the view; the agent keeps running and a reopens it"},
    {"Ctrl-B n", "switch agent"},
    {"Ctrl-B [", "scroll history"},
    {"Ctrl-B q", "quit"},
    {HELP_SECTION, "PLANNING"},
    {"P", "review the agent's proposal"},
    {"V", "validate"},
    {"F", "request changes (feedback to the agent)"},
    {"E", "execute the validated revision (confirm with y)"},
    {"I", "import draft and policy files"},
    {HELP_SECTION, "OTHER"},
    {"H", "hosts: Enter follows a host's work, r retries it, A adds a machine over SSH"},
    {"D", "statistics"},
    {"t", "theme (terminal / dark / light)"},
    {"?", "close help"},
    {"q", "quit"},
};
#define HELP_ROWS (sizeof(help_rows) / sizeof(help_rows[0]))
#define HELP_KEY_WIDTH 18

static bool help_section(size_t index) { return !strcmp(help_rows[index][0], HELP_SECTION); }

/* Rows one entry needs at this column width. */
static size_t help_entry_rows(size_t index, int width) {
    return help_section(index) ? 2U : wrapped_rows(help_rows[index][1], width > HELP_KEY_WIDTH ? (size_t)(width - HELP_KEY_WIDTH) : 1U);
}

/* One entry at column x (relative to the content) within width columns. */
static void help_entry(struct app *app, size_t index, int x, int width, size_t *skip) {
    int saved_x = app->content_x, saved_width = app->content_width;
    app->content_x += x; app->content_width = width;
    labelled_skip(app, HELP_KEY_WIDTH, help_rows[index][0], help_rows[index][1], TV_BASE, skip);
    app->content_x = saved_x; app->content_width = saved_width;
}

static void help_heading(struct app *app, size_t index, size_t *skip) {
    if (*skip) { (*skip)--; } else if (app->line < app->limit) linef(app, "");
    if (*skip) { (*skip)--; return; }
    if (app->line < app->limit) { style(app, TONE_BORDER); linef(app, "%s", help_rows[index][1]); style(app, TONE_BASE); }
}

/* The total rows the help needs, so scrolling stops at its end. */
static size_t help_total_rows(int width, bool pairs) {
    size_t index = 0U, rows = 0U;
    while (index < HELP_ROWS) {
        size_t left, right;
        if (help_section(index)) { rows += 2U; index++; continue; }
        left = help_entry_rows(index, pairs ? width / 2 - 1 : width);
        if (pairs && index + 1U < HELP_ROWS && !help_section(index + 1U)) {
            right = help_entry_rows(index + 1U, width / 2 - 1);
            rows += left > right ? left : right; index += 2U;
        } else { rows += left; index++; }
    }
    return rows;
}

void render_help(struct app *app) {
    bool pairs = app->content_width >= 110;
    int half = app->content_width / 2 - 1;
    size_t index = 0U, skip, total, visible;
    style(app, TONE_STRONG); linef(app, "KEYBOARD HELP"); style(app, TONE_BASE);
    total = help_total_rows(app->content_width, pairs);
    visible = app->limit > app->line ? (size_t)(app->limit - app->line) : 1U;
    if (app->help_scroll + visible > total) app->help_scroll = total > visible ? total - visible : 0U;
    skip = app->help_scroll;
    while (index < HELP_ROWS && app->line < app->limit) {
        if (help_section(index)) { help_heading(app, index, &skip); index++; continue; }
        if (pairs && index + 1U < HELP_ROWS && !help_section(index + 1U)) {
            int top = app->line;
            size_t left_skip = skip, right_skip = skip, rows = help_entry_rows(index, half), right = help_entry_rows(index + 1U, half);
            if (right > rows) rows = right;
            help_entry(app, index, 0, half, &left_skip);
            app->line = top;
            help_entry(app, index + 1U, half + 2, half, &right_skip);
            app->line = top + (int)(rows > skip ? rows - skip : 0U);
            if (app->line > app->limit) app->line = app->limit;
            skip = skip > rows ? skip - rows : 0U;
            index += 2U;
            continue;
        }
        help_entry(app, index, 0, app->content_width, &skip);
        index++;
    }
}

/* j/k and Space/b scroll the help; any other key closes it. */
bool help_scroll_key(struct app *app, char key) {
    size_t page = app->rows > 12 ? (size_t)app->rows / 2U : 6U;
    if (key == 'j') app->help_scroll++;
    else if (key == 'k') { if (app->help_scroll) app->help_scroll--; }
    else if (key == ' ') app->help_scroll += page;
    else if (key == 'b') app->help_scroll = app->help_scroll > page ? app->help_scroll - page : 0U;
    else return false;
    return true;
}
