#include "kettle/input.h"
#include "kettle/tree.h"
#include <stdio.h>
#include <string.h>

static struct kt_input input;
static struct kt_event event;
static int failures;

static void check(bool ok, const char *what, int line) {
    if (ok) return;
    fprintf(stderr, "tests/c/test_kettle_input.c:%d: %s\n", line, what);
    failures++;
}
#define CHECK(condition) check((condition), #condition, __LINE__)

/* Delivers bytes one at a time; true when the last byte produced an event. */
static bool send_n(const char *bytes, size_t length) {
    size_t i;
    bool got = false;
    for (i = 0; i < length; i++) got = kt_input_feed(&input, (unsigned char)bytes[i], &event);
    return got;
}
static bool send(const char *bytes) { return send_n(bytes, strlen(bytes)); }

static bool raw_is(const char *bytes, size_t length) {
    return event.length == length && !memcmp(event.bytes, bytes, length) && event.bytes[length] == '\0';
}

struct key_case { const char *bytes; size_t length; uint32_t key; unsigned mods; };
#define K(text, key, mods) {text, sizeof(text) - 1U, key, mods}
#define SHIFT KT_MOD_SHIFT
#define ALT KT_MOD_ALT
#define CTRL KT_MOD_CTRL

/* One complete key, delivered byte by byte into a fresh decoder: nothing is
 * reported before the last byte, and the event carries the exact input. */
static void expect_key(const struct key_case *c, int line) {
    size_t i;
    unsigned events = 0;
    kt_input_init(&input);
    for (i = 0; i < c->length; i++) {
        bool got = kt_input_feed(&input, (unsigned char)c->bytes[i], &event);
        if (got) events++;
        if (got && i + 1 < c->length) check(false, "event before the sequence ended", line);
    }
    check(events == 1, "exactly one event", line);
    check(event.type == KT_KEY, "key event", line);
    check(event.key == c->key, "key", line);
    check(event.mods == c->mods, "modifiers", line);
    check(raw_is(c->bytes, c->length), "raw bytes", line);
    check(!kt_input_flush(&input, &event), "nothing left to flush", line);
}

static void expect_keys(const struct key_case *cases, size_t count, int line) {
    size_t i;
    for (i = 0; i < count; i++) expect_key(&cases[i], line * 1000 + (int)i);
}
#define KEYS(cases) expect_keys((cases), sizeof(cases) / sizeof((cases)[0]), __LINE__)

static void control_bytes(void) {
    static const struct key_case cases[] = {
        K("\x00", ' ', CTRL), K("\x01", 'a', CTRL), K("\x02", 'b', CTRL), K("\x03", 'c', CTRL),
        K("\x08", 'h', CTRL), K("\x0a", 'j', CTRL), K("\x1a", 'z', CTRL), K("\x1c", '\\', CTRL),
        K("\x1d", ']', CTRL), K("\x1e", '^', CTRL), K("\x1f", '_', CTRL),
        K("\t", KT_KEY_TAB, 0), K("\r", KT_KEY_ENTER, 0), K("\x7f", KT_KEY_BACKSPACE, 0)};
    unsigned char byte;
    KEYS(cases);
    /* Every other C0 byte is its letter with Ctrl. */
    for (byte = 1; byte <= 26; byte++) {
        char text[1];
        struct key_case c = {text, 1, (uint32_t)('a' + byte - 1), CTRL};
        text[0] = (char)byte;
        if (byte != '\t' && byte != '\r') expect_key(&c, __LINE__);
    }
}

static void printable_keys(void) {
    static const struct key_case cases[] = {
        K("a", 'a', 0), K("D", 'D', 0), K(" ", ' ', 0), K("~", '~', 0), K("[", '[', 0), K("O", 'O', 0),
        K("\xc3\xa9", 0xe9, 0), K("\xe7\x95\x8c", 0x754c, 0), K("\xf0\x9f\x98\x80", 0x1f600, 0)};
    KEYS(cases);
}

static void named_keys(void) {
    static const struct key_case cases[] = {
        K("\033[A", KT_KEY_UP, 0), K("\033[B", KT_KEY_DOWN, 0), K("\033[C", KT_KEY_RIGHT, 0),
        K("\033[D", KT_KEY_LEFT, 0), K("\033[H", KT_KEY_HOME, 0), K("\033[F", KT_KEY_END, 0),
        K("\033OA", KT_KEY_UP, 0), K("\033OB", KT_KEY_DOWN, 0), K("\033OC", KT_KEY_RIGHT, 0),
        K("\033OD", KT_KEY_LEFT, 0), K("\033OH", KT_KEY_HOME, 0), K("\033OF", KT_KEY_END, 0),
        K("\033[1~", KT_KEY_HOME, 0), K("\033[7~", KT_KEY_HOME, 0), K("\033[4~", KT_KEY_END, 0),
        K("\033[8~", KT_KEY_END, 0), K("\033[2~", KT_KEY_INSERT, 0), K("\033[3~", KT_KEY_DELETE, 0),
        K("\033[5~", KT_KEY_PAGE_UP, 0), K("\033[6~", KT_KEY_PAGE_DOWN, 0),
        K("\033[Z", KT_KEY_TAB, SHIFT)};
    KEYS(cases);
}

static void function_keys(void) {
    static const struct key_case cases[] = {
        K("\033OP", KT_KEY_F1, 0), K("\033OQ", KT_KEY_F2, 0), K("\033OR", KT_KEY_F3, 0), K("\033OS", KT_KEY_F4, 0),
        K("\033[11~", KT_KEY_F1, 0), K("\033[12~", KT_KEY_F2, 0), K("\033[13~", KT_KEY_F3, 0),
        K("\033[14~", KT_KEY_F4, 0), K("\033[15~", KT_KEY_F5, 0), K("\033[17~", KT_KEY_F6, 0),
        K("\033[18~", KT_KEY_F7, 0), K("\033[19~", KT_KEY_F8, 0), K("\033[20~", KT_KEY_F9, 0),
        K("\033[21~", KT_KEY_F10, 0), K("\033[23~", KT_KEY_F11, 0), K("\033[24~", KT_KEY_F12, 0),
        K("\033[P", KT_KEY_F1, 0), K("\033[1;2P", KT_KEY_F1, SHIFT), K("\033[1;5S", KT_KEY_F4, CTRL)};
    KEYS(cases);
}

static void modified_keys(void) {
    static const struct key_case cases[] = {
        K("\033[1;1A", KT_KEY_UP, 0), K("\033[1;2A", KT_KEY_UP, SHIFT), K("\033[1;3B", KT_KEY_DOWN, ALT),
        K("\033[1;4C", KT_KEY_RIGHT, SHIFT | ALT), K("\033[1;5C", KT_KEY_RIGHT, CTRL),
        K("\033[1;6D", KT_KEY_LEFT, CTRL | SHIFT), K("\033[1;7H", KT_KEY_HOME, CTRL | ALT),
        K("\033[1;8F", KT_KEY_END, CTRL | ALT | SHIFT), K("\033[1;9A", KT_KEY_UP, ALT),
        K("\033[1;16A", KT_KEY_UP, CTRL | ALT | SHIFT),
        K("\033[3;5~", KT_KEY_DELETE, CTRL), K("\033[2;2~", KT_KEY_INSERT, SHIFT),
        K("\033[5;2~", KT_KEY_PAGE_UP, SHIFT), K("\033[6;3~", KT_KEY_PAGE_DOWN, ALT),
        K("\033[1;5~", KT_KEY_HOME, CTRL), K("\033[15;5~", KT_KEY_F5, CTRL),
        K("\033[24;4~", KT_KEY_F12, ALT | SHIFT), K("\033[1;2Z", KT_KEY_TAB, SHIFT),
        K("\033O5P", KT_KEY_F1, CTRL), K("\033O1;2Q", KT_KEY_F2, SHIFT), K("\033O2A", KT_KEY_UP, SHIFT)};
    KEYS(cases);
}

static void alt_keys(void) {
    static const struct key_case cases[] = {
        K("\033a", 'a', ALT), K("\033D", 'D', ALT), K("\033 ", ' ', ALT), K("\033]", ']', ALT),
        K("\033\r", KT_KEY_ENTER, ALT), K("\033\t", KT_KEY_TAB, ALT), K("\033\x7f", KT_KEY_BACKSPACE, ALT),
        K("\033\x02", 'b', CTRL | ALT), K("\033\x1c", '\\', CTRL | ALT), K("\033\x00", ' ', CTRL | ALT),
        K("\033\xc3\xa9", 0xe9, ALT), K("\033\xf0\x9f\x98\x80", 0x1f600, ALT)};
    KEYS(cases);
}

/* Complete sequences the decoder does not name keep their bytes. */
static void unnamed_sequences(void) {
    static const struct key_case cases[] = {
        K("\033[1;0A", KT_KEY_SEQUENCE, 0), K("\033[1;17A", KT_KEY_SEQUENCE, 0), K("\033[2A", KT_KEY_SEQUENCE, 0),
        K("\033[1;2;3A", KT_KEY_SEQUENCE, 0), K("\033[;A", KT_KEY_SEQUENCE, 0), K("\033[1;A", KT_KEY_SEQUENCE, 0),
        K("\033[?1;2c", KT_KEY_SEQUENCE, 0), K("\033[>0;10;1c", KT_KEY_SEQUENCE, 0),
        K("\033[9~", KT_KEY_SEQUENCE, 0), K("\033[16~", KT_KEY_SEQUENCE, 0), K("\033[25~", KT_KEY_SEQUENCE, 0),
        K("\033[99999~", KT_KEY_SEQUENCE, 0), K("\033[~", KT_KEY_SEQUENCE, 0), K("\033[3;0~", KT_KEY_SEQUENCE, 0),
        K("\033[3;5;1~", KT_KEY_SEQUENCE, 0), K("\033O3~", KT_KEY_SEQUENCE, 0), K("\033[a", KT_KEY_SEQUENCE, 0),
        K("\033[0n", KT_KEY_SEQUENCE, 0), K("\033[12;40R", KT_KEY_SEQUENCE, 0), K("\033[ q", KT_KEY_SEQUENCE, 0),
        K("\033Ox", KT_KEY_SEQUENCE, 0), K("\033O17P", KT_KEY_SEQUENCE, 0), K("\033[1:2A", KT_KEY_SEQUENCE, 0),
        K("\033[[", KT_KEY_SEQUENCE, 0)};
    KEYS(cases);
}

/* A timeout tells a lone Escape, and Alt with '[' or 'O', from a sequence. */
static void flush_cases(void) {
    kt_input_init(&input);
    CHECK(!send("\033"));
    CHECK(kt_input_flush(&input, &event));
    CHECK(kt_event_is(&event, KT_KEY_ESCAPE, 0));
    CHECK(raw_is("\033", 1));
    CHECK(!kt_input_flush(&input, &event));
    CHECK(!send("\033["));
    CHECK(kt_input_flush(&input, &event));
    CHECK(kt_event_is(&event, '[', ALT));
    CHECK(raw_is("\033[", 2));
    CHECK(!send("\033O"));
    CHECK(kt_input_flush(&input, &event));
    CHECK(kt_event_is(&event, 'O', ALT));
    CHECK(raw_is("\033O", 2));
    /* A longer unfinished sequence is dropped, and the decoder starts afresh. */
    CHECK(!send("\033[1;"));
    CHECK(!kt_input_flush(&input, &event));
    CHECK(send("x"));
    CHECK(kt_event_is(&event, 'x', 0));
    CHECK(!send("\033O1"));
    CHECK(!kt_input_flush(&input, &event));
    CHECK(send("y"));
    CHECK(kt_event_is(&event, 'y', 0));
}

static void flush_partial_text(void) {
    kt_input_init(&input);
    CHECK(!send("\xc3"));
    CHECK(kt_input_flush(&input, &event));
    CHECK(kt_event_is(&event, '?', 0));
    CHECK(raw_is("\xc3", 1));
    CHECK(!send("\033\xe7\x95"));
    CHECK(kt_input_flush(&input, &event));
    CHECK(kt_event_is(&event, '?', ALT));
    CHECK(raw_is("\033\xe7\x95", 3));
    /* An invalid continuation ends the scalar with every byte kept. */
    CHECK(send("\xc3x"));
    CHECK(kt_event_is(&event, '?', 0));
    CHECK(raw_is("\xc3x", 2));
    CHECK(send("\xff"));
    CHECK(kt_event_is(&event, '?', 0));
    /* A sequence split by a timeout becomes separate keys, bytes intact. */
    CHECK(!send("\033"));
    CHECK(kt_input_flush(&input, &event));
    CHECK(send("["));
    CHECK(kt_event_is(&event, '[', 0));
    CHECK(send("A"));
    CHECK(kt_event_is(&event, 'A', 0));
}

/* Escape always starts a new sequence and ends an unfinished one. */
static void escape_restarts(void) {
    kt_input_init(&input);
    CHECK(!send("\033"));
    CHECK(kt_input_feed(&input, 0x1b, &event));
    CHECK(kt_event_is(&event, KT_KEY_ESCAPE, 0));
    CHECK(raw_is("\033", 1));
    CHECK(send("[A"));
    CHECK(kt_event_is(&event, KT_KEY_UP, 0));
    CHECK(raw_is("\033[A", 3));
    CHECK(!send("\033["));
    CHECK(kt_input_feed(&input, 0x1b, &event));
    CHECK(kt_event_is(&event, '[', ALT));
    CHECK(send("[C"));
    CHECK(kt_event_is(&event, KT_KEY_RIGHT, 0));
    CHECK(!send("\033[1;"));
    CHECK(!kt_input_feed(&input, 0x1b, &event));
    CHECK(send("[B"));
    CHECK(kt_event_is(&event, KT_KEY_DOWN, 0));
    CHECK(!send("\xe7\x95"));
    CHECK(kt_input_feed(&input, 0x1b, &event));
    CHECK(kt_event_is(&event, '?', 0));
    CHECK(raw_is("\xe7\x95", 2));
    CHECK(send("x"));
    CHECK(kt_event_is(&event, 'x', ALT));
}

static void overlong_sequences(void) {
    size_t i;
    kt_input_init(&input);
    CHECK(!send("\033["));
    for (i = 0; i < 200; i++) CHECK(!kt_input_feed(&input, '9', &event));
    CHECK(!send("M"));
    CHECK(send("j"));
    CHECK(kt_event_is(&event, 'j', 0));
    /* Exactly at the buffer bound, with and without a final byte. */
    CHECK(!send("\033["));
    for (i = 0; i < 61; i++) CHECK(!kt_input_feed(&input, '1', &event));
    CHECK(!send("A"));
    CHECK(send("k"));
    CHECK(kt_event_is(&event, 'k', 0));
    CHECK(!send("\033["));
    for (i = 0; i < 60; i++) CHECK(!kt_input_feed(&input, '1', &event));
    CHECK(send("A"));
    CHECK(event.key == KT_KEY_SEQUENCE);
    CHECK(event.length == 63);
    /* Escape ends a drain as well: the key after it is not lost. */
    CHECK(!send("\033["));
    for (i = 0; i < 200; i++) CHECK(!kt_input_feed(&input, ';', &event));
    CHECK(send("\033[A"));
    CHECK(kt_event_is(&event, KT_KEY_UP, 0));
    CHECK(!send("\033["));
    for (i = 0; i < 200; i++) CHECK(!kt_input_feed(&input, '9', &event));
    CHECK(!kt_input_flush(&input, &event));
    CHECK(send("z"));
    CHECK(kt_event_is(&event, 'z', 0));
}

static void mouse_reports(void) {
    kt_input_init(&input);
    CHECK(!send("\033[<0;"));
    CHECK(send("12;3M"));
    CHECK(event.type == KT_MOUSE);
    CHECK(event.x == 11);
    CHECK(event.y == 2);
    CHECK(raw_is("\033[<0;12;3M", 10));
    CHECK(send("\033[<32;23;8M"));
    CHECK(event.button == 32);
    CHECK(send("\033[<0;23;8m"));
    CHECK(event.release);
    CHECK(!send("\033[<0;999999999999;8M"));
    CHECK(!send("\033[<0;0;8M"));
    CHECK(!send("\033[<0;1M"));
    CHECK(!send("\033[Mqqq")); /* legacy mouse payload cannot inject quit */
    CHECK(send("x"));
    CHECK(kt_event_is(&event, 'x', 0));
    CHECK(!kt_event_is(&event, 'x', CTRL));
}

struct paste_case { const char *bytes; size_t length; uint32_t key; };
#define P(text, key) {text, sizeof(text) - 1U, key}

/* Whatever a paste holds stays paste: no key event, so no command. */
static void paste_content(void) {
    static const struct paste_case cases[] = {
        P("q", 'q'), P("\x1d", ']'), P("\x1c", '\\'), P("\x02", 'b'), P("\r", KT_KEY_ENTER),
        P("\033[A", KT_KEY_SEQUENCE), P("\033[1;5C", KT_KEY_SEQUENCE), P("\033OP", KT_KEY_SEQUENCE),
        P("\033x", 'x'), P("\033[<0;1;1M", KT_KEY_SEQUENCE), P("\033[Z", KT_KEY_SEQUENCE),
        P("\xc3\xa9", 0xe9)};
    size_t i;
    kt_input_init(&input);
    CHECK(send("\033[200~"));
    CHECK(event.type == KT_PASTE_BEGIN);
    CHECK(raw_is("\033[200~", 6));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check(send_n(cases[i].bytes, cases[i].length), "paste content event", __LINE__ * 1000 + (int)i);
        check(event.type == KT_PASTE, "content stays paste", __LINE__ * 1000 + (int)i);
        check(event.key == cases[i].key, "paste key", __LINE__ * 1000 + (int)i);
        check(raw_is(cases[i].bytes, cases[i].length), "paste bytes", __LINE__ * 1000 + (int)i);
        check(!kt_event_is(&event, cases[i].key, event.mods), "paste is never a key", __LINE__ * 1000 + (int)i);
        check(!kt_event_text(&event), "paste is never typed text", __LINE__ * 1000 + (int)i);
    }
    /* A legacy mouse prefix inside a paste does not swallow what follows. */
    CHECK(send("\033[M"));
    CHECK(event.type == KT_PASTE);
    CHECK(send("q"));
    CHECK(event.type == KT_PASTE);
    CHECK(send("\033[201~"));
    CHECK(event.type == KT_PASTE_END);
    CHECK(send("q"));
    CHECK(kt_event_is(&event, 'q', 0));
}

/* The end marker is found after content that ends in Escape or in an
 * unfinished sequence, so a paste cannot leave the decoder stuck in it. */
static void paste_end(void) {
    kt_input_init(&input);
    CHECK(send("\033[200~"));
    CHECK(!send("\033"));
    CHECK(kt_input_feed(&input, 0x1b, &event));
    CHECK(event.type == KT_PASTE);
    CHECK(raw_is("\033", 1));
    CHECK(send("[201~"));
    CHECK(event.type == KT_PASTE_END);
    CHECK(send("\033[200~"));
    CHECK(!send("\033O"));
    CHECK(kt_input_feed(&input, 0x1b, &event));
    CHECK(event.type == KT_PASTE);
    CHECK(send("[201~"));
    CHECK(event.type == KT_PASTE_END);
    CHECK(send("\033[200~"));
    CHECK(!send("\033[12"));
    CHECK(!kt_input_feed(&input, 0x1b, &event));
    CHECK(send("[201~"));
    CHECK(event.type == KT_PASTE_END);
    /* A timeout inside a paste reports paste too. */
    CHECK(send("\033[200~"));
    CHECK(!send("\033"));
    CHECK(kt_input_flush(&input, &event));
    CHECK(event.type == KT_PASTE);
    CHECK(send("\033[201~"));
    CHECK(event.type == KT_PASTE_END);
    CHECK(send("\x1d"));
    CHECK(kt_event_is(&event, ']', CTRL));
}

/* Events of a mixed stream, joined, are the stream: nothing is rewritten. */
static void raw_stream(void) {
    static const char stream[] = "a\x02\033[A\033x\xc3\xa9\033[1;5C\t\r\x7f\033OP\033[<0;3;4M\033[200~h\033i\033[201~"
        "\033[?1;2c\x1c\x1d\033\r\033[24;4~Z";
    char joined[sizeof(stream)];
    size_t i, used = 0;
    kt_input_init(&input);
    for (i = 0; i < sizeof(stream) - 1U; i++) {
        if (!kt_input_feed(&input, (unsigned char)stream[i], &event)) continue;
        memcpy(joined + used, event.bytes, event.length);
        used += event.length;
    }
    CHECK(used == sizeof(stream) - 1U);
    CHECK(!memcmp(joined, stream, used));
    CHECK(!kt_input_flush(&input, &event));
}

static void event_predicates(void) {
    kt_input_init(&input);
    CHECK(send("a"));
    CHECK(kt_event_text(&event));
    CHECK(send("\xc3\xa9"));
    CHECK(kt_event_text(&event));
    CHECK(send(" "));
    CHECK(kt_event_text(&event));
    CHECK(send("\x02"));
    CHECK(!kt_event_text(&event));
    CHECK(send("\033a"));
    CHECK(!kt_event_text(&event));
    CHECK(send("\t"));
    CHECK(!kt_event_text(&event));
    CHECK(send("\033[A"));
    CHECK(!kt_event_text(&event));
    CHECK(send("\033[<0;1;1M"));
    CHECK(!kt_event_is(&event, event.key, event.mods));
    CHECK(send("\033[Z"));
    CHECK(kt_event_is(&event, KT_KEY_TAB, SHIFT));
    CHECK(!kt_event_is(&event, KT_KEY_TAB, 0));
}

static void tree_cases(void) {
    struct kt_tree tree;
    struct kt_tree_node nodes[] = {{"Root", 0, 0, true, KT_BASE}, {"Child", 1, 1, true, KT_BASE},
        {"Nested", 2, 2, false, KT_BASE}, {"Other", 3, 0, false, KT_BASE}};
    struct kt_cell storage[40];
    struct kt_canvas c;
    size_t scroll = 0;
    CHECK(kt_tree_init(&tree, nodes, 4));
    kt_tree_expand(&tree, true); CHECK(tree.selected == 1);
    kt_tree_move(&tree, 1); CHECK(tree.selected == 2);
    kt_tree_expand(&tree, false); CHECK(tree.selected == 1);
    kt_tree_expand(&tree, false); CHECK(!nodes[1].expanded);
    kt_tree_move(&tree, 1); CHECK(tree.selected == 3);
    CHECK(kt_init(&c, storage, 40, 20, 2, true));
    kt_tree_draw(&tree, &c, &scroll, true); CHECK(scroll == 1);
    CHECK(kt_tree_click(&tree, scroll, 0));
    CHECK(tree.selected == 1);
    nodes[1].depth = 3; CHECK(!kt_tree_init(&tree, nodes, 4));
}

/* kt_tree_scroll is the scroll kt_tree_draw settles on, without drawing. */
static void tree_scroll(void) {
    struct kt_tree tree;
    struct kt_tree_node nodes[] = {{"a", 0, 0, true, KT_BASE}, {"b", 1, 1, true, KT_BASE},
        {"c", 2, 1, true, KT_BASE}, {"d", 3, 0, true, KT_BASE}, {"e", 4, 0, true, KT_BASE}};
    struct kt_cell storage[20];
    struct kt_canvas c;
    size_t scroll = 0;
    CHECK(kt_tree_init(&tree, nodes, 5));
    CHECK(kt_tree_scroll(&tree, 0, 2) == 0);
    tree.selected = 4;
    CHECK(kt_tree_scroll(&tree, 0, 2) == 3);
    CHECK(kt_tree_scroll(&tree, 4, 2) == 4);
    CHECK(kt_tree_scroll(&tree, 9, 2) == 4);
    nodes[0].expanded = false;
    CHECK(kt_tree_scroll(&tree, 0, 2) == 1);
    tree.selected = 2; /* hidden by its collapsed parent: shown as that parent */
    CHECK(kt_tree_scroll(&tree, 3, 2) == 0);
    tree.selected = 4;
    CHECK(kt_init(&c, storage, 20, 10, 2, true));
    kt_tree_draw(&tree, &c, &scroll, true);
    CHECK(scroll == 1);
    CHECK(kt_tree_scroll(&tree, 9, 0) == 9);
}

int main(void) {
    control_bytes();
    printable_keys();
    named_keys();
    function_keys();
    modified_keys();
    alt_keys();
    unnamed_sequences();
    flush_cases();
    flush_partial_text();
    escape_restarts();
    overlong_sequences();
    mouse_reports();
    paste_content();
    paste_end();
    raw_stream();
    event_predicates();
    tree_cases();
    tree_scroll();
    if (failures) { fprintf(stderr, "input/tree: %d checks failed\n", failures); return 1; }
    puts("input/tree: control, named, function, modified and Alt keys byte by byte, timeouts, Escape restarts, "
         "fragmented mouse, paste isolation and end, legacy/overlong input, raw bytes, focus, collapse and "
         "selection bounds passed");
    return 0;
}
