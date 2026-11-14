/* The control centre's input boundary, driven through the real loop with the
 * runtime's headless driver: bytes go in as a terminal would send them, the
 * runtime decodes them, and the application's own update routes the events.
 * No terminal, no real time: the Escape and sequence waits are advanced. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../../src/tui/internal.h"
#include "../../src/kettle/testing.h"

struct fixture { struct app *app; struct kt_test *test; };

static bool fixture_open(struct fixture *f) {
    struct kt_options options;
    f->test = NULL;
    f->app = calloc(1U, sizeof(*f->app));
    if (!f->app) return false;
    /* Observations fail at once instead of running a real hydra. */
    f->app->hydra = "/nonexistent/hydra";
    f->app->cols = 80; f->app->rows = 24;
    f->app->no_color = true;
    tui_options(f->app, &options);
    f->test = kt_test_new(&tui_app, f->app, 80, 24, &options);
    if (f->test) f->app->program = kt_test_program(f->test);
    return f->test != NULL;
}

static void fixture_close(struct fixture *f) {
    struct app *app = f->app;
    kt_test_free(f->test);
    if (!app) return;
    app->program = NULL; app->cmds = NULL;
    free(app->workflows);
    native_workspace_destroy(app);
    native_terminals_destroy(app);
    native_observations_destroy(app);
    native_attention_destroy(app);
    statistics_destroy(app);
    transcript_free(app->transcript);
    frame_free(app);
    free(app);
}

/* One head, so keys that act on heads have something to act on. */
static void one_head(struct app *app) {
    app->model.head_count = 1;
    copy_text(app->model.heads[0].branch, TEXT, "feature-one");
    copy_text(app->model.heads[0].head_id, sizeof(app->model.heads[0].head_id), "head_one");
}

static void type(struct fixture *f, const char *bytes) { kt_test_type(f->test, bytes); }

/* Hydra's chords need no agent view: with nothing attached the leader is
 * held and shown, a key that completes nothing is named and dropped, and the
 * case of a letter decides the command. */
static bool chords_without_agent(void) {
    struct fixture f;
    struct app *app;
    char text[512];
    bool ok = fixture_open(&f);
    app = f.app;
    ok = ok && !key_pending_text(app, text, sizeof(text));
    if (ok) type(&f, "\x1c");
    ok = ok && key_pending_text(app, text, sizeof(text)) && kt_test_running(f.test);
    ok = ok && !strncmp(text, "Ctrl-\\ ", 7) && strstr(text, "Tab leave input") && strstr(text, "q quit");
    ok = ok && kt_test_contains(f.test, "Ctrl-\\ ");
    if (ok) type(&f, "Q");
    ok = ok && !key_pending_text(app, text, sizeof(text)) && kt_test_running(f.test);
    ok = ok && !strcmp(app->notice, "Ctrl-\\ Q is not bound; nothing was sent to the agent");
    if (ok) type(&f, "\x1c\x1c");
    ok = ok && !key_pending_text(app, text, sizeof(text)) && kt_test_running(f.test) && !app->terminals;
    if (ok) type(&f, "\x1cq");
    ok = ok && !kt_test_running(f.test) && kt_test_status(f.test) == 0;
    fixture_close(&f);
    return ok;
}

/* Hint text follows the bindings: a rebound action shows its new chord, an
 * unbound one falls back or is left out, and the first variant that fits wins. */
static bool hints_follow_keys(void) {
    static const char *const variants[] = {
        "{terminal.leave} leaves input|{terminal.close} closes the view", "{terminal.leave} leave", "L"};
    static const char keys[] = "terminal.leave = f2\nterminal.reconnect = none\n";
    struct app *app = calloc(1U, sizeof(*app));
    char text[256], error[128];
    bool ok;
    if (!app) return false;
    ok = kt_keymap_configure(&native_keys(app)->map, keys, sizeof(keys) - 1U, error, sizeof(error));
    key_hint(app, text, sizeof(text), " / ",
             "A|a or {terminal.reconnect} reconnect~a reconnect|{terminal.close} close|{terminal.reconnect} again|{no.such} key|B");
    ok = ok && !strcmp(text, "A / a reconnect / Ctrl-\\ x close / B");
    key_hint_fit(app, text, sizeof(text), "  ", variants, 3, 41);
    ok = ok && !strcmp(text, "F2 leaves input  Ctrl-\\ x closes the view");
    key_hint_fit(app, text, sizeof(text), "  ", variants, 3, 40);
    ok = ok && !strcmp(text, "F2 leave");
    key_hint_fit(app, text, sizeof(text), "  ", variants, 3, 0);
    ok = ok && !strcmp(text, "L");
    key_hint(app, text, 6, " / ", "{terminal.close} closes");
    ok = ok && strlen(text) == 5;
    ok = ok && key_text(app, KEY_TERMINAL_LEAVE, text, sizeof(text)) && !strcmp(text, "F2");
    ok = ok && !key_text(app, KEY_TERMINAL_RECONNECT, text, sizeof(text)) && !text[0];
    free(app);
    return ok;
}

/* A paste, however long and whatever it holds, runs no command; the key
 * after it does. A mouse report too long to be one is drained the same way. */
static bool oversized_input(bool paste) {
    static char data[8300];
    const char *prefix = paste ? "\033[200~" : "\033[<0;";
    const char *suffix = paste ? "q:kill\r\033[201~" : ";5q";
    size_t prefix_size = strlen(prefix), suffix_size = strlen(suffix), offset;
    struct fixture f;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    memset(data, '9', sizeof(data));
    memcpy(data, prefix, prefix_size);
    memcpy(data + sizeof(data) - suffix_size, suffix, suffix_size);
    /* In pieces, as a terminal delivers it, with pauses longer than any key wait. */
    for (offset = 0; offset < sizeof(data); offset += 100U) {
        size_t piece = sizeof(data) - offset < 100U ? sizeof(data) - offset : 100U;
        kt_test_input(f.test, data + offset, piece);
        if (paste) kt_test_advance(f.test, 500);
    }
    ok = kt_test_running(f.test) && f.app->view == 0 && !f.app->marked_count && !f.app->modal_count;
    if (paste) ok = ok && !strcmp(f.app->notice, "bracketed paste ignored");
    type(&f, "q");
    ok = ok && !kt_test_running(f.test);
    fixture_close(&f);
    return ok;
}

/* An arrow key that stalls after "ESC [" is not Escape and is not the keys
 * of its tail: A would mark every head. The key after the tail works. */
static bool stalled_sequence(void) {
    struct fixture f;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    f.app->view = 1;
    type(&f, "\033[");
    kt_test_advance(f.test, 149);
    type(&f, "B");
    ok = f.app->view == 1 && !f.app->marked_count;
    type(&f, "\033[");
    kt_test_advance(f.test, 150);
    ok = ok && f.app->view == 1;
    type(&f, "1;5A");
    ok = ok && !f.app->marked_count && f.app->view == 1;
    type(&f, "A");
    ok = ok && f.app->marked_count == 1;
    fixture_close(&f);
    return ok;
}

/* Escape acts after its short wait; Escape and a key in quick succession are
 * both handled, in that order. */
static bool escape_then_key(void) {
    struct fixture f;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    f.app->view = 1;
    type(&f, "\033");
    kt_test_advance(f.test, 19);
    ok = f.app->view == 1;
    kt_test_advance(f.test, 1);
    ok = ok && f.app->view == 0;
    f.app->view = 1;
    type(&f, "\033q");
    ok = ok && f.app->view == 0 && !kt_test_running(f.test);
    fixture_close(&f);
    return ok;
}

/* Over the result overlay the arrow and page keys scroll it; none of their
 * bytes closes it or runs a command. Any of them closes help, nothing more. */
static bool keys_over_overlays(void) {
    struct fixture f;
    char text[2048] = "";
    int i;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    for (i = 0; i < 100; i++) text_append(text, sizeof(text), "line %d\n", i);
    show_result(f.app, "OUTPUT", text);
    type(&f, "\033[B\033[B\033[A\033[6~");
    ok = f.app->result_open && f.app->result_scroll == 13 && !f.app->marked_count && f.app->view == 0;
    type(&f, "\033");
    kt_test_advance(f.test, 20);
    ok = ok && !f.app->result_open;
    type(&f, "?");
    ok = ok && f.app->help;
    /* Help scrolls with the arrows and closes with Esc; nothing else acts. */
    type(&f, "\033[B\033[B\033[A");
    ok = ok && f.app->help && f.app->help_scroll == 1;
    type(&f, "\033");
    kt_test_advance(f.test, 20);
    ok = ok && !f.app->help && !f.app->marked_count && f.app->view == 0;
    fixture_close(&f);
    return ok;
}

/* A prompt waits inside the key handler while the loop runs: what follows in
 * the input is its text, a paste is text and cannot submit (its line break
 * is a blank), Enter submits. With nothing left to read it gives up rather
 * than wait. */
static bool prompt_takes_input(void) {
    struct fixture f;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    type(&f, "/fea\033[200~tu\rre\033[201~-x\177\177\r");
    ok = !strcmp(f.app->search, "featu re") && !f.app->modal_count && kt_test_running(f.test);
    type(&f, "/zzz");
    ok = ok && !strcmp(f.app->search, "featu re") && !f.app->modal_count && kt_test_running(f.test);
    ok = ok && !kt_test_contains(f.test, "Search heads");
    fixture_close(&f);
    return ok;
}

/* Keys that arrive together cost one frame, and the frame after a resize
 * erases nothing. */
static bool frames_are_few(void) {
    struct fixture f;
    size_t before;
    int i;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    before = kt_test_frames(f.test);
    /* 119 presses of the help key leave help open. */
    for (i = 0; i < 119; i++) kt_test_feed(f.test, "?", 1U);
    kt_test_step(f.test);
    ok = f.app->help && kt_test_frames(f.test) > before && kt_test_frames(f.test) <= before + 2U;
    kt_test_resize(f.test, 100, 30);
    ok = ok && f.app->cols == 100 && f.app->rows == 30 && !strstr(kt_test_output(f.test, NULL), "\033[2J");
    ok = ok && kt_test_contains(f.test, "KEYBOARD HELP");
    fixture_close(&f);
    return ok;
}

/* A question's default button is Cancel: x then Enter removes nothing, and
 * the dialog is gone. Esc closes a form without submitting it and leaves
 * the view as it was. */
static bool dialogs_default_to_cancel(void) {
    struct fixture f;
    bool ok = fixture_open(&f);
    if (!ok) { fixture_close(&f); return false; }
    one_head(f.app);
    type(&f, "x\r");
    ok = !strcmp(f.app->notice, "Removal cancelled; nothing changed") && !f.app->modal_count && !f.app->dialogs.count;
    type(&f, "/abc\033");
    kt_test_advance(f.test, 20);
    ok = ok && !f.app->search[0] && f.app->view == 0 && !f.app->dialogs.count && kt_test_running(f.test);
    fixture_close(&f);
    return ok;
}

int main(void) {
    static const struct { bool (*run)(void); const char *name; } cases[] = {
        {chords_without_agent, "chords are held, named when unbound and case-sensitive with no agent attached"},
        {hints_follow_keys, "hints name the chords in effect and drop unbound actions"},
        {stalled_sequence, "a stalled escape sequence is neither Escape nor the keys of its tail"},
        {escape_then_key, "Escape acts after 20 ms; Escape then a key handles both"},
        {keys_over_overlays, "arrow and page keys scroll the result overlay and the help, which Esc closes"},
        {prompt_takes_input, "a prompt reads its text through the loop; paste cannot submit"},
        {dialogs_default_to_cancel, "a question defaults to Cancel and Esc closes a form without submitting"},
        {frames_are_few, "a burst of keys is one frame; a resize erases nothing"}
    };
    size_t i;
    bool passed = true, paste_ok, csi_ok;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bool ok = cases[i].run();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", cases[i].name);
        passed = passed && ok;
    }
    paste_ok = oversized_input(true); csi_ok = oversized_input(false);
    printf("[%s] oversized paste stays inert until its terminator, across stalls\n", paste_ok ? "PASS" : "FAIL");
    printf("[%s] oversized CSI stays inert until its terminator\n", csi_ok ? "PASS" : "FAIL");
    return passed && paste_ok && csi_ok ? 0 : 1;
}
