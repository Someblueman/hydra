#include "kettle/components.h"
#include "kettle/testing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Components inside a running program, under the headless driver: keys and
 * chords through the real decoder, pastes, mouse reports, and the frames
 * that come out. */

/* The program under test, whose last frame a failure shows. */
static struct kt_test *current;

static void check_at(bool passed, const char *what, int line) {
    if (passed) return;
    fprintf(stderr, "FAIL tests/c/test_kettle_widgets.c:%d: %s\n", line, what);
    if (current) fprintf(stderr, "%s\n", kt_test_screen(current));
    exit(1);
}
#define CHECK(condition) check_at((condition), #condition, __LINE__)

static bool has(const char *text, const char *part) { return strstr(text, part) != NULL; }

/* ---- a list, a text input and a footer ---------------------------------- */

static const char *const names[] = {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
                                    "india", "juliett", "kilo", "lima"};
#define NAMES (sizeof(names) / sizeof(names[0]))

enum { APP_QUIT, APP_FOCUS, APP_ACTIONS };

struct editor {
    struct kt_list list;
    struct kt_match matches[NAMES];
    struct kt_textinput input;
    char text[16];
    struct kt_help help;
    struct kt_binding keys[APP_ACTIONS];
    struct kt_keymap_state chord;
    bool on_input;
    int chosen, submitted, changes, invalid;
};

static void name_item(void *context, size_t index, struct kt_list_item *item) {
    (void)context;
    item->title = names[index];
}

static bool no_spaces(void *context, const char *text, char *error, size_t capacity) {
    (void)context;
    if (!strchr(text, ' ')) return true;
    snprintf(error, capacity, "no spaces");
    return false;
}

static void footer(struct editor *e) {
    struct kt_help_source sources[3];
    if (e->on_input) {
        sources[0].bindings = e->input.keys; sources[0].count = KT_TEXTINPUT_ACTIONS;
        sources[0].scope = kt_textinput_scope(&e->input);
    } else {
        sources[0].bindings = e->list.keys; sources[0].count = KT_LIST_ACTIONS; sources[0].scope = kt_list_scope(&e->list);
    }
    sources[1].bindings = e->keys; sources[1].count = APP_ACTIONS; sources[1].scope = 1;
    sources[2].bindings = e->help.keys; sources[2].count = KT_HELP_ACTIONS; sources[2].scope = kt_help_scope(&e->help);
    (void)kt_help_set(&e->help, sources, 3);
}

static void editor_init(struct editor *e) {
    static const char *const suggestions[] = {"main", "maintenance", "feature"};
    struct kt_keymap map;
    char error[64];
    memset(e, 0, sizeof(*e));
    kt_list_init(&e->list, name_item, NULL, NAMES);
    kt_list_filter_storage(&e->list, e->matches, NAMES);
    e->list.status_line = true;
    kt_textinput_init(&e->input, e->text, sizeof(e->text));
    e->input.placeholder = "branch";
    e->input.limit = 12;
    e->input.validate = no_spaces;
    e->input.suggestions = suggestions;
    e->input.suggestion_count = 3;
    kt_help_init(&e->help);
    e->keys[APP_QUIT].name = "app.quit"; e->keys[APP_QUIT].label = "quit"; e->keys[APP_QUIT].priority = 9;
    e->keys[APP_FOCUS].name = "app.focus"; e->keys[APP_FOCUS].label = "switch"; e->keys[APP_FOCUS].action = APP_FOCUS;
    e->keys[APP_QUIT].scope = e->keys[APP_FOCUS].scope = 1;
    map.bindings = e->keys; map.count = APP_ACTIONS;
    CHECK(kt_keymap_configure(&map, "app.quit = ctrl+q\napp.focus = ctrl+o, ctrl+x o\n",
                                   strlen("app.quit = ctrl+q\napp.focus = ctrl+o, ctrl+x o\n"), error, sizeof(error)));
    footer(e);
}

static void editor_place(struct editor *e, int columns, int rows) {
    kt_list_place(&e->list, (struct kt_rect){0, 0, columns, rows - 3});
    kt_textinput_place(&e->input, (struct kt_rect){0, rows - 2, 20, 1});
}

static bool app_key(struct editor *e, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct kt_keymap map = {e->keys, APP_ACTIONS};
    const struct kt_binding *b;
    enum kt_key_match m;
    if (msg->type != KT_MSG_KEY) return false;
    m = kt_keymap_feed(&map, &e->chord, 1, &msg->u.event, &b);
    if (m != KT_KEY_ACTION) return m != KT_KEY_UNBOUND;
    if (b->action == APP_QUIT) kt_cmd_quit(cmds, 0);
    else e->on_input = e->input.focused = !e->on_input;
    if (b->action == APP_FOCUS) e->list.focused = !e->on_input;
    return true;
}

static bool focused_update(struct editor *e, const struct kt_msg *msg, struct kt_cmds *cmds) {
    if (e->on_input) {
        enum kt_textinput_result r = kt_textinput_update(&e->input, msg, cmds);
        e->changes += r == KT_TEXTINPUT_CHANGED;
        e->submitted += r == KT_TEXTINPUT_SUBMITTED;
        e->invalid += r == KT_TEXTINPUT_INVALID;
        return r != KT_TEXTINPUT_IGNORED;
    } else {
        enum kt_list_result r = kt_list_update(&e->list, msg, cmds);
        if (r == KT_LIST_CHOSEN) e->chosen = (int)kt_list_index(&e->list);
        return r != KT_LIST_IGNORED;
    }
}

static bool editor_update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct editor *e = model;
    bool handled;
    if (msg->type == KT_MSG_RESIZE) { editor_place(e, msg->u.resize.columns, msg->u.resize.rows); return true; }
    /* The focused component first, then the help, then the application. */
    handled = focused_update(e, msg, cmds);
    if (!handled) handled = kt_help_update(&e->help, msg, cmds) != KT_HELP_IGNORED;
    if (!handled) handled = app_key(e, msg, cmds);
    footer(e);
    return handled;
}

static void editor_view(const void *model, struct kt_frame *frame) {
    const struct editor *e = model;
    struct kt_canvas *canvas = kt_frame_canvas(frame);
    int columns = kt_frame_columns(frame), rows = kt_frame_rows(frame), x, y;
    kt_list_view(&e->list, canvas, e->list.area);
    kt_textinput_view(&e->input, canvas, e->input.area);
    if (!e->input.valid) kt_text(canvas, (struct kt_rect){22, rows - 2, columns - 22, 1}, e->input.error, KT_WARNING);
    if (e->help.full) {
        kt_fill(canvas, (struct kt_rect){0, 0, columns, rows}, KT_BASE);
        kt_help_full_view(&e->help, canvas, (struct kt_rect){0, 0, columns, rows});
    }
    else kt_help_view(&e->help, canvas, (struct kt_rect){0, rows - 1, columns, 1});
    if (kt_textinput_cursor(&e->input, &x, &y)) kt_frame_cursor(frame, x, y);
}

static const struct kt_app editor_app = {NULL, editor_update, editor_view};

static struct kt_test *start(struct editor *e, int columns, int rows) {
    struct kt_options options;
    struct kt_test *t;
    memset(&options, 0, sizeof(options));
    options.escape_ms = 10;
    editor_init(e);
    t = kt_test_new(&editor_app, e, columns, rows, &options);
    CHECK(t != NULL);
    current = t;
    return t;
}

static void list_keys(void) {
    static struct editor e;
    struct kt_test *t = start(&e, 80, 9);
    CHECK(has(kt_test_row(t, 0), "\xe2\x96\x8c alpha"));
    CHECK(has(kt_test_row(t, 5), "1\xe2\x80\x93" "5 of 12"));
    CHECK(has(kt_test_row(t, 8), "\xe2\x86\x91/\xe2\x86\x93 select"));
    kt_test_type(t, "jj\033[B");
    CHECK(kt_list_index(&e.list) == 3 && has(kt_test_row(t, 3), "\xe2\x96\x8c delta"));
    kt_test_type(t, "\033[6~");                 /* page down */
    CHECK(kt_list_index(&e.list) == 7 && e.list.top == 3);
    kt_test_type(t, "G");
    CHECK(kt_list_index(&e.list) == 11 && has(kt_test_row(t, 4), "\xe2\x96\x8c lima"));
    CHECK(has(kt_test_row(t, 5), "8\xe2\x80\x93" "12 of 12"));
    kt_test_type(t, "\r");
    CHECK(e.chosen == 11);
    /* Filter as you type: '/' then letters; Esc after its wait clears. */
    kt_test_type(t, "/ch");
    CHECK(e.list.filtering && kt_list_rows(&e.list) == 2 && kt_list_index(&e.list) == 2);
    CHECK(has(kt_test_row(t, 0), "/ ch") && has(kt_test_row(t, 0), "2 of 12") && has(kt_test_row(t, 2), "  echo"));
    CHECK(has(kt_test_row(t, 1), "\xe2\x96\x8c charlie"));
    CHECK(has(kt_test_row(t, 8), "enter keep filter") && has(kt_test_row(t, 8), "esc clear filter"));
    /* A paste is typed into the filter as one edit. */
    kt_test_type(t, "\033[200~ar\033[201~");
    CHECK(!strcmp(kt_textinput_value(&e.list.filter), "char") && kt_list_rows(&e.list) == 1);
    kt_test_type(t, "\r");
    CHECK(!e.list.filtering && kt_list_rows(&e.list) == 1);
    kt_test_type(t, "\033");
    kt_test_advance(t, 10);
    CHECK(kt_list_rows(&e.list) == NAMES && kt_list_index(&e.list) == 2);
    /* The mouse: a click selects, a second click chooses, the wheel scrolls. */
    kt_test_type(t, "\033[<0;4;5M\033[<0;4;5m");
    CHECK(kt_list_index(&e.list) == 4 && e.chosen == 11);
    kt_test_type(t, "\033[<0;4;5M");
    CHECK(e.chosen == 4);
    kt_test_type(t, "\033[<65;4;2M\033[<65;4;2M\033[<65;4;2M");
    CHECK(e.list.top == 7 && kt_list_index(&e.list) == 4);
    /* Help: '?' opens the full form, Esc closes it. */
    kt_test_type(t, "?");
    CHECK(e.help.full && has(kt_test_screen(t), "Move") && has(kt_test_screen(t), "pgup/pgdn"));
    CHECK(has(kt_test_screen(t), "\xe2\x86\x91/\xe2\x86\x93  k/j"));
    kt_test_type(t, "\033");
    kt_test_advance(t, 10);
    CHECK(!e.help.full);
    CHECK(kt_test_sleep(t) == -1);
    kt_test_type(t, "\x11");
    CHECK(!kt_test_running(t) && kt_test_status(t) == 0);
    kt_test_free(t);
    puts("PASS list: keys, paging, filter as you type, paste into the filter, mouse, help toggled from its table");
}

static void text_input(void) {
    static struct editor e;
    struct kt_test *t = start(&e, 50, 9);
    int x, y;
    /* A chord of two keys moves the focus. */
    kt_test_type(t, "\x18");
    CHECK(!e.on_input);
    kt_test_type(t, "o");
    CHECK(e.on_input && e.input.focused);
    CHECK(has(kt_test_row(t, 7), "\xe2\x94\x83 branch") && kt_test_cursor(t, &x, &y) && x == 2 && y == 7);
    CHECK(has(kt_test_row(t, 8), "enter submit"));
    kt_test_type(t, "fix login");
    CHECK(!strcmp(e.text, "fix login") && e.changes == 9 && !e.input.valid);
    CHECK(has(kt_test_row(t, 7), "no spaces"));
    kt_test_type(t, "\r");
    CHECK(e.invalid == 1 && e.submitted == 0);
    /* Word motions and deletions. */
    kt_test_type(t, "\033b");                   /* alt+b: to the start of "login" */
    CHECK(e.input.cursor == 4);
    kt_test_type(t, "\x08");                    /* ctrl+h deletes the space */
    CHECK(!strcmp(e.text, "fixlogin") && e.input.valid);
    kt_test_type(t, "-\x05 up");                /* '-', ctrl+e, " up" */
    CHECK(!strcmp(e.text, "fix-login up") && !e.input.valid);
    kt_test_type(t, "\x17");                    /* ctrl+w: the word before the cursor */
    CHECK(!strcmp(e.text, "fix-login ") && e.input.cursor == 10);
    kt_test_type(t, "\x01\x0b");                /* ctrl+a, ctrl+k */
    CHECK(!strcmp(e.text, "") && e.input.cursor == 0);
    /* The character limit, and a paste that is cut where it stops fitting. */
    kt_test_type(t, "\033[200~one\ntwo\tthree-four\033[201~");
    CHECK(!strcmp(e.text, "one two thre") && e.changes > 9);
    kt_test_type(t, "x");
    CHECK(!strcmp(e.text, "one two thre"));
    /* Horizontal scrolling keeps the cursor in view; the cursor is drawn. */
    kt_test_type(t, "\x15");                    /* ctrl+u */
    CHECK(!strcmp(e.text, ""));
    kt_test_type(t, "\033[200~abcdefghijkl\033[201~");
    CHECK(e.input.cursor == 12 && e.input.offset == 0);
    kt_textinput_place(&e.input, (struct kt_rect){0, 7, 8, 1});
    CHECK(e.input.offset == 7);
    kt_test_type(t, "\033[H");
    CHECK(e.input.offset == 0);
    /* Suggestions: offered after the text, tab or right takes one. */
    kt_textinput_place(&e.input, (struct kt_rect){0, 7, 20, 1});
    kt_test_type(t, "\x0b" "ma");
    CHECK(!strcmp(kt_textinput_suggestion(&e.input), "main"));
    CHECK(has(kt_test_row(t, 7), "\xe2\x94\x83 main"));
    CHECK(has(kt_test_row(t, 8), "tab complete"));
    kt_test_type(t, "\x0e");                    /* ctrl+n: the next one */
    CHECK(!strcmp(kt_textinput_suggestion(&e.input), "maintenance"));
    kt_test_type(t, "\t");
    CHECK(!strcmp(e.text, "maintenance") && !kt_textinput_suggestion(&e.input));
    kt_test_type(t, "\x15" "fe\033[C");
    CHECK(!strcmp(e.text, "feature"));
    kt_test_type(t, "\r");
    CHECK(e.submitted == 1);
    /* A click puts the cursor under it. */
    kt_test_type(t, "\033[<0;5;8M");
    CHECK(e.input.cursor == 2);
    CHECK(kt_test_sleep(t) == -1);
    kt_test_free(t);
    puts("PASS text input: typing, validation, word motions, limit, paste, scrolling, suggestions, click");
}

/* ---- timed components and modals ---------------------------------------- */

enum { SPIN = 1, TOAST = 2, FORM_MODAL = 1 };

struct timed {
    struct kt_spinner spinner;
    struct kt_toasts toasts;
    struct kt_modals modals;
    struct kt_form form;
    struct kt_field fields[2];
    struct kt_textinput name;
    int ticks, expired, dismissed, submitted;
    char last[64];
};

static void timed_init(struct timed *m) {
    static const char *const agents[] = {"codex", "claude"};
    memset(m, 0, sizeof(*m));
    kt_spinner_init(&m->spinner, SPIN);
    m->spinner.label = "working";
    kt_toasts_init(&m->toasts, TOAST);
    m->toasts.timeout_ms = 3000;
    kt_modals_init(&m->modals);
    kt_textinput_init(&m->name, NULL, 0);
    m->fields[0].kind = KT_FIELD_TEXT; m->fields[0].label = "Task name"; m->fields[0].input = &m->name;
    m->fields[1].kind = KT_FIELD_CHOICE; m->fields[1].label = "Agent";
    m->fields[1].options = agents; m->fields[1].option_count = 2;
    kt_form_init(&m->form, "New task", m->fields, 2);
}

static bool timed_modal(struct timed *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    switch (kt_modals_update(&m->modals, msg, cmds)) {
    case KT_MODALS_RESULT:
        if (m->modals.result == KT_FORM_SUBMITTED) {
            m->submitted++;
            snprintf(m->last, sizeof(m->last), "%s", kt_textinput_value(&m->name));
            kt_toasts_push(&m->toasts, cmds, KT_TOAST_SUCCESS, "Created");
        }
        if (m->modals.result == KT_FORM_SUBMITTED || m->modals.result == KT_FORM_CANCELLED) kt_modals_pop(&m->modals);
        return true;
    case KT_MODALS_IGNORED: return false;
    default: return true;
    }
}

static bool timed_key(struct timed *m, uint32_t key, struct kt_cmds *cmds) {
    struct kt_modal modal;
    switch (key) {
    case 's': kt_spinner_start(&m->spinner, cmds); return true;
    case 'x': kt_spinner_stop(&m->spinner, cmds); return true;
    case 'h': kt_spinner_show(&m->spinner, !m->spinner.visible, cmds); return true;
    case 't': kt_toasts_push(&m->toasts, cmds, KT_TOAST_INFO, "Saved"); return true;
    case 'w': kt_toasts_push(&m->toasts, cmds, KT_TOAST_WARNING, "Careful"); return true;
    case 'n':
        kt_textinput_set(&m->name, "");
        kt_form_focus(&m->form, 0);
        modal = kt_form_modal(&m->form, FORM_MODAL, 40);
        return kt_modals_push(&m->modals, &modal);
    default: return false;
    }
}

static bool timed_update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct timed *m = model;
    enum kt_toasts_result toast;
    if (msg->type == KT_MSG_RESIZE) {
        struct kt_rect all = {0, 0, msg->u.resize.columns, msg->u.resize.rows};
        kt_modals_place(&m->modals, all);
        kt_toasts_place(&m->toasts, kt_rect_inset(all, 0, 1, 1, 0));
        return true;
    }
    if (kt_spinner_update(&m->spinner, msg, cmds) == KT_SPINNER_HANDLED) { m->ticks++; return true; }
    toast = kt_toasts_update(&m->toasts, msg, cmds);
    m->expired += toast == KT_TOASTS_EXPIRED;
    m->dismissed += toast == KT_TOASTS_DISMISSED;
    if (toast != KT_TOASTS_IGNORED) return true;
    if (timed_modal(m, msg, cmds)) return true;
    return msg->type == KT_MSG_KEY && timed_key(m, msg->u.event.key, cmds);
}

static void timed_view(const void *model, struct kt_frame *frame) {
    const struct timed *m = model;
    struct kt_canvas *canvas = kt_frame_canvas(frame);
    struct kt_rect all = {0, 0, kt_frame_columns(frame), kt_frame_rows(frame)};
    kt_text(canvas, (struct kt_rect){0, 1, all.width, 1}, "base view", KT_STRONG);
    kt_spinner_view(&m->spinner, canvas, (struct kt_rect){0, 0, 20, 1});
    kt_toasts_view(&m->toasts, canvas, kt_rect_inset(all, 0, 1, 1, 0));
    kt_modals_view(&m->modals, canvas, all);
}

static const struct kt_app timed_app = {NULL, timed_update, timed_view};

static struct kt_test *timed_start(struct timed *m) {
    struct kt_options options;
    struct kt_test *t;
    memset(&options, 0, sizeof(options));
    options.escape_ms = 10;
    timed_init(m);
    t = kt_test_new(&timed_app, m, 50, 14, &options);
    CHECK(t != NULL);
    current = t;
    return t;
}

static void spinner(void) {
    static struct timed m;
    struct kt_test *t = timed_start(&m);
    /* Stopped: no frame, no timer, the loop sleeps until an event. */
    CHECK(!strcmp(kt_test_row(t, 0), "working") && kt_test_sleep(t) == -1);
    kt_test_type(t, "s");
    CHECK(m.spinner.timer && kt_test_sleep(t) == 80);
    CHECK(!strcmp(kt_test_row(t, 0), "\xe2\xa0\x8b working"));
    kt_test_advance(t, 80 * 3);
    CHECK(m.ticks == 3 && !strcmp(kt_test_row(t, 0), "\xe2\xa0\xb8 working"));
    /* Hidden: the timer goes; shown again, it comes back. */
    kt_test_type(t, "h");
    CHECK(!m.spinner.timer && kt_test_sleep(t) == -1 && !strcmp(kt_test_row(t, 0), ""));
    kt_test_advance(t, 1000);
    CHECK(m.ticks == 3);
    kt_test_type(t, "h");
    CHECK(m.spinner.timer && kt_test_sleep(t) == 80);
    kt_test_advance(t, 80);
    CHECK(m.ticks == 4);
    /* Stopped: idle again. */
    kt_test_type(t, "x");
    CHECK(!m.spinner.timer && kt_test_sleep(t) == -1);
    kt_test_advance(t, 1000);
    CHECK(m.ticks == 4 && !strcmp(kt_test_row(t, 0), "working"));
    kt_test_free(t);
    puts("PASS spinner: ticks only while running and shown, silent when stopped or hidden");
}

static void toasts(void) {
    static struct timed m;
    struct kt_test *t = timed_start(&m);
    kt_test_type(t, "t");
    CHECK(m.toasts.count == 1 && kt_test_sleep(t) == 3000);
    CHECK(has(kt_test_row(t, 12), "\xe2\x96\x8c \xe2\x80\xa2 Saved"));
    kt_test_advance(t, 1000);
    kt_test_type(t, "w");
    CHECK(has(kt_test_row(t, 11), "Saved") && has(kt_test_row(t, 12), "\xe2\x96\xb2 Careful"));
    /* Each expires on its own time; then nothing is left to wake for. */
    kt_test_advance(t, 2000);
    CHECK(m.expired == 1 && m.toasts.count == 1 && !has(kt_test_screen(t), "Saved"));
    kt_test_advance(t, 1000);
    CHECK(m.expired == 2 && m.toasts.count == 0 && kt_test_sleep(t) == -1);
    /* A fourth replaces the oldest, whose timer goes with it. */
    kt_test_type(t, "tttw");
    CHECK(m.toasts.count == 3 && m.toasts.items[2].kind == KT_TOAST_WARNING);
    /* A click dismisses the one under it. */
    kt_test_type(t, "\033[<0;48;13M");
    CHECK(m.dismissed == 1 && m.toasts.count == 2);
    kt_test_advance(t, 3000);
    CHECK(m.expired == 4 && m.toasts.count == 0 && kt_test_sleep(t) == -1);
    kt_test_free(t);
    puts("PASS toasts: stacking, a timer each, replacement, click to dismiss, silent when none");
}

static void modal_form(void) {
    static struct timed m;
    struct kt_test *t = timed_start(&m);
    kt_test_type(t, "n");
    CHECK(m.modals.count == 1 && has(kt_test_screen(t), "New task"));
    /* Keys and a paste go to the form, not the application below. */
    kt_test_type(t, "s\033[200~fix login\033[201~");
    CHECK(!strcmp(kt_textinput_value(&m.name), "sfix login") && !m.spinner.running);
    kt_test_type(t, "\t\033[C");
    CHECK(m.fields[1].choice == 1);
    /* The base is still drawn, faint under the panel. */
    CHECK(has(kt_test_row(t, 1), "base view"));
    kt_test_type(t, "\r");
    CHECK(m.submitted == 1 && !strcmp(m.last, "sfix login") && m.modals.count == 0 && m.toasts.count == 1);
    /* Esc cancels the form; the stack is empty again. */
    kt_test_type(t, "n\033");
    kt_test_advance(t, 10);
    CHECK(m.modals.count == 0 && m.submitted == 1);
    kt_test_advance(t, 3000);
    CHECK(kt_test_sleep(t) == -1);
    kt_test_free(t);
    puts("PASS modal stack: a form over the view takes keys and paste, submits and cancels");
}

int main(void) {
    list_keys();
    text_input();
    spinner();
    toasts();
    modal_form();
    return 0;
}
