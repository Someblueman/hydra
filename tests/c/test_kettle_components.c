#include "kettle/component_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The components' pure parts: fuzzy matching and ranking, wrapping, chord
 * hints, the layout of generated help, scroll arithmetic at the edges, and
 * views drawn into a caller canvas from hand-made messages. Interaction under
 * the runtime is in test_kettle_widgets.c. */

static int failures;

static void check(bool ok, const char *what, int line) {
    if (ok) return;
    fprintf(stderr, "tests/c/test_kettle_components.c:%d: %s\n", line, what);
    failures++;
}
#define CHECK(condition) check((condition), #condition, __LINE__)

/* ---- a canvas to draw on, read back as text ----------------------------- */

#define WIDTH 140
#define HEIGHT 30
static struct kt_cell cells[WIDTH * HEIGHT];
static struct kt_canvas canvas;
static char row_buffer[WIDTH * 16];

static void blank(int width, int height, bool unicode) {
    CHECK(kt_init(&canvas, cells, sizeof(cells) / sizeof(cells[0]), width, height, unicode));
    kt_clear(&canvas, KT_BASE);
}

static const char *row(int y) {
    size_t used = 0;
    int x;
    for (x = 0; x < canvas.width; x++) {
        const struct kt_cell *c = &canvas.cells[y * canvas.stride + x];
        unsigned i;
        if (!c->width) continue;
        used += kt_utf8_encode(c->glyph, row_buffer + used);
        for (i = 0; i < c->combining_count; i++) used += kt_utf8_encode(c->combining[i], row_buffer + used);
    }
    while (used && row_buffer[used - 1] == ' ') used--;
    row_buffer[used] = '\0';
    return row_buffer;
}

static const struct kt_cell *cell(int x, int y) { return &canvas.cells[y * canvas.stride + x]; }

static struct kt_msg key(uint32_t code, unsigned mods) {
    struct kt_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = KT_MSG_KEY;
    msg.u.event.type = KT_KEY;
    msg.u.event.key = code;
    msg.u.event.mods = mods;
    return msg;
}

static struct kt_msg mouse(int x, int y, unsigned button) {
    struct kt_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = KT_MSG_MOUSE;
    msg.u.event.type = KT_MOUSE;
    msg.u.event.x = x;
    msg.u.event.y = y;
    msg.u.event.button = button;
    return msg;
}

/* ---- fuzzy matching ----------------------------------------------------- */

static void fuzzy(void) {
    size_t p[KT_FUZZY_POSITIONS];
    CHECK(kt_fuzzy("", "anything", NULL, 0) == 0);
    CHECK(kt_fuzzy("xyz", "feature-live", NULL, 0) == -1);
    CHECK(kt_fuzzy("fl", "feature-live", p, KT_FUZZY_POSITIONS) > 0);
    /* The shortest span ending where the earliest match ends: "f" of
     * feature, "l" of live. */
    CHECK(p[0] == 0 && p[1] == 8);
    /* Case is ignored and blanks in the pattern are skipped. */
    CHECK(kt_fuzzy("F L", "feature-live", p, KT_FUZZY_POSITIONS) > 0 && p[1] == 8);
    /* Word starts and adjacency outrank scattered characters. */
    CHECK(kt_fuzzy("nt", "new task", NULL, 0) > kt_fuzzy("nt", "maintenance", NULL, 0));
    CHECK(kt_fuzzy("rem", "remove", NULL, 0) > kt_fuzzy("rem", "frame mode", NULL, 0));
    CHECK(kt_fuzzy("ab", "ab", NULL, 0) > kt_fuzzy("ab", "a-b", NULL, 0));
    /* Multibyte text: positions are byte offsets of the matched scalars. */
    CHECK(kt_fuzzy("x", "\xe7\x95\x8c-x", p, 1) > 0 && p[0] == 4);
    /* Capacity bounds what is written, not whether it matches. */
    p[1] = 77;
    CHECK(kt_fuzzy("abc", "abc", p, 1) > 0 && p[0] == 0 && p[1] == 77);
    CHECK(kt_fuzzy(NULL, "a", NULL, 0) == -1 && kt_fuzzy("a", NULL, NULL, 0) == -1);
}

static void ranking(void) {
    struct kt_match m[] = {{0, 5}, {1, 9}, {2, 5}, {3, 1}, {4, 9}, {5, 7}};
    struct kt_match one[] = {{3, 3}};
    size_t expect[] = {1, 4, 5, 0, 2, 3}, i;
    kt_fuzzy_rank(m, 6);
    for (i = 0; i < 6; i++) CHECK(m[i].index == expect[i]);
    kt_fuzzy_rank(one, 1);
    kt_fuzzy_rank(NULL, 0);
    CHECK(one[0].index == 3);
}

/* ---- text ---------------------------------------------------------------- */

static void text(void) {
    int width;
    CHECK(kt_text_fit("hello", 5, 3, &width) == 3 && width == 3);
    CHECK(kt_text_fit("\xe7\x95\x8c\xe9\x9d\xa2", 6, 3, &width) == 3 && width == 2);
    CHECK(kt_text_fit("e\xcc\x81x", 4, 1, &width) == 3 && width == 1);
    CHECK(kt_text_fit("abc", 2, 10, &width) == 2 && width == 2);
    CHECK(kt_text_fit("abc", 3, -1, &width) == 0 && width == 0);
    CHECK(kt_paragraph_height("Closes its terminal and deletes the worktree.", 20) == 3);
    CHECK(kt_paragraph_height("", 10) == 1);
    CHECK(kt_paragraph_height("a\n\nb", 10) == 3);
    CHECK(kt_paragraph_height("abcdefghij", 4) == 3);
    blank(24, 4, true);
    CHECK(kt_paragraph(&canvas, (struct kt_rect){1, 0, 20, 2}, "Closes its terminal and deletes the worktree.", KT_BASE) == 2);
    CHECK(!strcmp(row(0), " Closes its terminal"));
    CHECK(!strcmp(row(1), " and deletes the"));
    CHECK(!strcmp(row(2), ""));
}

static void hints(void) {
    struct kt_chord chord;
    char out[32];
    CHECK(kt_chord_parse("up", 2, &chord) && kt_chord_hint(&chord, out, sizeof(out)) == 3 && !strcmp(out, "\xe2\x86\x91"));
    CHECK(kt_chord_parse("enter", 5, &chord) && kt_chord_hint(&chord, out, sizeof(out)) == 5 && !strcmp(out, "enter"));
    CHECK(kt_chord_parse("ctrl+b tab", 10, &chord) && kt_chord_hint(&chord, out, sizeof(out)) == 10 &&
          !strcmp(out, "ctrl+b tab"));
    CHECK(kt_chord_parse("shift+tab", 9, &chord) && kt_chord_hint(&chord, out, sizeof(out)) && !strcmp(out, "shift+tab"));
    CHECK(kt_chord_parse("pgdn", 4, &chord) && kt_chord_hint(&chord, out, sizeof(out)) && !strcmp(out, "pgdn"));
    CHECK(kt_chord_parse("G", 1, &chord) && kt_chord_hint(&chord, out, sizeof(out)) && !strcmp(out, "G"));
    CHECK(kt_chord_parse("ctrl+]", 6, &chord) && kt_chord_hint(&chord, out, 4) == 6 && !strcmp(out, ""));
    CHECK(kt_chord_hint(&chord, out, 6) == 6 && !strcmp(out, "ctrl+"));
}

/* ---- help ---------------------------------------------------------------- */

enum { GO, BACK, NEW, REMOVE, AGENT, SEARCH, QUIT, HIDDEN, JUMP1, JUMP2, JUMP3, ACTIONS };
static struct kt_binding app[ACTIONS];

static void bind(size_t action, const char *name, const char *label, const char *group, int priority,
                 const char *chords) {
    struct kt_keymap map = {app, ACTIONS};
    char config[96], error[96];
    app[action].name = name;
    app[action].label = label;
    app[action].action = (uint32_t)action;
    app[action].scope = 1;
    app[action].group = group;
    app[action].priority = priority;
    snprintf(config, sizeof(config), "%s = %s\n", name, chords);
    CHECK(kt_keymap_configure(&map, config, strlen(config), error, sizeof(error)));
}

static void app_keys(void) {
    memset(app, 0, sizeof(app));
    bind(GO, "go", "open", "Move", 5, "enter");
    bind(BACK, "back", "back", "Move", 0, "esc");
    bind(NEW, "new", "new task", "Work", 2, "n");
    bind(REMOVE, "remove", "remove", "Work", 1, "x");
    bind(AGENT, "agent", "agent", "Work", 2, "a");
    bind(SEARCH, "search", "search", "Move", 0, "/");
    bind(QUIT, "quit", "quit", NULL, 9, "q");
    bind(HIDDEN, "hidden", "only in full", "Work", -1, "ctrl+x");
    bind(JUMP1, "jump1", "jump to tab", "Move", 0, "1");
    bind(JUMP2, "jump2", "jump to tab", "Move", 0, "2");
    bind(JUMP3, "jump3", "jump to tab", "Move", 0, "3");
}

static void help_short(void) {
    struct kt_help help;
    struct kt_list list;
    struct kt_help_source sources[2];
    const char *full_line = "\xe2\x86\x91/\xe2\x86\x93 select \xe2\x80\xa2 enter open \xe2\x80\xa2 / filter \xe2\x80\xa2 "
                            "enter open \xe2\x80\xa2 esc back \xe2\x80\xa2 n new task \xe2\x80\xa2 x remove \xe2\x80\xa2 "
                            "a agent \xe2\x80\xa2 / search \xe2\x80\xa2 q quit \xe2\x80\xa2 1\xe2\x80\x93" "3 jump to tab";
    int full_width = kt_text_width(full_line), width;
    static struct kt_match matches[4];
    app_keys();
    kt_list_init(&list, NULL, NULL, 0);
    kt_list_filter_storage(&list, matches, 4);
    kt_help_init(&help);
    sources[0].bindings = list.keys; sources[0].count = KT_LIST_ACTIONS; sources[0].scope = kt_list_scope(&list);
    sources[1].bindings = app; sources[1].count = ACTIONS; sources[1].scope = 1;
    CHECK(kt_help_set(&help, sources, 2));
    /* Adjacent rows with one label are one entry; three or more are a range;
     * a negative priority stays out. */
    blank(WIDTH, 2, true);
    CHECK(kt_help_view(&help, &canvas, (struct kt_rect){0, 0, WIDTH, 1}) == full_width);
    CHECK(!strcmp(row(0), full_line));
    /* Keys bold, labels muted, separators faint. */
    CHECK(cell(0, 0)->attributes == KT_BOLD && cell(4, 0)->foreground == KT_COLOR_MUTED);
    CHECK(cell(11, 0)->foreground == KT_COLOR_FAINT);
    /* One column short: the lowest priority goes, the latest of equals first,
     * and "? more" ends the line. Never part of an entry. */
    blank(WIDTH, 2, true);
    width = kt_help_view(&help, &canvas, (struct kt_rect){0, 0, full_width - 1, 1});
    CHECK(width <= full_width - 1);
    CHECK(strstr(row(0), "? more") && !strstr(row(0), "jump") && strstr(row(0), "q quit"));
    CHECK(strstr(row(0), "/ search") && strstr(row(0), "esc back"));
    /* Narrow: what is left is still whole entries, highest priority first. */
    for (width = 1; width < full_width; width++) {
        const char *text;
        blank(WIDTH, 2, true);
        CHECK(kt_help_view(&help, &canvas, (struct kt_rect){0, 0, width, 1}) <= width);
        text = row(0);
        CHECK(!strstr(text, "\xe2\x80\xa6"));
        if (width >= 30) CHECK(strstr(text, "q quit \xe2\x80\xa2 ? more") != NULL);
        if (width >= 45) CHECK(strstr(text, "\xe2\x86\x91/\xe2\x86\x93 select") != NULL);
    }
    blank(WIDTH, 2, true);
    CHECK(kt_help_view(&help, &canvas, (struct kt_rect){0, 0, 6, 1}) == 6 && !strcmp(row(0), "? more"));
    blank(WIDTH, 2, true);
    CHECK(kt_help_view(&help, &canvas, (struct kt_rect){0, 0, 5, 1}) == 0);
    /* The help's own toggle, listed, gives way to "more" first. */
    sources[0].bindings = help.keys; sources[0].count = KT_HELP_ACTIONS; sources[0].scope = kt_help_scope(&help);
    CHECK(kt_help_set(&help, sources, 2));
    blank(WIDTH, 2, true);
    (void)kt_help_view(&help, &canvas, (struct kt_rect){0, 0, WIDTH, 1});
    CHECK(!strncmp(row(0), "? help \xe2\x80\xa2 enter open", 17));
    blank(WIDTH, 2, true);
    (void)kt_help_view(&help, &canvas, (struct kt_rect){0, 0, 60, 1});
    CHECK(!strstr(row(0), "? help") && strstr(row(0), "? more"));
    /* ASCII canvases separate with a bar. */
    blank(WIDTH, 2, false);
    (void)kt_help_view(&help, &canvas, (struct kt_rect){0, 0, WIDTH, 1});
    CHECK(!strncmp(row(0), "? help | enter open | esc back", 30));
}

static void help_full(void) {
    struct kt_help help;
    struct kt_help_source source = {app, ACTIONS, 1};
    int height;
    app_keys();
    kt_help_init(&help);
    CHECK(kt_help_set(&help, &source, 1));
    /* Groups in order of first appearance, each in columns filled top to
     * bottom, key columns aligned across groups. */
    height = kt_help_full_height(&help, 80);
    CHECK(height == 9);
    CHECK(kt_help_full_width(&help, 2) == 2 * (6 + 3 + 12) + 6);
    blank(80, 12, true);
    CHECK(kt_help_full_view(&help, &canvas, (struct kt_rect){2, 1, 76, 11}) == height);
    CHECK(!strcmp(row(1), "  Move"));
    CHECK(!strcmp(row(2), "  enter    open              /        search"));
    CHECK(!strcmp(row(3), "  esc      back              1\xe2\x80\x93" "3      jump to tab"));
    CHECK(!strcmp(row(5), "  Work"));
    CHECK(!strcmp(row(6), "  n        new task          a        agent"));
    CHECK(!strcmp(row(7), "  x        remove            ctrl+x   only in full"));
    CHECK(!strcmp(row(9), "  q        quit"));
    CHECK(cell(2, 1)->foreground == KT_COLOR_ACCENT && (cell(2, 1)->attributes & KT_BOLD));
    /* One column when narrow. */
    CHECK(kt_help_full_height(&help, 30) == 2 + 4 + 1 + 4 + 1 + 1);
    /* Toggling. */
    {
        struct kt_msg q = key('?', 0), esc = key(KT_KEY_ESCAPE, 0);
        CHECK(kt_help_update(&help, &esc, NULL) == KT_HELP_IGNORED);
        CHECK(kt_help_update(&help, &q, NULL) == KT_HELP_OPENED && help.full);
        CHECK(kt_help_update(&help, &esc, NULL) == KT_HELP_CLOSED && !help.full);
        CHECK(kt_help_update(&help, &q, NULL) == KT_HELP_OPENED);
        CHECK(kt_help_update(&help, &q, NULL) == KT_HELP_CLOSED);
    }
}

/* ---- scroll arithmetic --------------------------------------------------- */

static int tall(const void *context, size_t row) { (void)context; return row % 3 == 0 ? 1 : 2; }

static void scrolling(void) {
    struct kt_ui_rows none = {0, NULL, NULL}, ten = {10, NULL, NULL}, mixed = {9, tall, NULL};
    /* Empty. */
    CHECK(kt_ui_bottom_top(&none, 5) == 0 && kt_ui_rows_shown(&none, 0, 5) == 0);
    CHECK(kt_ui_scroll_by(&none, 0, 3, 5) == 0 && kt_ui_scroll_to(&none, 0, 0, 5) == 0);
    CHECK(kt_ui_row_at(&none, 0, 0, 5) == KT_NONE);
    /* Uniform rows: the last full page, clamped scrolling either way. */
    CHECK(kt_ui_bottom_top(&ten, 4) == 6 && kt_ui_bottom_top(&ten, 10) == 0 && kt_ui_bottom_top(&ten, 20) == 0);
    CHECK(kt_ui_bottom_top(&ten, 0) == 9);
    CHECK(kt_ui_scroll_by(&ten, 0, -1, 4) == 0 && kt_ui_scroll_by(&ten, 5, 100, 4) == 6);
    CHECK(kt_ui_scroll_by(&ten, 2, -100, 4) == 0 && kt_ui_scroll_by(&ten, 8, 1, 4) == 6);
    CHECK(kt_ui_scroll_to(&ten, 0, 3, 4) == 0 && kt_ui_scroll_to(&ten, 0, 4, 4) == 1);
    CHECK(kt_ui_scroll_to(&ten, 5, 2, 4) == 2 && kt_ui_scroll_to(&ten, 0, 9, 4) == 6);
    CHECK(kt_ui_scroll_to(&ten, 9, 99, 4) == 6);
    CHECK(kt_ui_rows_shown(&ten, 8, 4) == 2 && kt_ui_row_at(&ten, 8, 1, 4) == 9 && kt_ui_row_at(&ten, 8, 2, 4) == KT_NONE);
    CHECK(kt_ui_row_at(&ten, 0, -1, 4) == KT_NONE && kt_ui_row_at(&ten, 0, 4, 4) == KT_NONE);
    /* Rows of 1 and 2 lines: 1,2,2,1,2,2,1,2,2. A row that does not fit
     * whole is shown partly, and a selection never hides below the edge. */
    CHECK(kt_ui_bottom_top(&mixed, 5) == 6 && kt_ui_bottom_top(&mixed, 4) == 7);
    CHECK(kt_ui_rows_shown(&mixed, 0, 4) == 3);
    CHECK(kt_ui_scroll_to(&mixed, 0, 2, 4) == 1 && kt_ui_scroll_to(&mixed, 0, 2, 5) == 0);
    CHECK(kt_ui_scroll_to(&mixed, 0, 1, 1) == 1);
    CHECK(kt_ui_row_at(&mixed, 0, 2, 5) == 1 && kt_ui_row_at(&mixed, 0, 3, 5) == 2);
}

/* ---- list ---------------------------------------------------------------- */

static const char *const heads[] = {"feature-live", "feature-stale", "feature-unavailable", "run visualization-proof",
                                    "run second-run", "docs", "fix-login-redirect", "main"};
#define HEADS (sizeof(heads) / sizeof(heads[0]))

/* Rows 0 and 4 are section headers. */
static void head_item(void *context, size_t index, struct kt_list_item *item) {
    (void)context;
    if (index == 0 || index == 5) { item->title = index ? "Elsewhere" : "This project"; item->header = true; return; }
    item->title = heads[index < 5 ? index - 1 : index - 2];
    item->detail = index == 1 ? "codex" : NULL;
    item->icon = "\xe2\x97\x8f";
    item->icon_color = KT_COLOR_SUCCESS;
}

static struct kt_list list;
static struct kt_match matches[16];

static void list_view(void) {
    struct kt_msg down = key(KT_KEY_DOWN, 0), up = key(KT_KEY_UP, 0), end = key(KT_KEY_END, 0);
    struct kt_msg home = key('g', 0), enter = key(KT_KEY_ENTER, 0), slash = key('/', 0);
    kt_list_init(&list, head_item, NULL, HEADS + 2);
    /* The first selectable row, not the header. */
    CHECK(list.selected == 1 && kt_list_index(&list) == 1);
    kt_list_place(&list, (struct kt_rect){1, 0, 40, 6});
    blank(42, 8, true);
    kt_list_view(&list, &canvas, (struct kt_rect){1, 0, 40, 6});
    /* A header is a rule with its title; the scroll bar takes the last column. */
    CHECK(!strncmp(row(0), "   This project \xe2\x94\x80\xe2\x94\x80", 22));
    CHECK(strstr(row(0), "\xe2\x94\x80\xe2\x94\x83") && strstr(row(3), "\xe2\x94\x80") == NULL);
    CHECK(!strncmp(row(1), " \xe2\x96\x8c \xe2\x97\x8f feature-live", 21));
    CHECK(strstr(row(1), "codex"));
    CHECK(cell(1, 1)->foreground == KT_COLOR_ACCENT && cell(5, 1)->background == KT_COLOR_SELECTION);
    CHECK(cell(3, 1)->foreground == KT_COLOR_SUCCESS && (cell(5, 1)->attributes & KT_BOLD));
    /* Moving skips headers and scrolls; the last row is at the bottom edge. */
    CHECK(kt_list_update(&list, &down, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 2);
    CHECK(kt_list_update(&list, &down, NULL) == KT_LIST_MOVED);
    CHECK(kt_list_update(&list, &down, NULL) == KT_LIST_MOVED);
    CHECK(kt_list_update(&list, &down, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 6);
    CHECK(list.top == 1);
    CHECK(kt_list_update(&list, &end, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 9 && list.top == 4);
    CHECK(kt_list_update(&list, &down, NULL) == KT_LIST_HANDLED && kt_list_index(&list) == 9);
    CHECK(kt_list_update(&list, &home, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 1 && list.top == 0);
    CHECK(kt_list_update(&list, &up, NULL) == KT_LIST_HANDLED && kt_list_index(&list) == 1);
    CHECK(kt_list_update(&list, &enter, NULL) == KT_LIST_CHOSEN);
    /* No filter storage: '/' is not the list's. */
    CHECK(kt_list_update(&list, &slash, NULL) == KT_LIST_IGNORED);
}

static void list_filter(void) {
    struct kt_msg down = key(KT_KEY_DOWN, 0), enter = key(KT_KEY_ENTER, 0), slash = key('/', 0);
    struct kt_msg esc = key(KT_KEY_ESCAPE, 0);
    /* Filtering: characters go to the filter, matches are ranked and
     * highlighted, headers drop out. */
    kt_list_filter_storage(&list, matches, 16);
    CHECK(kt_list_update(&list, &slash, NULL) == KT_LIST_HANDLED && list.filtering);
    {
        struct kt_msg r = key('r', 0), u = key('u', 0), n = key('n', 0), j = key('j', 0), back = key(KT_KEY_BACKSPACE, 0);
        /* feature-live has an r: the selection stays where it is. */
        CHECK(kt_list_update(&list, &r, NULL) == KT_LIST_HANDLED && kt_list_index(&list) == 1);
        CHECK(kt_list_update(&list, &u, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 4);
        CHECK(kt_list_update(&list, &n, NULL) == KT_LIST_HANDLED);
        /* Matches at the start of the text rank first, then item order. */
        CHECK(kt_list_rows(&list) == 3 && kt_list_row_item(&list, 0) == 4 && kt_list_row_item(&list, 1) == 6);
        CHECK(kt_list_row_item(&list, 2) == 3 && kt_list_row_item(&list, 3) == KT_NONE);
        CHECK(kt_list_update(&list, &j, NULL) == KT_LIST_MOVED && kt_list_rows(&list) == 0);
        CHECK(kt_list_index(&list) == KT_NONE);
        CHECK(kt_list_update(&list, &back, NULL) == KT_LIST_MOVED && kt_list_rows(&list) == 3);
    }
    blank(42, 8, true);
    kt_list_view(&list, &canvas, (struct kt_rect){1, 0, 40, 6});
    CHECK(!strncmp(row(0), "  / run", 7) && strstr(row(0), "3 of 10"));
    CHECK(!strncmp(row(1), " \xe2\x96\x8c \xe2\x97\x8f run visualization-proof", 32));
    CHECK(cell(5, 1)->attributes & KT_UNDERLINE && !(cell(8, 1)->attributes & KT_UNDERLINE));
    CHECK(kt_list_update(&list, &down, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 6);
    CHECK(kt_list_update(&list, &enter, NULL) == KT_LIST_HANDLED && !list.filtering && kt_list_rows(&list) == 3);
    CHECK(kt_list_update(&list, &enter, NULL) == KT_LIST_CHOSEN && kt_list_index(&list) == 6);
    CHECK(kt_list_update(&list, &esc, NULL) == KT_LIST_HANDLED && kt_list_rows(&list) == HEADS + 2);
    CHECK(kt_list_index(&list) == 6);
    CHECK(kt_list_update(&list, &esc, NULL) == KT_LIST_IGNORED);
}

static void list_mouse(void) {
    struct kt_msg enter = key(KT_KEY_ENTER, 0);
    /* The mouse: a click selects, a second chooses, the wheel scrolls. */
    {
        struct kt_msg click = mouse(10, 2, 0), wheel = mouse(10, 2, 65), outside = mouse(10, 7, 0);
        kt_list_place(&list, (struct kt_rect){1, 0, 40, 6});
        list.top = 0;
        CHECK(kt_list_update(&list, &click, NULL) == KT_LIST_MOVED && kt_list_index(&list) == 2);
        CHECK(kt_list_update(&list, &click, NULL) == KT_LIST_CHOSEN);
        CHECK(kt_list_update(&list, &wheel, NULL) == KT_LIST_HANDLED && list.top == 3);
        CHECK(kt_list_update(&list, &wheel, NULL) == KT_LIST_HANDLED && list.top == 4);
        CHECK(kt_list_update(&list, &outside, NULL) == KT_LIST_IGNORED);
    }
    /* Status line, two-row items and the empty text. */
    list.status_line = true;
    list.item_height = 2;
    kt_list_place(&list, (struct kt_rect){1, 0, 40, 6});
    CHECK(kt_list_select(&list, 1) && list.top == 0);
    blank(42, 8, true);
    kt_list_view(&list, &canvas, (struct kt_rect){1, 0, 40, 6});
    /* A two-row item: the bar runs down both rows, the detail under the title. */
    CHECK(!strncmp(row(2), " \xe2\x96\x8c   codex   ", 13));
    CHECK(!strcmp(row(5), " 1\xe2\x80\x93" "3 of 10"));
    kt_list_set_count(&list, 0);
    CHECK(kt_list_index(&list) == KT_NONE && kt_list_update(&list, &enter, NULL) == KT_LIST_HANDLED);
    blank(42, 8, true);
    kt_list_view(&list, &canvas, (struct kt_rect){1, 0, 40, 6});
    CHECK(!strcmp(row(0), "   Nothing here"));
}

/* ---- table --------------------------------------------------------------- */

static const struct kt_table_column head_columns[] = {
    {"HEAD", KT_MIN(12), KT_ALIGN_LEFT}, {"STATUS", KT_FIXED(10), KT_ALIGN_LEFT}, {"REP", KT_FIXED(4), KT_ALIGN_RIGHT}};

static void head_cell(void *context, size_t row, size_t column, struct kt_table_cell *cell) {
    static const char *const status[] = {"running", "terminal gone", "stopped"};
    (void)context;
    if (column == 0) cell->text = heads[row];
    if (column == 1) { cell->text = status[row % 3]; cell->color = row % 3 == 1 ? KT_COLOR_WARNING : KT_DEFAULT; }
    if (column == 2) cell->text = row ? "\xe2\x80\x94" : "done";
}

static struct kt_table table_under_test;

static void table_view(void) {
    struct kt_table *t = &table_under_test;
    struct kt_rect parts[KT_TABLE_COLUMNS];
    kt_table_init(t, head_columns, 3, head_cell, NULL, HEADS);
    t->sortable = true;
    t->sort_column = 1;
    kt_table_place(t, (struct kt_rect){1, 0, 36, 5});
    /* Columns by constraint after the bar's two columns, a gap of two. */
    kt_table_columns(t, t->area, parts);
    CHECK(parts[0].x == 3 && parts[1].x == parts[0].x + parts[0].width + 2 && parts[2].width == 4);
    CHECK(parts[2].x + parts[2].width == 1 + 36 - 2);
    blank(40, 6, true);
    kt_table_view(t, &canvas, t->area);
    CHECK(!strcmp(row(0), "   HEAD            STATUS \xe2\x96\xb2     REP"));
    CHECK(cell(26, 0)->foreground == KT_COLOR_ACCENT);
    /* Cells are cut with an ellipsis per column; colours come from the cell. */
    CHECK(!strcmp(row(1), " \xe2\x96\x8c feature-live    running     done \xe2\x94\x83"));
    CHECK(!strcmp(row(2), "   feature-stale   terminal \xe2\x80\xa6     \xe2\x80\x94 \xe2\x94\x83"));
    CHECK(cell(20, 2)->foreground == KT_COLOR_WARNING);
    CHECK(!strncmp(row(3), "   feature-unava\xe2\x80\xa6", 19));
}

static void table_keys(void) {
    struct kt_table *t = &table_under_test;
    struct kt_msg down = key('j', 0), end = key('G', 0), sort = key('s', 0), reverse = key('S', 0);
    struct kt_msg heading = mouse(20, 0, 0), click = mouse(5, 2, 0);
    CHECK(kt_table_update(t, &down, NULL) == KT_TABLE_MOVED && t->selected == 1);
    CHECK(kt_table_update(t, &end, NULL) == KT_TABLE_MOVED && t->selected == HEADS - 1 && t->top == HEADS - 4);
    CHECK(kt_table_update(t, &down, NULL) == KT_TABLE_HANDLED);
    /* Sorting is asked for, not done: the next column, then the order. */
    CHECK(kt_table_update(t, &sort, NULL) == KT_TABLE_SORTED && t->sort_column == 2 && !t->sort_descending);
    CHECK(kt_table_update(t, &reverse, NULL) == KT_TABLE_SORTED && t->sort_column == 2 && t->sort_descending);
    CHECK(kt_table_update(t, &sort, NULL) == KT_TABLE_SORTED && t->sort_column == 0);
    CHECK(kt_table_update(t, &heading, NULL) == KT_TABLE_SORTED && t->sort_column == 1 && !t->sort_descending);
    CHECK(kt_table_update(t, &heading, NULL) == KT_TABLE_SORTED && t->sort_descending);
    blank(40, 6, true);
    kt_table_view(t, &canvas, t->area);
    CHECK(strstr(row(0), "STATUS \xe2\x96\xbc") != NULL);
    CHECK(kt_table_update(t, &click, NULL) == KT_TABLE_MOVED && t->selected == t->top + 1);
    CHECK(kt_table_update(t, &click, NULL) == KT_TABLE_CHOSEN);
    t->sortable = false;
    CHECK(kt_table_update(t, &sort, NULL) == KT_TABLE_IGNORED);
    kt_table_set_count(t, 0);
    CHECK(t->selected == KT_NONE && kt_table_update(t, &down, NULL) == KT_TABLE_HANDLED);
    blank(40, 6, true);
    kt_table_view(t, &canvas, t->area);
    CHECK(!strcmp(row(1), "   Nothing here"));
}

/* ---- viewport ------------------------------------------------------------ */

static const char *const prose[] = {
    "The quick brown fox jumps over the lazy dog.", "", "short", "averyveryverylongwordthatmustbecut", "one two three"};

static size_t prose_line(void *context, size_t index, struct kt_span *spans, size_t capacity) {
    (void)context;
    if (!capacity) return 0;
    spans[0].text = prose[index];
    spans[0].style = index == 2 ? KT_INK(KT_COLOR_ACCENT, KT_BOLD) : KT_INK(KT_COLOR_TEXT, 0);
    if (index != 4 || capacity < 2) return 1;
    spans[0].text = "one tw";
    spans[1].text = "o three";
    spans[1].style = KT_INK(KT_COLOR_DANGER, 0);
    return 2;
}

static size_t numbered(void *context, size_t index, struct kt_span *spans, size_t capacity) {
    static char text[64][16];
    (void)context;
    if (!capacity) return 0;
    snprintf(text[index % 64], sizeof(text[0]), "line %zu", index);
    spans[0].text = text[index % 64];
    spans[0].style = KT_BASE;
    return 1;
}

static void viewport_wrap(void) {
    struct kt_viewport v;
    struct kt_msg down = key('j', 0), up = key('k', 0), bottom = key('G', 0), top = key('g', 0);
    kt_viewport_init(&v, prose_line, NULL, 5);
    v.wrap = true;
    kt_viewport_place(&v, (struct kt_rect){0, 0, 11, 4});
    /* Ten columns of text and one for the scroll bar. */
    CHECK(kt_viewport_line_rows(&v, 0, 10) == 5 && kt_viewport_line_rows(&v, 1, 10) == 1);
    CHECK(kt_viewport_line_rows(&v, 3, 10) == 4 && kt_viewport_line_rows(&v, 4, 10) == 2);
    blank(12, 5, true);
    kt_viewport_view(&v, &canvas, v.area);
    /* Wrapped at blanks; the scroll bar counts lines. */
    CHECK(!strcmp(row(0), "The quick \xe2\x94\x83") && !strcmp(row(1), "brown fox \xe2\x94\x82"));
    CHECK(!strcmp(row(2), "jumps over\xe2\x94\x82") && !strcmp(row(3), "the lazy  \xe2\x94\x82"));
    CHECK(kt_viewport_update(&v, &up, NULL) == KT_VIEWPORT_HANDLED && v.top == 0 && v.top_row == 0);
    CHECK(kt_viewport_update(&v, &down, NULL) == KT_VIEWPORT_HANDLED && v.top == 0 && v.top_row == 1);
    /* The end: the last line's last row at the bottom edge, and no further. */
    CHECK(kt_viewport_update(&v, &bottom, NULL) == KT_VIEWPORT_HANDLED && kt_viewport_at_bottom(&v));
    CHECK(v.top == 3 && v.top_row == 2 && kt_viewport_percent(&v) == 100);
    CHECK(kt_viewport_update(&v, &down, NULL) == KT_VIEWPORT_HANDLED && v.top == 3 && v.top_row == 2);
    blank(12, 5, true);
    kt_viewport_view(&v, &canvas, v.area);
    /* A word longer than the width is cut between cells; runs keep their style across a break. */
    CHECK(!strncmp(row(0), "dthatmustb", 10) && !strncmp(row(1), "ecut ", 5));
    CHECK(!strncmp(row(2), "one two ", 8) && !strncmp(row(3), "three ", 6));
    CHECK(cell(6, 2)->foreground == KT_COLOR_DANGER && cell(0, 2)->foreground == KT_COLOR_TEXT);
    CHECK(cell(0, 3)->foreground == KT_COLOR_DANGER);
    CHECK(kt_viewport_update(&v, &up, NULL) == KT_VIEWPORT_HANDLED && v.top == 3 && v.top_row == 1);
    CHECK(kt_viewport_update(&v, &top, NULL) == KT_VIEWPORT_HANDLED && v.top == 0 && kt_viewport_percent(&v) == 0);
    /* Taller: the place is kept and clamped. */
    kt_viewport_place(&v, (struct kt_rect){0, 0, 11, 30});
    CHECK(v.top == 0 && kt_viewport_at_bottom(&v));
}

static struct kt_viewport follower;

static void viewport_follow(void) {
    struct kt_viewport *v = &follower;
    struct kt_msg up = key(KT_KEY_UP, 0), end = key(KT_KEY_END, 0), page = key(KT_KEY_PAGE_UP, 0);
    struct kt_msg half = key('d', KT_MOD_CTRL);
    kt_viewport_init(v, numbered, NULL, 3);
    v->follow = true;
    kt_viewport_place(v, (struct kt_rect){0, 0, 10, 4});
    CHECK(v->at_end && v->top == 0);
    /* Following: new lines keep the end in view. */
    kt_viewport_set_count(v, 10);
    CHECK(v->top == 6 && v->at_end);
    kt_viewport_set_count(v, 12);
    CHECK(v->top == 8);
    blank(10, 4, true);
    kt_viewport_view(v, &canvas, v->area);
    CHECK(!strcmp(row(3), "line 11  \xe2\x94\x83"));
    /* Scrolling up leaves the end: more lines do not move the view. */
    CHECK(kt_viewport_update(v, &up, NULL) == KT_VIEWPORT_HANDLED && v->top == 7 && !v->at_end);
    kt_viewport_set_count(v, 20);
    CHECK(v->top == 7);
    CHECK(kt_viewport_update(v, &page, NULL) == KT_VIEWPORT_HANDLED && v->top == 4);
    CHECK(kt_viewport_update(v, &half, NULL) == KT_VIEWPORT_HANDLED && v->top == 6);
    CHECK(kt_viewport_percent(v) == 6 * 100 / 17);
    CHECK(kt_viewport_update(v, &end, NULL) == KT_VIEWPORT_HANDLED && v->top == 16 && v->at_end);
    kt_viewport_set_count(v, 21);
    CHECK(v->top == 17);
    /* Fewer lines than rows; none at all. */
    kt_viewport_set_count(v, 2);
    CHECK(v->top == 0);
    kt_viewport_set_count(v, 0);
    CHECK(v->top == 0 && kt_viewport_at_bottom(v));
    blank(10, 4, true);
    kt_viewport_view(v, &canvas, v->area);
    CHECK(!strcmp(row(0), ""));
}

static void viewport_sideways(void) {
    struct kt_viewport *v = &follower;
    struct kt_msg right = key('l', 0), left = key('h', 0);
    /* Clipped lines scroll sideways, no further than the widest one shown. */
    kt_viewport_set_count(v, 4);
    v->scrollbar = false;
    CHECK(kt_viewport_update(v, &right, NULL) == KT_VIEWPORT_HANDLED && v->left == 0);
    v->area.width = 4;
    CHECK(kt_viewport_update(v, &right, NULL) == KT_VIEWPORT_HANDLED && v->left == 2);
    blank(10, 4, true);
    kt_viewport_view(v, &canvas, v->area);
    CHECK(!strcmp(row(1), "ne 1"));
    CHECK(kt_viewport_update(v, &left, NULL) == KT_VIEWPORT_HANDLED && v->left == 0);
    v->wrap = true;
    CHECK(kt_viewport_update(v, &left, NULL) == KT_VIEWPORT_IGNORED);
}

static void viewport_canvas(void) {
    static struct kt_cell source_cells[20 * 6];
    struct kt_canvas source;
    struct kt_viewport v;
    struct kt_msg down = key(KT_KEY_DOWN, 0), wheel = mouse(1, 1, 65), right = key(KT_KEY_RIGHT, 0);
    CHECK(kt_init(&source, source_cells, 20 * 6, 20, 6, true));
    kt_clear(&source, KT_BASE);
    kt_text(&source, (struct kt_rect){0, 0, 20, 1}, "row zero \xe7\x95\x8c", KT_STRONG);
    kt_text(&source, (struct kt_rect){0, 5, 20, 1}, "row five is longer", KT_MUTED);
    kt_viewport_init(&v, NULL, NULL, 0);
    kt_viewport_set_canvas(&v, &source);
    v.scrollbar = false;
    kt_viewport_place(&v, (struct kt_rect){0, 0, 10, 3});
    blank(12, 4, true);
    kt_viewport_view(&v, &canvas, v.area);
    /* The wide character would be cut by the edge: a blank instead. */
    CHECK(!strcmp(row(0), "row zero") && cell(0, 0)->attributes == KT_BOLD);
    CHECK(kt_viewport_update(&v, &down, NULL) == KT_VIEWPORT_HANDLED && v.top == 1);
    CHECK(kt_viewport_update(&v, &wheel, NULL) == KT_VIEWPORT_HANDLED && v.top == 3);
    blank(12, 4, true);
    kt_viewport_view(&v, &canvas, v.area);
    CHECK(!strcmp(row(2), "row five i") && cell(0, 2)->foreground == KT_COLOR_MUTED);
    CHECK(kt_viewport_update(&v, &right, NULL) == KT_VIEWPORT_HANDLED && v.left == 8);
    blank(12, 4, true);
    kt_viewport_view(&v, &canvas, v.area);
    CHECK(!strcmp(row(2), " is longer"));
}

int main(void) {
    fuzzy();
    ranking();
    text();
    hints();
    help_short();
    help_full();
    scrolling();
    list_view();
    list_filter();
    list_mouse();
    table_view();
    table_keys();
    viewport_wrap();
    viewport_follow();
    viewport_sideways();
    viewport_canvas();
    if (failures) {
        fprintf(stderr, "components: %d check(s) failed\n", failures);
        return 1;
    }
    puts("components: fuzzy match and rank, wrapping, chord hints, short and full help, scroll arithmetic, the "
         "list, the table and the viewport passed");
    return 0;
}
