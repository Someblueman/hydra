#include "kettle/keymap.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what, int line) {
    if (ok) return;
    fprintf(stderr, "tests/c/test_kettle_keymap.c:%d: %s\n", line, what);
    failures++;
}
#define CHECK(condition) check((condition), #condition, __LINE__)
#define SHIFT KT_MOD_SHIFT
#define ALT KT_MOD_ALT
#define CTRL KT_MOD_CTRL

static bool parse(const char *text, struct kt_chord *out) { return kt_chord_parse(text, strlen(text), out); }

struct chord_case {
    const char *text;
    unsigned count;
    struct kt_keystroke keys[KT_CHORD_MAX];
    const char *display, *config;
};

static const struct chord_case chords[] = {
    {"ctrl+]", 1, {{']', CTRL}}, "Ctrl-]", "ctrl+]"},
    {"ctrl+b tab", 2, {{'b', CTRL}, {KT_KEY_TAB, 0}}, "Ctrl-B Tab", "ctrl+b tab"},
    {"shift+tab", 1, {{KT_KEY_TAB, SHIFT}}, "Shift-Tab", "shift+tab"},
    {"g g", 2, {{'g', 0}, {'g', 0}}, "g g", "g g"},
    {"f5", 1, {{KT_KEY_F5, 0}}, "F5", "f5"},
    {"F12", 1, {{KT_KEY_F12, 0}}, "F12", "f12"},
    {"f1", 1, {{KT_KEY_F1, 0}}, "F1", "f1"},
    {"space", 1, {{' ', 0}}, "Space", "space"},
    {"esc", 1, {{KT_KEY_ESCAPE, 0}}, "Esc", "esc"},
    {"alt+enter", 1, {{KT_KEY_ENTER, ALT}}, "Alt-Enter", "alt+enter"},
    {"ctrl+\\ x", 2, {{'\\', CTRL}, {'x', 0}}, "Ctrl-\\ x", "ctrl+\\ x"},
    {"ctrl+\\ ctrl+\\", 2, {{'\\', CTRL}, {'\\', CTRL}}, "Ctrl-\\ Ctrl-\\", "ctrl+\\ ctrl+\\"},
    {"Ctrl-\\ Tab", 2, {{'\\', CTRL}, {KT_KEY_TAB, 0}}, "Ctrl-\\ Tab", "ctrl+\\ tab"},
    {"D", 1, {{'D', 0}}, "D", "D"},
    {"d", 1, {{'d', 0}}, "d", "d"},
    {"shift+d", 1, {{'D', 0}}, "D", "D"},
    {"Shift-D", 1, {{'D', 0}}, "D", "D"},
    {"ctrl+B", 1, {{'b', CTRL}}, "Ctrl-B", "ctrl+b"},
    {"ctrl+shift+b", 1, {{'b', CTRL}}, "Ctrl-B", "ctrl+b"},
    {"alt+d", 1, {{'d', ALT}}, "Alt-d", "alt+d"},
    {"alt+D", 1, {{'D', ALT}}, "Alt-D", "alt+D"},
    {"ctrl+alt+b", 1, {{'b', CTRL | ALT}}, "Ctrl-Alt-B", "ctrl+alt+b"},
    {"ctrl+i", 1, {{KT_KEY_TAB, 0}}, "Tab", "tab"},
    {"ctrl+m", 1, {{KT_KEY_ENTER, 0}}, "Enter", "enter"},
    {"ctrl+[", 1, {{KT_KEY_ESCAPE, 0}}, "Esc", "esc"},
    {"ctrl+?", 1, {{KT_KEY_BACKSPACE, 0}}, "Backspace", "backspace"},
    {"ctrl+@", 1, {{' ', CTRL}}, "Ctrl-Space", "ctrl+space"},
    {"ctrl+space", 1, {{' ', CTRL}}, "Ctrl-Space", "ctrl+space"},
    {"control+alt+delete", 1, {{KT_KEY_DELETE, CTRL | ALT}}, "Ctrl-Alt-Del", "ctrl+alt+delete"},
    {"meta+x", 1, {{'x', ALT}}, "Alt-x", "alt+x"},
    {"alt+shift+up", 1, {{KT_KEY_UP, ALT | SHIFT}}, "Alt-Shift-Up", "alt+shift+up"},
    {"  ctrl+]  ", 1, {{']', CTRL}}, "Ctrl-]", "ctrl+]"},
    {"a\tb", 2, {{'a', 0}, {'b', 0}}, "a b", "a b"},
    {"a b c d", 4, {{'a', 0}, {'b', 0}, {'c', 0}, {'d', 0}}, "a b c d", "a b c d"},
    {"ctrl++", 1, {{'+', CTRL}}, "Ctrl-+", "ctrl+plus"},
    {"ctrl+-", 1, {{'-', CTRL}}, "Ctrl--", "ctrl+minus"},
    {"ctrl--", 1, {{'-', CTRL}}, "Ctrl--", "ctrl+minus"},
    {"-", 1, {{'-', 0}}, "-", "minus"},
    {"+", 1, {{'+', 0}}, "+", "plus"},
    {"plus minus", 2, {{'+', 0}, {'-', 0}}, "+ -", "plus minus"},
    {"comma", 1, {{',', 0}}, ",", "comma"},
    {",", 1, {{',', 0}}, ",", "comma"},
    {"hash", 1, {{'#', 0}}, "#", "hash"},
    {"#", 1, {{'#', 0}}, "#", "hash"},
    {"=", 1, {{'=', 0}}, "=", "="},
    {"\xc3\xa9", 1, {{0xe9, 0}}, "\xc3\xa9", "\xc3\xa9"},
    {"alt+\xe7\x95\x8c", 1, {{0x754c, ALT}}, "Alt-\xe7\x95\x8c", "alt+\xe7\x95\x8c"},
    {"pgup pageup pgdn pagedown", 4, {{KT_KEY_PAGE_UP, 0}, {KT_KEY_PAGE_UP, 0}, {KT_KEY_PAGE_DOWN, 0}, {KT_KEY_PAGE_DOWN, 0}},
     "PgUp PgUp PgDn PgDn", "pageup pageup pagedown pagedown"},
    {"del ins return escape", 4, {{KT_KEY_DELETE, 0}, {KT_KEY_INSERT, 0}, {KT_KEY_ENTER, 0}, {KT_KEY_ESCAPE, 0}},
     "Del Ins Enter Esc", "delete insert enter esc"},
    {"left right home end", 4, {{KT_KEY_LEFT, 0}, {KT_KEY_RIGHT, 0}, {KT_KEY_HOME, 0}, {KT_KEY_END, 0}},
     "Left Right Home End", "left right home end"},
    {"TAB Down BACKSPACE", 3, {{KT_KEY_TAB, 0}, {KT_KEY_DOWN, 0}, {KT_KEY_BACKSPACE, 0}}, "Tab Down Backspace", "tab down backspace"},
    {"ctrl+f5 shift+f10", 2, {{KT_KEY_F5, CTRL}, {KT_KEY_F10, SHIFT}}, "Ctrl-F5 Shift-F10", "ctrl+f5 shift+f10"},
};
#define CHORDS (sizeof(chords) / sizeof(chords[0]))

static bool chord_is(const struct kt_chord *chord, const struct chord_case *c) {
    unsigned i;
    if (chord->count != c->count) return false;
    for (i = 0; i < c->count; i++)
        if (chord->keys[i].key != c->keys[i].key || chord->keys[i].mods != c->keys[i].mods) return false;
    return true;
}

/* Every spelling reads as the expected keystrokes, and both written forms
 * read back as the same chord. */
static void parse_and_format(void) {
    size_t i;
    for (i = 0; i < CHORDS; i++) {
        const struct chord_case *c = &chords[i];
        struct kt_chord chord, again;
        char text[128];
        int line = __LINE__ * 1000 + (int)i;
        check(parse(c->text, &chord), "chord parses", line);
        check(chord_is(&chord, c), "chord keystrokes", line);
        check(kt_chord_format(&chord, text, sizeof(text)) == strlen(c->display), "display length", line);
        check(!strcmp(text, c->display), "display text", line);
        check(parse(text, &again), "display parses", line);
        check(kt_chord_equal(&chord, &again), "display round trip", line);
        check(kt_chord_config(&chord, text, sizeof(text)) == strlen(c->config), "config length", line);
        check(!strcmp(text, c->config), "config text", line);
        check(parse(text, &again), "config parses", line);
        check(kt_chord_equal(&chord, &again), "config round trip", line);
    }
}

static void parse_refusals(void) {
    static const char *const bad[] = {
        "", "   ", "\t", "a b c d e", "ctrl+", "ctrl", "shift", "alt+", "foo", "ab", "f0", "f13", "f01", "f123",
        "fx", "shift+1", "shift+space", "shift+]", "ctrl+foo", "ctrl+shift", "tab+ctrl", "a+b", "x-y",
        "\x01", "a \x7f", "\xff", "\xc3", "\xc3\xa9\xc3\xa9", "ctrl+\xc2\x85", "super+x", "ctrl +x"};
    struct kt_chord chord;
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        check(!parse(bad[i], &chord), "malformed chord is refused", __LINE__ * 1000 + (int)i);
    CHECK(!kt_chord_parse("ctrl+] x", 0, &chord));
    /* length bounds the text: what follows it is not read. */
    CHECK(kt_chord_parse("ctrl+] x", 6, &chord));
    CHECK(chord.count == 1);
}

static void format_bounds(void) {
    struct kt_chord chord;
    char text[32];
    CHECK(parse("ctrl+\\ shift+tab", &chord));
    memset(text, 'x', sizeof(text));
    CHECK(kt_chord_format(&chord, text, 0) == 16);
    CHECK(text[0] == 'x');
    CHECK(kt_chord_format(&chord, text, 1) == 16);
    CHECK(text[0] == '\0');
    CHECK(kt_chord_format(&chord, text, 17) == 16);
    CHECK(!strcmp(text, "Ctrl-\\ Shift-Tab"));
    CHECK(kt_chord_format(&chord, text, 16) == 16);
    /* Truncation stops before the piece that does not fit. */
    CHECK(!strcmp(text, "Ctrl-\\ Shift-"));
    CHECK(kt_chord_format(&chord, text, 8) == 16);
    CHECK(!strcmp(text, "Ctrl-\\ "));
    CHECK(kt_chord_config(&chord, text, 6) == 16);
    CHECK(!strcmp(text, "ctrl+"));
    chord.count = 1; chord.keys[0].key = KT_KEY_SEQUENCE; chord.keys[0].mods = 0;
    CHECK(kt_chord_format(&chord, text, sizeof(text)) == 1);
    CHECK(!strcmp(text, "?"));
    chord.count = 0;
    CHECK(kt_chord_format(&chord, text, sizeof(text)) == 0);
    CHECK(text[0] == '\0');
}

static void chord_relations(void) {
    struct kt_chord leader, close, other;
    CHECK(parse("ctrl+\\", &leader));
    CHECK(parse("ctrl+\\ x", &close));
    CHECK(parse("ctrl+\\ X", &other));
    CHECK(kt_chord_extends(&close, &leader));
    CHECK(!kt_chord_extends(&leader, &close));
    CHECK(!kt_chord_extends(&close, &close));
    CHECK(!kt_chord_extends(&close, &other));
    CHECK(kt_chord_equal(&close, &close));
    CHECK(!kt_chord_equal(&close, &other));
    CHECK(!kt_chord_equal(&close, &leader));
}

enum { LEAVE, CLOSE, STATS, LOWER, TRIPLE, OTHER, BOTH, ACTIONS };
enum { ATTACHED = 1, LIST = 2, DIALOG = 4 };
static struct kt_binding bindings[ACTIONS];
static struct kt_keymap map = {bindings, ACTIONS};
static struct kt_keymap_state state;
static const struct kt_binding *matched;

static void bind(int action, const char *name, uint32_t scope, const char *first, const char *second) {
    struct kt_binding *b = &bindings[action];
    memset(b, 0, sizeof(*b));
    b->name = name; b->label = name; b->action = (uint32_t)action; b->scope = scope;
    if (first) check(parse(first, &b->chords[b->chord_count++]), first, __LINE__);
    if (second) check(parse(second, &b->chords[b->chord_count++]), second, __LINE__);
}

static void reset_map(void) {
    bind(LEAVE, "terminal.leave", ATTACHED, "ctrl+]", "ctrl+\\ tab");
    bind(CLOSE, "terminal.close", ATTACHED, "ctrl+\\ x", NULL);
    bind(STATS, "view.statistics", ATTACHED, "ctrl+\\ D", NULL);
    bind(LOWER, "view.lower", ATTACHED, "ctrl+\\ d", NULL);
    bind(TRIPLE, "list.top", LIST, "g g g", NULL);
    bind(OTHER, "list.close", LIST, "ctrl+\\ x", NULL);
    bind(BOTH, "app.help", ATTACHED | LIST, "f1", NULL);
    kt_keymap_reset(&state);
}

static struct kt_event key_event(const char *text) {
    struct kt_chord chord;
    struct kt_event e;
    memset(&e, 0, sizeof(e));
    check(parse(text, &chord), text, __LINE__);
    e.type = KT_KEY; e.key = chord.keys[0].key; e.mods = chord.keys[0].mods;
    return e;
}

static enum kt_key_match press(uint32_t scope, const char *text) {
    struct kt_event e = key_event(text);
    return kt_keymap_feed(&map, &state, scope, &e, &matched);
}

static void feed_single_and_chord(void) {
    reset_map();
    CHECK(press(ATTACHED, "a") == KT_KEY_UNBOUND);
    CHECK(!matched);
    CHECK(!state.pending.count);
    CHECK(press(ATTACHED, "ctrl+]") == KT_KEY_ACTION);
    CHECK(matched == &bindings[LEAVE]);
    CHECK(!state.pending.count);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(!matched);
    CHECK(state.pending.count == 1);
    CHECK(state.pending.keys[0].key == '\\');
    CHECK(press(ATTACHED, "x") == KT_KEY_ACTION);
    CHECK(matched == &bindings[CLOSE]);
    CHECK(!state.pending.count);
    /* The second chord of a binding. */
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "tab") == KT_KEY_ACTION);
    CHECK(matched == &bindings[LEAVE]);
    /* Letters match by case. */
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "D") == KT_KEY_ACTION);
    CHECK(matched == &bindings[STATS]);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "d") == KT_KEY_ACTION);
    CHECK(matched == &bindings[LOWER]);
    /* A modifier is part of the key. */
    CHECK(press(ATTACHED, "]") == KT_KEY_UNBOUND);
    CHECK(press(ATTACHED, "alt+]") == KT_KEY_UNBOUND);
    CHECK(press(ATTACHED, "shift+tab") == KT_KEY_UNBOUND);
}

/* A key that does not continue the chord ends it: everything is consumed
 * and reported, nothing is replayed, and the next key starts afresh. */
static void feed_abandoned(void) {
    struct kt_chord expected;
    reset_map();
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "k") == KT_KEY_ABANDONED);
    CHECK(!matched);
    CHECK(!state.pending.count);
    CHECK(parse("ctrl+\\ k", &expected));
    CHECK(kt_chord_equal(&state.abandoned, &expected));
    CHECK(press(ATTACHED, "k") == KT_KEY_UNBOUND);
    /* The key that ends a chord does not start or complete another one. */
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "ctrl+]") == KT_KEY_ABANDONED);
    CHECK(!matched);
    CHECK(parse("ctrl+\\ ctrl+]", &expected));
    CHECK(kt_chord_equal(&state.abandoned, &expected));
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_ABANDONED);
    CHECK(press(ATTACHED, "x") == KT_KEY_UNBOUND);
    CHECK(press(ATTACHED, "ctrl+]") == KT_KEY_ACTION);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED, "esc") == KT_KEY_ABANDONED);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    kt_keymap_reset(&state);
    CHECK(!state.pending.count);
    CHECK(press(ATTACHED, "x") == KT_KEY_UNBOUND);
}

static void feed_other_events(void) {
    struct kt_event e;
    reset_map();
    memset(&e, 0, sizeof(e));
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    /* Mouse and paste never match, and leave the chord as it is. */
    e.type = KT_MOUSE; e.key = 'x';
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, &matched) == KT_KEY_UNBOUND);
    e.type = KT_PASTE;
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, &matched) == KT_KEY_UNBOUND);
    e.type = KT_PASTE_BEGIN;
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, &matched) == KT_KEY_UNBOUND);
    CHECK(state.pending.count == 1);
    CHECK(!matched);
    CHECK(press(ATTACHED, "x") == KT_KEY_ACTION);
    /* A pasted leave key is not the leave command. */
    e.type = KT_PASTE; e.key = ']'; e.mods = CTRL;
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, &matched) == KT_KEY_UNBOUND);
    /* An unnamed sequence is a key that matches nothing. */
    e.type = KT_KEY; e.key = KT_KEY_SEQUENCE; e.mods = 0;
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, &matched) == KT_KEY_UNBOUND);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, NULL) == KT_KEY_ABANDONED);
    CHECK(state.abandoned.count == 2);
    CHECK(state.abandoned.keys[1].key == KT_KEY_SEQUENCE);
    /* matched may be omitted. */
    e = key_event("ctrl+]");
    CHECK(kt_keymap_feed(&map, &state, ATTACHED, &e, NULL) == KT_KEY_ACTION);
}

static void feed_scopes(void) {
    reset_map();
    CHECK(press(LIST, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(LIST, "x") == KT_KEY_ACTION);
    CHECK(matched == &bindings[OTHER]);
    CHECK(press(LIST, "ctrl+]") == KT_KEY_UNBOUND);
    CHECK(press(DIALOG, "ctrl+\\") == KT_KEY_UNBOUND);
    CHECK(press(DIALOG, "f1") == KT_KEY_UNBOUND);
    CHECK(press(0, "ctrl+]") == KT_KEY_UNBOUND);
    /* A binding live in several scopes; several scopes live at once. */
    CHECK(press(ATTACHED, "f1") == KT_KEY_ACTION);
    CHECK(matched == &bindings[BOTH]);
    CHECK(press(LIST, "f1") == KT_KEY_ACTION);
    CHECK(matched == &bindings[BOTH]);
    CHECK(press(ATTACHED | LIST, "ctrl+\\") == KT_KEY_PENDING);
    CHECK(press(ATTACHED | LIST, "x") == KT_KEY_ACTION);
    CHECK(matched == &bindings[CLOSE]);
    CHECK(press(LIST, "g") == KT_KEY_PENDING);
    CHECK(press(LIST, "g") == KT_KEY_PENDING);
    CHECK(state.pending.count == 2);
    CHECK(press(LIST, "g") == KT_KEY_ACTION);
    CHECK(matched == &bindings[TRIPLE]);
    CHECK(press(LIST, "g") == KT_KEY_PENDING);
    CHECK(press(LIST, "g") == KT_KEY_PENDING);
    CHECK(press(LIST, "x") == KT_KEY_ABANDONED);
    CHECK(state.abandoned.count == 3);
    CHECK(press(ATTACHED, "g") == KT_KEY_UNBOUND);
}

/* An unchecked table with a chord that begins another: the short one wins. */
static void feed_shadowed(void) {
    reset_map();
    bind(LOWER, "view.lower", ATTACHED, "ctrl+\\", NULL);
    CHECK(press(ATTACHED, "ctrl+\\") == KT_KEY_ACTION);
    CHECK(matched == &bindings[LOWER]);
    CHECK(!state.pending.count);
    bind(LOWER, "view.lower", ATTACHED, NULL, NULL);
    state.pending.count = KT_CHORD_MAX + 3;
    CHECK(press(ATTACHED, "ctrl+]") == KT_KEY_ACTION);
}

/* Decoded terminal bytes drive the map as a user's keys do. */
static void feed_decoded_bytes(void) {
    static const char bytes[] = "\x1cx\x1c\t\x1d\x02\x1ck\033[200~\x1d\033[201~";
    static const enum kt_key_match expected[] = {
        KT_KEY_PENDING, KT_KEY_ACTION, KT_KEY_PENDING, KT_KEY_ACTION, KT_KEY_ACTION, KT_KEY_UNBOUND,
        KT_KEY_PENDING, KT_KEY_ABANDONED, KT_KEY_UNBOUND, KT_KEY_UNBOUND, KT_KEY_UNBOUND};
    struct kt_input input;
    struct kt_event e;
    size_t i, events = 0;
    reset_map();
    kt_input_init(&input);
    for (i = 0; i < sizeof(bytes) - 1U; i++) {
        if (!kt_input_feed(&input, (unsigned char)bytes[i], &e)) continue;
        if (events < sizeof(expected) / sizeof(expected[0]))
            check(kt_keymap_feed(&map, &state, ATTACHED, &e, &matched) == expected[events], "decoded key", __LINE__ * 1000 + (int)events);
        events++;
    }
    CHECK(events == sizeof(expected) / sizeof(expected[0]));
}

static bool configure(const char *text, char *error, size_t size) {
    return kt_keymap_configure(&map, text, strlen(text), error, size);
}

static bool bound(int action, unsigned index, const char *chord) {
    struct kt_chord expected;
    return parse(chord, &expected) && index < bindings[action].chord_count &&
        kt_chord_equal(&bindings[action].chords[index], &expected);
}

static void configure_valid(void) {
    char error[160] = "untouched";
    reset_map();
    CHECK(configure("# keys\n\nterminal.leave = ctrl+b tab\n   \n"
                    "terminal.close=none   # no close key\r\n"
                    "  view.statistics  =  f5 ,  Ctrl-B   D  # two chords\r\n"
                    "view.lower = NONE\n"
                    "list.top = comma hash, = =", error, sizeof(error)));
    CHECK(!error[0]);
    CHECK(bindings[LEAVE].chord_count == 1);
    CHECK(bound(LEAVE, 0, "ctrl+b tab"));
    CHECK(bindings[LEAVE].line == 3);
    CHECK(bindings[CLOSE].chord_count == 0);
    CHECK(bindings[CLOSE].line == 5);
    CHECK(bindings[STATS].chord_count == 2);
    CHECK(bound(STATS, 0, "f5"));
    CHECK(bound(STATS, 1, "ctrl+b D"));
    CHECK(bindings[LOWER].chord_count == 0);
    CHECK(bindings[TRIPLE].line == 8);
    CHECK(bound(TRIPLE, 0, ", #"));
    CHECK(bound(TRIPLE, 1, "= ="));
    /* Bindings no line names keep their chords. */
    CHECK(bound(OTHER, 0, "ctrl+\\ x"));
    CHECK(bindings[OTHER].line == 0);
    /* A later line for the same action replaces the earlier one. */
    CHECK(configure("app.help = f2\napp.help = f3\n", error, sizeof(error)));
    CHECK(bindings[BOTH].chord_count == 1);
    CHECK(bound(BOTH, 0, "f3"));
    CHECK(configure("", error, sizeof(error)));
    CHECK(configure("\n\n# only comments\n", error, sizeof(error)));
    CHECK(kt_keymap_configure(&map, "app.help = f4\napp.help = f5\n", 14, NULL, 0));
    CHECK(bound(BOTH, 0, "f4"));
}

struct refusal { const char *text, *error; };

/* Each bad line is named with its number and reason, and changes nothing. */
static void configure_refusals(void) {
    static const struct refusal cases[] = {
        {"terminal.leav = ctrl+]\n", "line 1: unknown action 'terminal.leav'"},
        {"\n\nTERMINAL.LEAVE = x\n", "line 3: unknown action 'TERMINAL.LEAVE'"},
        {"terminal.leave = ctrl+foo\n", "line 1: cannot read chord 'ctrl+foo'"},
        {"terminal.leave = ctrl+], shift+1\n", "line 1: cannot read chord 'shift+1'"},
        {"terminal.leave = a b c d e\n", "line 1: cannot read chord 'a b c d e'"},
        {"# c\nterminal.leave ctrl+]\n", "line 2: expected 'action = chord'"},
        {"terminal.leave = a, b, c\n", "line 1: too many chords for one action"},
        {"terminal.leave =\n", "line 1: no chord given; write none to unbind"},
        {"terminal.leave = a,\n", "line 1: cannot read chord ''"},
        {"terminal.leave = , a\n", "line 1: cannot read chord ''"},
        {"terminal.leave = none, a\n", "line 1: cannot read chord 'none'"},
        {"= ctrl+]\n", "line 1: unknown action ''"},
        {"terminal.leave = x # ok\nnope = y\nterminal.close = ctrl+\nbad\n", "line 2: unknown action 'nope' (and 2 more)"},
    };
    char error[160];
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int line = __LINE__ * 1000 + (int)i;
        reset_map();
        check(!configure(cases[i].text, error, sizeof(error)), "bad line is refused", line);
        if (strcmp(error, cases[i].error)) fprintf(stderr, "got: %s\n", error);
        check(!strcmp(error, cases[i].error), cases[i].error, line);
        check(bound(CLOSE, 0, "ctrl+\\ x"), "refused line leaves its binding", line);
        check(bindings[CLOSE].line == 0, "refused line is not recorded", line);
    }
    /* Good lines around a bad one are applied. */
    reset_map();
    CHECK(!configure("terminal.leave = x # ok\nnope = y\nterminal.close = ctrl+\nview.lower = f7\n", error, sizeof(error)));
    CHECK(bound(LEAVE, 0, "x"));
    CHECK(bindings[LEAVE].chord_count == 1);
    CHECK(bound(LOWER, 0, "f7"));
    CHECK(bound(CLOSE, 0, "ctrl+\\ x"));
    /* A short error buffer is filled safely; none at all is allowed. */
    CHECK(!configure("nope = y\n", error, 8));
    CHECK(!strcmp(error, "line 1:"));
    CHECK(!kt_keymap_configure(&map, "nope = y\n", 9, NULL, 0));
}

static void check_conflicts(void) {
    char error[200] = "untouched";
    reset_map();
    CHECK(kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!error[0]);
    /* The same chord in different scopes is fine; in a shared scope it is not. */
    bind(LOWER, "view.lower", ATTACHED, "ctrl+\\ x", NULL);
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "terminal.close and view.lower both use Ctrl-\\ x"));
    bind(LOWER, "view.lower", DIALOG, "ctrl+\\ x", NULL);
    CHECK(kt_keymap_check(&map, error, sizeof(error)));
    bind(LOWER, "view.lower", DIALOG | LIST, "ctrl+\\ x", NULL);
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "view.lower and list.close both use Ctrl-\\ x"));
    /* A chord that begins another, whichever comes first in the table. */
    bind(LOWER, "view.lower", ATTACHED, "ctrl+\\", NULL);
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "view.lower uses Ctrl-\\, which begins Ctrl-\\ Tab of terminal.leave"));
    bind(LOWER, "view.lower", ATTACHED, "ctrl+] q", NULL);
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "terminal.leave uses Ctrl-], which begins Ctrl-] q of view.lower"));
    bind(LOWER, "view.lower", ATTACHED, "f9", "f9");
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "view.lower lists F9 twice"));
    bind(LOWER, "view.lower", ATTACHED, "f9", "f9 a");
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "view.lower uses F9, which begins F9 a of view.lower"));
    bind(LOWER, "view.lower", ATTACHED, "f9", "f8");
    CHECK(kt_keymap_check(&map, error, sizeof(error)));
    /* Bindings a configuration line set are named with it. */
    CHECK(configure("\nview.lower = f1\n", error, sizeof(error)));
    CHECK(!kt_keymap_check(&map, error, sizeof(error)));
    CHECK(!strcmp(error, "view.lower (line 2) and app.help both use F1"));
    CHECK(!kt_keymap_check(&map, error, 12));
    CHECK(!strcmp(error, "view.lower"));
    CHECK(!kt_keymap_check(&map, NULL, 0));
}

/* The written table configures a second table to the same chords. */
static void binding_lines(void) {
    static struct kt_binding copy[ACTIONS];
    struct kt_keymap second = {copy, ACTIONS};
    char text[1024] = "", line[160], error[160];
    size_t i;
    reset_map();
    bind(LOWER, "view.lower", ATTACHED, NULL, NULL);
    bind(TRIPLE, "list.top", LIST, ", #", "space +");
    CHECK(kt_binding_config(&bindings[LEAVE], line, sizeof(line)) == strlen("terminal.leave = ctrl+], ctrl+\\ tab"));
    CHECK(!strcmp(line, "terminal.leave = ctrl+], ctrl+\\ tab"));
    CHECK(kt_binding_config(&bindings[LOWER], line, sizeof(line)) == strlen("view.lower = none"));
    CHECK(!strcmp(line, "view.lower = none"));
    CHECK(kt_binding_config(&bindings[TRIPLE], line, sizeof(line)) > 0);
    CHECK(!strcmp(line, "list.top = comma hash, space plus"));
    CHECK(kt_binding_config(&bindings[LEAVE], line, 10) == strlen("terminal.leave = ctrl+], ctrl+\\ tab"));
    CHECK(strlen(line) < 10);
    memcpy(copy, bindings, sizeof(copy));
    for (i = 0; i < ACTIONS; i++) {
        copy[i].chord_count = 1;
        check(parse("f12", &copy[i].chords[0]), "placeholder", __LINE__);
        (void)kt_binding_config(&bindings[i], line, sizeof(line));
        snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s   # %s\n", line, bindings[i].label);
    }
    CHECK(kt_keymap_configure(&second, text, strlen(text), error, sizeof(error)));
    for (i = 0; i < ACTIONS; i++) {
        unsigned c;
        check(copy[i].chord_count == bindings[i].chord_count, "written chord count", __LINE__ * 1000 + (int)i);
        for (c = 0; c < bindings[i].chord_count; c++)
            check(kt_chord_equal(&copy[i].chords[c], &bindings[i].chords[c]), "written chord", __LINE__ * 1000 + (int)i);
    }
    CHECK(kt_keymap_find(&map, "terminal.close") == &bindings[CLOSE]);
    CHECK(!kt_keymap_find(&map, "terminal.clos"));
    CHECK(!kt_keymap_find(&map, "terminal.close "));
}

int main(void) {
    parse_and_format();
    parse_refusals();
    format_bounds();
    chord_relations();
    feed_single_and_chord();
    feed_abandoned();
    feed_other_events();
    feed_scopes();
    feed_shadowed();
    feed_decoded_bytes();
    configure_valid();
    configure_refusals();
    check_conflicts();
    binding_lines();
    if (failures) { fprintf(stderr, "keymap: %d checks failed\n", failures); return 1; }
    puts("keymap: chord spelling and round trips, case, scopes, pending and abandoned chords, decoded bytes, "
         "configuration lines and refusals, conflicts and written tables passed");
    return 0;
}
