#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "kettle/program.h"
#include "kettle/testing.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* The runtime under the headless driver (deterministic: virtual clock, fed
 * input, intercepted processes), then under the real POSIX backend on a
 * private pseudo-terminal with real child processes. */

static void check_at(bool passed, const char *what, int line) {
    if (passed) return;
    fprintf(stderr, "FAIL tests/c/test_kettle_program.c:%d: %s\n", line, what);
    exit(1);
}
#define CHECK(condition) check_at((condition), #condition, __LINE__)

static bool has(const char *text, const char *part) { return strstr(text, part) != NULL; }

static size_t occurrences(const char *text, const char *part) {
    size_t count = 0;
    for (text = strstr(text, part); text; text = strstr(text + 1, part)) count++;
    return count;
}

/* ---- a recording application ------------------------------------------- */

enum { TAG_ONCE = 1, TAG_REPEAT, TAG_FIRST, TAG_SECOND, TAG_SPAWN, TAG_LEFT, TAG_EXEC, TAG_WATCH };

struct recorder {
    char log[2048];             /* one word per message, in delivery order */
    int inits, updates, views;
    bool handle_signals, cursor, all_motion, titled;
    const char *title;
    uint64_t repeat, second, spawn, watch;
    int watched, written;
    struct kt_spawn spawn_options;
    struct kt_program *program;  /* set by tests whose keys pump or lend */
    bool paste_opens_helper;
};

static void note(struct recorder *r, const char *format, ...) {
    size_t used = strlen(r->log);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(r->log + used, sizeof(r->log) - used, format, arguments);
    va_end(arguments);
}

static char *echo_argv[] = {"echo", "hi", NULL};
static char *editor_argv[] = {"editor", "notes.txt", NULL};

static void key_timers(struct recorder *r, char key, struct kt_cmds *cmds) {
    if (key == 't') (void)kt_cmd_timer(cmds, TAG_ONCE, 100, 0);
    if (key == 'r') r->repeat = kt_cmd_timer(cmds, TAG_REPEAT, 50, 50);
    if (key == 'c') kt_cmd_cancel(cmds, r->repeat);
    if (key == '2') {
        (void)kt_cmd_timer(cmds, TAG_FIRST, 30, 0);
        r->second = kt_cmd_timer(cmds, TAG_SECOND, 30, 0);
    }
}

static void key_processes(struct recorder *r, char key, struct kt_cmds *cmds) {
    struct kt_spawn spawn = r->spawn_options;
    spawn.argv = echo_argv;
    if (key == 's') r->spawn = kt_cmd_spawn(cmds, TAG_SPAWN, &spawn);
    if (key == 'k') kt_cmd_cancel(cmds, r->spawn);
    if (key == 'l') { spawn.on_exit = KT_SPAWN_LEAVE; (void)kt_cmd_spawn(cmds, TAG_LEFT, &spawn); }
    if (key == 'e') kt_cmd_exec(cmds, TAG_EXEC, editor_argv);
    if (key == 'z') kt_cmd_suspend(cmds);
}

static void key_other(struct recorder *r, char key, struct kt_cmds *cmds) {
    struct kt_msg first = {KT_MSG_USER, 0, {{0}}}, second = {KT_MSG_USER, 0, {{0}}};
    first.u.user.a = 1; second.u.user.a = 2;
    if (key == 'q') kt_cmd_quit(cmds, 7);
    if (key == 'p') { kt_cmd_post(cmds, &first); kt_cmd_post(cmds, &second); }
    if (key == 'R') kt_cmd_repaint(cmds);
    if (key == 'w') r->watch = kt_cmd_watch(cmds, TAG_WATCH, r->watched, POLLIN);
    if (key == 'W') kt_cmd_cancel(cmds, r->watch);
    if (key == 'C') r->cursor = !r->cursor;
    if (key == 'M') r->all_motion = !r->all_motion;
    if (key == 'T') r->titled = !r->titled;
}

/* A helper that does not return before it has its answer: it runs the loop
 * itself, keeps the keys ('.' ends it, 'q' goes to update) and lets every
 * other message take the ordinary path. */
static void helper(struct recorder *r, struct kt_cmds *cmds) {
    struct kt_msg msg;
    note(r, "h[ ");
    kt_cmd_timer(cmds, TAG_ONCE, 0, 0);
    while (kt_program_next(r->program, &msg, -1)) {
        char key = msg.type == KT_MSG_KEY ? msg.u.event.bytes[0] : '\0';
        if (key == '.') break;
        if (msg.type == KT_MSG_PASTE) note(r, "p:%.*s ", (int)msg.u.paste.length, msg.u.paste.bytes);
        else if (key && key != 'q') note(r, "k:%c ", key);
        else (void)kt_program_dispatch(r->program, &msg);
        (void)kt_program_render(r->program);
    }
    note(r, "]h ");
}

/* Lends the terminal and takes it back within one update. */
static void lend(struct recorder *r) {
    note(r, "L:%d", (int)kt_program_release(r->program));
    note(r, "%d", (int)kt_program_release(r->program));
    note(r, "%d", (int)kt_program_render(r->program));
    note(r, "%d ", (int)kt_program_reclaim(r->program));
}

static void on_key(struct recorder *r, const struct kt_event *event, struct kt_cmds *cmds) {
    char key = event->length == 1 ? event->bytes[0] : '\0';
    if (event->key == KT_KEY_UP) note(r, "K:up ");
    else if (event->length == 1 && event->bytes[0] == '\033') note(r, "K:esc ");
    else note(r, "K:%.*s ", (int)event->length, event->bytes);
    key_timers(r, key, cmds);
    key_processes(r, key, cmds);
    key_other(r, key, cmds);
    if (key == 'h' && r->program) helper(r, cmds);
    if (key == 'l' && r->program) lend(r);
}

/* The outer message's bytes must outlive the helper's own messages. */
static void on_paste(struct recorder *r, const struct kt_msg *msg, struct kt_cmds *cmds) {
    if (r->paste_opens_helper) { r->paste_opens_helper = false; helper(r, cmds); }
    note(r, "P:%d%s:%.*s ", (int)msg->u.paste.length, msg->u.paste.truncated ? "u" : "",
         (int)msg->u.paste.length, msg->u.paste.bytes);
}

static void on_process(struct recorder *r, const struct kt_process_result *result) {
    note(r, "X:%d:%d:%s%s%s%s%s:%.*s ", (int)(result->id == r->spawn), result->status,
         result->timed_out ? "t" : "", result->cancelled ? "c" : "", result->truncated ? "u" : "",
         result->failed ? "f" : "", result->output[result->length] ? "!" : "",
         (int)result->length, result->output);
}

static void on_fd(struct recorder *r, const struct kt_msg *msg, struct kt_cmds *cmds) {
    char byte = '?';
    if (msg->u.fd.revents & POLLNVAL) { note(r, "F:invalid "); return; }
    if (read(msg->u.fd.fd, &byte, 1) != 1) kt_cmd_cancel(cmds, msg->u.fd.id);
    note(r, "F:%c ", byte);
}

static bool on_effect(struct recorder *r, const struct kt_msg *msg, struct kt_cmds *cmds) {
    switch (msg->type) {
    case KT_MSG_TIMER:
        note(r, "T:%d ", (int)msg->tag);
        if (msg->tag == TAG_FIRST) kt_cmd_cancel(cmds, r->second);
        return true;
    case KT_MSG_FD: on_fd(r, msg, cmds); return true;
    case KT_MSG_PROCESS: on_process(r, &msg->u.process); return true;
    case KT_MSG_EXEC: note(r, "E:%d ", msg->u.exec.status); return true;
    case KT_MSG_RESUME: note(r, "Z "); return true;
    case KT_MSG_USER: note(r, "U:%d ", (int)msg->u.user.a); return true;
    default: return false;
    }
}

static void recorder_init(void *model, struct kt_cmds *cmds) {
    struct recorder *r = model;
    (void)cmds;
    r->inits++;
    note(r, "init ");
}

static bool recorder_update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct recorder *r = model;
    r->updates++;
    switch (msg->type) {
    case KT_MSG_KEY: on_key(r, &msg->u.event, cmds); return true;
    case KT_MSG_MOUSE: note(r, "M:%d,%d ", msg->u.event.x, msg->u.event.y); return true;
    case KT_MSG_PASTE: on_paste(r, msg, cmds); return true;
    case KT_MSG_RESIZE: note(r, "R:%dx%d ", msg->u.resize.columns, msg->u.resize.rows); return true;
    case KT_MSG_FOCUS: note(r, "focus "); return true;
    case KT_MSG_BLUR: note(r, "blur "); return true;
    case KT_MSG_SIGNAL: note(r, "S:%d ", msg->u.signal.number); return r->handle_signals;
    default: return on_effect(r, msg, cmds);
    }
}

static void recorder_view(const void *model, struct kt_frame *frame) {
    const struct recorder *r = model;
    struct kt_canvas *canvas = kt_frame_canvas(frame);
    char line[64];
    ((struct recorder *)model)->views++;   /* counting only; the frame does not depend on it */
    snprintf(line, sizeof(line), "updates %d", r->updates);
    kt_text(canvas, (struct kt_rect){0, 0, canvas->width, 1}, line, KT_BASE);
    kt_text(canvas, (struct kt_rect){0, 1, canvas->width, 1}, "caf\xc3\xa9 \xe7\x95\x8c", KT_TITLE);
    if (r->cursor) kt_frame_cursor(frame, 3, 1);
    if (r->all_motion) kt_frame_mouse(frame, KT_MOUSE_ALL);
    if (r->titled) {
        char title[64];
        snprintf(title, sizeof(title), "%s", r->title ? r->title : "recorder");
        kt_frame_title(frame, title);
        memset(title, '!', sizeof(title) - 1);   /* the frame keeps its own copy */
    }
}

static void recorder_style(void *model, FILE *out, enum kt_style style) {
    (void)model;
    fputs(style == KT_TITLE ? "\033[1m" : "\033[0m", out);
}

static const struct kt_app recorder_app = {recorder_init, recorder_update, recorder_view, recorder_style};

static struct kt_test *start(struct recorder *r, const struct kt_options *options) {
    struct kt_test *t;
    memset(r, 0, sizeof(*r));
    r->watched = -1;
    t = kt_test_new(&recorder_app, r, 20, 4, options);
    CHECK(t != NULL);
    return t;
}

/* ---- headless: loop and rendering --------------------------------------- */

static void test_first_frame(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    size_t length;
    const char *bytes = kt_test_output(t, &length);
    CHECK(!strcmp(r.log, "init R:20x4 "));
    CHECK(r.inits == 1);
    CHECK(r.views == 1);
    CHECK(kt_test_frames(t) == 1);
    CHECK(!strncmp(bytes, "\033[?2026h\033[H\033[2J", 15));
    CHECK(length > 8);
    CHECK(!strcmp(bytes + length - 8, "\033[?2026l"));
    CHECK(has(bytes, "\033[?25l"));
    CHECK(has(bytes, "\033[2;1H\033[1mcaf\xc3\xa9 \xe7\x95\x8c"));
    CHECK(!has(bytes, "\033[?1049h"));
    CHECK(!strcmp(kt_test_row(t, 0), "updates 1"));
    CHECK(!strcmp(kt_test_row(t, 1), "caf\xc3\xa9 \xe7\x95\x8c"));
    CHECK(!strcmp(kt_test_screen(t), "updates 1\ncaf\xc3\xa9 \xe7\x95\x8c\n\n"));
    CHECK(!kt_test_cursor(t, NULL, NULL));
    kt_test_free(t);
}

static void test_burst_is_one_frame(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    size_t length;
    const char *bytes;
    kt_test_type(t, "abc");
    bytes = kt_test_output(t, &length);
    CHECK(!strcmp(r.log, "init R:20x4 K:a K:b K:c "));
    CHECK(r.updates == 4);
    CHECK(r.views == 2);
    CHECK(kt_test_frames(t) == 2);
    /* Only the changed digit travels, inside one synchronized update. */
    CHECK(!strcmp(bytes, "\033[?2026h\033[1;9H\033[0m4\033[?2026l"));
    CHECK(length == strlen(bytes));
    CHECK(kt_test_contains(t, "updates 4"));
    kt_test_free(t);
}

static void test_idle_is_silent(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    struct kt_msg nothing = {KT_MSG_USER + 9, 0, {{0}}};
    kt_test_step(t);
    kt_test_step(t);
    kt_test_advance(t, 60000);
    CHECK(r.updates == 1);
    CHECK(r.views == 1);
    CHECK(kt_test_frames(t) == 1);
    CHECK(kt_test_sleep(t) == -1);
    /* A message that changes nothing visible draws a frame that writes nothing. */
    r.updates--;
    kt_test_send(t, &nothing);
    CHECK(r.views == 2);
    CHECK(kt_test_frames(t) == 1);
    kt_test_free(t);
}

static void test_frame_cap(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.fps = 10;
    t = start(&r, &options);
    CHECK(r.views == 1);
    kt_test_type(t, "a");
    kt_test_type(t, "b");
    CHECK(r.updates == 3);
    CHECK(r.views == 1);
    CHECK(kt_test_sleep(t) == 100);
    kt_test_advance(t, 99);
    CHECK(r.views == 1);
    kt_test_advance(t, 1);
    CHECK(r.views == 2);
    CHECK(kt_test_frames(t) == 2);
    CHECK(kt_test_contains(t, "updates 3"));
    CHECK(kt_test_sleep(t) == -1);
    kt_test_free(t);
}

static void test_unsynchronized_output(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.no_synchronized_output = true;
    options.alt_screen = true;
    t = start(&r, &options);
    CHECK(!strncmp(kt_test_output(t, NULL), "\033[?1049h\033[H\033[2J", 15));
    CHECK(!has(kt_test_output(t, NULL), "2026"));
    kt_test_free(t);
}

static void test_repaint_overwrites(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    const char *bytes;
    kt_test_type(t, "R");
    bytes = kt_test_output(t, NULL);
    CHECK(!has(bytes, "\033[2J"));
    CHECK(has(bytes, "\033[1;1H"));
    CHECK(has(bytes, "\033[4;1H"));
    CHECK(has(bytes, "updates 2"));
    kt_test_resize(t, 30, 5);
    bytes = kt_test_output(t, NULL);
    CHECK(has(r.log, "R:30x5 "));
    CHECK(!has(bytes, "\033[2J"));
    CHECK(has(bytes, "\033[5;1H"));
    CHECK(!strcmp(kt_test_screen(t), "updates 3\ncaf\xc3\xa9 \xe7\x95\x8c\n\n\n"));
    kt_test_free(t);
}

static void test_clipped_terminal(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.max_columns = 12; options.max_rows = 2;
    t = start(&r, &options);
    CHECK(!strcmp(r.log, "init R:12x2 "));
    /* The margin the canvas cannot reach is erased with the repaint. */
    CHECK(has(kt_test_output(t, NULL), "\033[1;13H\033[0m\033[K"));
    CHECK(has(kt_test_output(t, NULL), "\033[3;1H\033[0m\033[J"));
    CHECK(!strcmp(kt_test_row(t, 2), ""));
    kt_test_free(t);
}

/* ---- headless: input ---------------------------------------------------- */

static void test_escape(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    kt_test_type(t, "\033");
    CHECK(r.updates == 1);
    CHECK(kt_test_sleep(t) == 50);
    kt_test_advance(t, 49);
    CHECK(r.updates == 1);
    kt_test_advance(t, 1);
    CHECK(has(r.log, "K:esc "));
    CHECK(kt_test_sleep(t) == -1);
    /* A sequence split across reads inside the wait is still one key. */
    kt_test_type(t, "\033");
    kt_test_advance(t, 20);
    kt_test_type(t, "[A");
    kt_test_advance(t, 200);
    CHECK(!strcmp(r.log, "init R:20x4 K:esc K:up "));
    kt_test_free(t);
}

static void test_escape_wait_option(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.escape_ms = 5;
    t = start(&r, &options);
    kt_test_type(t, "\033");
    CHECK(kt_test_sleep(t) == 5);
    kt_test_advance(t, 5);
    CHECK(has(r.log, "K:esc "));
    kt_test_free(t);
}

/* A key split by a slow link arrives whole when sequences may wait longer
 * than a lone Escape; one that never finishes is dropped with its tail. */
static void test_sequence_wait(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.escape_ms = 5; options.sequence_ms = 100;
    t = start(&r, &options);
    kt_test_type(t, "\033");
    CHECK(kt_test_sleep(t) == 5);
    kt_test_type(t, "[");
    CHECK(kt_test_sleep(t) == 100);
    kt_test_advance(t, 99);
    kt_test_type(t, "A");
    CHECK(!strcmp(r.log, "init R:20x4 K:up "));
    kt_test_type(t, "\033[1;");
    kt_test_advance(t, 100);
    CHECK(kt_test_sleep(t) == -1);
    kt_test_type(t, "5Cx");
    CHECK(!strcmp(r.log, "init R:20x4 K:up K:x "));
    kt_test_free(t);
    /* Unset, sequences wait as long as Escape does. */
    t = start(&r, NULL);
    kt_test_type(t, "\033[");
    CHECK(kt_test_sleep(t) == 50);
    kt_test_free(t);
}

/* The closing bracket of a paste may stall for longer than any key wait. */
static void test_paste_outlasts_waits(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.bracketed_paste = true;
    t = start(&r, &options);
    kt_test_type(t, "\033[200~ab\033[20");
    CHECK(kt_test_sleep(t) == -1);
    kt_test_advance(t, 60000);
    kt_test_type(t, "1~x");
    CHECK(!strcmp(r.log, "init R:20x4 P:2:ab K:x "));
    kt_test_free(t);
}

static void test_paste(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.bracketed_paste = true;
    t = start(&r, &options);
    CHECK(has(kt_test_output(t, NULL), "\033[?2004h"));
    kt_test_type(t, "\033[200~q\nline \xe7\x95\x8c\033[201~x");
    CHECK(!strcmp(r.log, "init R:20x4 P:10:q\nline \xe7\x95\x8c K:x "));
    CHECK(r.updates == 3);
    /* A paste that arrives in pieces is still one message. */
    kt_test_type(t, "\033[200~ab");
    CHECK(r.updates == 3);
    kt_test_type(t, "cd\033[201~");
    CHECK(has(r.log, "P:4:abcd "));
    CHECK(r.updates == 4);
    kt_test_free(t);
}

static void test_paste_limit(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.paste_limit = 4;
    t = start(&r, &options);
    kt_test_type(t, "\033[200~abcdefgh\033[201~");
    CHECK(has(r.log, "P:4u:abcd "));
    /* The cut never lands inside a character, and nothing after it is kept. */
    kt_test_type(t, "\033[200~abc\xc3\xa9z\033[201~");
    CHECK(has(r.log, "P:3u:abc "));
    kt_test_type(t, "\033[200~\033[201~");
    CHECK(has(r.log, "P:0: "));
    kt_test_free(t);
}

static void test_mouse_and_focus(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.mouse = KT_MOUSE_DRAG; options.focus_events = true;
    t = start(&r, &options);
    CHECK(has(kt_test_output(t, NULL), "\033[?1002h\033[?1006h"));
    CHECK(has(kt_test_output(t, NULL), "\033[?1004h"));
    kt_test_type(t, "\033[<0;5;3M\033[I\033[O");
    CHECK(!strcmp(r.log, "init R:20x4 M:4,2 focus blur "));
    kt_test_type(t, "M");
    CHECK(!strcmp(kt_test_output(t, NULL), "\033[?2026h\033[?1002l\033[?1003h\033[1;9H\033[0m5\033[?2026l"));
    kt_test_type(t, "M");
    CHECK(has(kt_test_output(t, NULL), "\033[?1003l\033[?1002h"));
    kt_test_free(t);
}

static void test_cursor_and_title(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    int x = -1, y = -1;
    kt_test_type(t, "C");
    CHECK(kt_test_cursor(t, &x, &y));
    CHECK(x == 3);
    CHECK(y == 1);
    CHECK(has(kt_test_output(t, NULL), "\033[2;4H\033[?25h\033[?2026l"));
    /* Cells written move the terminal cursor, so it is parked again. */
    kt_test_type(t, "x");
    CHECK(has(kt_test_output(t, NULL), "3\033[2;4H\033[?2026l"));
    kt_test_type(t, "C");
    CHECK(!kt_test_cursor(t, NULL, NULL));
    CHECK(has(kt_test_output(t, NULL), "\033[?25l"));
    r.title = "a\033]b\a title";
    kt_test_type(t, "T");
    CHECK(has(kt_test_output(t, NULL), "\033[22;2t\033]2;a ]b  title\033\\"));
    kt_test_type(t, "T");
    CHECK(has(kt_test_output(t, NULL), "\033[23;2t"));
    kt_test_free(t);
}

static void test_restore_sequence(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.alt_screen = options.bracketed_paste = options.focus_events = true;
    options.mouse = KT_MOUSE_DRAG;
    t = start(&r, &options);
    kt_test_type(t, "T");
    CHECK(!strcmp(kt_test_restore(t), "\033[0m\033[?1002l\033[?1006l\033[?2004l\033[?1004l"
                                      "\033[23;2t\033[?25h\033[?1049l"));
    kt_test_type(t, "TC");
    CHECK(!strcmp(kt_test_restore(t), "\033[0m\033[?1002l\033[?1006l\033[?2004l\033[?1004l\033[?1049l"));
    kt_test_type(t, "q");
    CHECK(!strcmp(kt_test_restore(t), "\033[0m\033[?1002l\033[?1006l\033[?2004l\033[?1004l\033[?1049l"));
    kt_test_free(t);
    /* A stopped job that is killed gets SIGCONT and SIGTERM at once: nothing
     * is drawn in between, and the modes set before the stop are still undone. */
    t = start(&r, &options);
    kt_test_feed(t, "q", 1);
    kt_test_signal(t, SIGCONT);
    CHECK(!kt_test_running(t));
    CHECK(!strcmp(kt_test_restore(t), "\033[0m\033[?1002l\033[?1006l\033[?2004l\033[?1004l\033[?25h\033[?1049l"));
    kt_test_free(t);
    /* Without the alternate screen the shell continues below the last frame. */
    t = start(&r, NULL);
    CHECK(!strcmp(kt_test_restore(t), "\033[0m\033[?25h\033[4;1H\r\n"));
    kt_test_free(t);
}

/* ---- headless: commands ------------------------------------------------- */

static void test_timers(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    kt_test_type(t, "t");
    CHECK(kt_test_sleep(t) == 100);
    kt_test_advance(t, 99);
    CHECK(!has(r.log, "T:1"));
    kt_test_advance(t, 1);
    CHECK(occurrences(r.log, "T:1 ") == 1);
    kt_test_advance(t, 1000);
    CHECK(occurrences(r.log, "T:1 ") == 1);
    CHECK(kt_test_sleep(t) == -1);
    kt_test_type(t, "r");
    kt_test_advance(t, 175);
    CHECK(occurrences(r.log, "T:2 ") == 3);
    CHECK(kt_test_sleep(t) == 25);
    kt_test_type(t, "c");
    kt_test_advance(t, 1000);
    CHECK(occurrences(r.log, "T:2 ") == 3);
    CHECK(kt_test_sleep(t) == -1);
    kt_test_free(t);
}

static void test_cancel_drops_queued_message(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    /* Both timers are due in the same wake-up; the first one's update cancels
     * the second, whose message is already waiting. */
    kt_test_type(t, "2");
    kt_test_advance(t, 30);
    CHECK(has(r.log, "T:3 "));
    CHECK(!has(r.log, "T:4 "));
    kt_test_advance(t, 1000);
    CHECK(!has(r.log, "T:4 "));
    kt_test_free(t);
}

static void test_posts_and_order(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    struct kt_msg posted = {KT_MSG_USER, 0, {{0}}};
    posted.u.user.a = 5;
    /* One wake-up delivers signals, then posted messages, then input; a post
     * from update goes ahead of the input still waiting. */
    r.handle_signals = true;
    kt_test_feed(t, "pab", 3);
    CHECK(kt_program_post(kt_test_program(t), &posted));
    kt_test_signal(t, SIGINT);
    CHECK(!strcmp(r.log, "init R:20x4 S:2 U:5 K:p U:1 U:2 K:a K:b "));
    CHECK(r.views == 2);
    kt_test_free(t);
}

static void test_signals(void) {
    static const int fatal[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    struct recorder r;
    struct kt_test *t;
    size_t i;
    for (i = 0; i < sizeof(fatal) / sizeof(*fatal); i++) {
        t = start(&r, NULL);
        kt_test_signal(t, SIGCHLD);
        CHECK(kt_test_running(t));
        kt_test_signal(t, fatal[i]);
        CHECK(!kt_test_running(t));
        CHECK(kt_test_status(t) == 128 + fatal[i]);
        kt_test_free(t);
    }
    t = start(&r, NULL);
    r.handle_signals = true;
    kt_test_signal(t, SIGTERM);
    CHECK(kt_test_running(t));
    CHECK(has(r.log, "S:15 "));
    kt_test_free(t);
}

static void test_quit(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    kt_test_type(t, "qab");
    CHECK(!kt_test_running(t));
    CHECK(kt_test_status(t) == 7);
    CHECK(!has(r.log, "K:a"));
    CHECK(kt_test_frames(t) == 1);
    CHECK(!strcmp(kt_test_screen(t), ""));
    kt_test_type(t, "x");
    CHECK(!has(r.log, "K:x"));
    kt_test_free(t);
}

static void test_watch(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    int fds[2];
    CHECK(pipe(fds) == 0);
    r.watched = fds[0];
    kt_test_type(t, "w");
    CHECK(!has(r.log, "F:"));
    CHECK(write(fds[1], "xy", 2) == 2);
    /* Level-triggered: reported until update has read everything. */
    kt_test_step(t);
    CHECK(has(r.log, "F:x F:y "));
    CHECK(occurrences(r.log, "F:") == 2);
    kt_test_type(t, "W");
    CHECK(write(fds[1], "z", 1) == 1);
    kt_test_step(t);
    CHECK(occurrences(r.log, "F:") == 2);
    /* A descriptor that is not open is reported once, not forever. */
    close(fds[0]); close(fds[1]);
    kt_test_type(t, "w");
    kt_test_step(t);
    CHECK(occurrences(r.log, "F:invalid ") == 1);
    kt_test_free(t);
}

/* ---- headless: processes ------------------------------------------------ */

static void test_spawn_interception(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    const struct kt_test_process *process;
    r.spawn_options.directory = "/work"; r.spawn_options.budget_ms = 500;
    kt_test_type(t, "s");
    CHECK(kt_test_process_count(t) == 1);
    process = kt_test_process(t, 0);
    CHECK(process->running);
    CHECK(process->id == r.spawn);
    CHECK(process->tag == TAG_SPAWN);
    CHECK(!strcmp(process->spawn.argv[0], "echo"));
    CHECK(!strcmp(process->spawn.argv[1], "hi"));
    CHECK(process->spawn.argv[2] == NULL);
    CHECK(!strcmp(process->spawn.directory, "/work"));
    CHECK(!has(r.log, "X:"));
    CHECK(kt_test_exit(t, process->id, 3, "hi\n"));
    CHECK(has(r.log, "X:1:3::hi\n "));
    CHECK(!kt_test_exit(t, r.spawn, 0, NULL));
    kt_test_advance(t, 5000);
    CHECK(occurrences(r.log, "X:") == 1);
    CHECK(kt_test_process(t, 1) == NULL);
    kt_test_free(t);
}

static void test_spawn_budget_and_cancel(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    r.spawn_options.budget_ms = 500;
    kt_test_type(t, "s");
    CHECK(kt_test_sleep(t) == 500);
    kt_test_advance(t, 499);
    CHECK(kt_test_process(t, 0)->running);
    kt_test_advance(t, 1);
    CHECK(kt_test_process(t, 0)->signal == SIGTERM);
    CHECK(has(r.log, "X:1:143:t: "));
    r.spawn_options.budget_ms = 0;
    kt_test_type(t, "s");
    CHECK(kt_test_sleep(t) == -1);
    kt_test_type(t, "k");
    CHECK(has(r.log, "X:1:143:c: "));
    CHECK(occurrences(r.log, "X:") == 2);
    kt_test_free(t);
}

static void test_spawn_truncation(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    r.spawn_options.max_output = 4;
    kt_test_type(t, "s");
    CHECK(kt_test_exit(t, r.spawn, 0, "abcdefgh"));
    CHECK(has(r.log, "X:1:0:u:abcd "));
    kt_test_free(t);
}

static void test_exit_reports_children(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    kt_test_type(t, "sl");
    CHECK(kt_test_process_count(t) == 2);
    kt_test_type(t, "q");
    /* The child the program owns is stopped and reported; the one it was told
     * to leave is neither signalled nor reported. */
    CHECK(!kt_test_running(t));
    CHECK(has(r.log, "X:1:143:c: "));
    CHECK(occurrences(r.log, "X:") == 1);
    CHECK(kt_test_process(t, 0)->signal == SIGTERM);
    CHECK(kt_test_process(t, 1)->signal == 0);
    CHECK(kt_test_process(t, 1)->running);
    kt_test_free(t);
}

static void test_exec_and_suspend(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    kt_test_exec_status(t, 5);
    kt_test_type(t, "e");
    CHECK(kt_test_exec_count(t) == 1);
    CHECK(!strcmp(kt_test_exec(t, 0)[0], "editor"));
    CHECK(!strcmp(kt_test_exec(t, 0)[1], "notes.txt"));
    CHECK(has(r.log, "E:5 "));
    /* The terminal was handed over, so the next frame takes it back whole. */
    CHECK(!strncmp(kt_test_output(t, NULL), "\033[?2026h\033[H\033[2J", 15));
    CHECK(has(kt_test_output(t, NULL), "updates 3"));
    kt_test_type(t, "z");
    CHECK(kt_test_suspends(t) == 1);
    CHECK(has(r.log, "Z "));
    kt_test_signal(t, SIGTSTP);
    CHECK(kt_test_suspends(t) == 2);
    kt_test_signal(t, SIGCONT);
    CHECK(occurrences(r.log, "Z ") == 3);
    CHECK(has(kt_test_output(t, NULL), "\033[H\033[2J"));
    kt_test_free(t);
}

/* ---- headless: the pump and memory -------------------------------------- */

static void test_pump(void) {
    struct recorder r;
    struct kt_test *t = start(&r, NULL);
    struct kt_program *program = kt_test_program(t);
    struct kt_msg msg;
    CHECK(!kt_program_next(program, &msg, 0));
    kt_test_feed(t, "ab", 2);
    CHECK(kt_program_next(program, &msg, 0));
    CHECK(msg.type == KT_MSG_KEY);
    CHECK(msg.u.event.bytes[0] == 'a');
    CHECK(kt_program_dispatch(program, &msg));
    CHECK(r.views == 1);
    CHECK(kt_program_render(program));
    CHECK(r.views == 2);
    CHECK(kt_test_contains(t, "updates 2"));
    CHECK(kt_program_next(program, &msg, 0));
    CHECK(msg.u.event.bytes[0] == 'b');
    CHECK(!kt_program_next(program, &msg, 0));
    CHECK(kt_program_status(program) == 0);
    kt_test_free(t);
}

/* update runs the loop itself: its queued commands take effect, its own
 * message survives, other messages take the ordinary path, frames are drawn. */
static void test_pump_inside_update(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    int views;
    options.bracketed_paste = true;
    t = start(&r, &options);
    r.program = kt_test_program(t);
    views = r.views;
    kt_test_type(t, "hab.c");
    /* Input in hand comes first, as in the loop proper; the timer queued
     * before the wait is due once the input is used up. */
    CHECK(!strcmp(r.log, "init R:20x4 K:h h[ k:a k:b ]h K:c T:1 "));
    CHECK(r.views >= views + 3);
    CHECK(kt_test_contains(t, "updates 4"));
    /* The paste that opened the helper is intact after a paste inside it. */
    r.log[0] = '\0'; r.paste_opens_helper = true;
    kt_test_type(t, "\033[200~outer\033[201~\033[200~inner\033[201~.");
    CHECK(!strcmp(r.log, "h[ p:inner ]h P:5:outer T:1 "));
    /* Nothing more to read: the wait gives up instead of hanging the test. */
    r.log[0] = '\0';
    kt_test_type(t, "hx");
    CHECK(!strcmp(r.log, "K:h h[ k:x T:1 ]h "));
    CHECK(kt_test_running(t));
    /* A quit inside the helper ends its wait; the rest is never read. */
    r.log[0] = '\0';
    kt_test_type(t, "hq.z");
    CHECK(!strcmp(r.log, "K:h h[ K:q ]h "));
    CHECK(!kt_test_running(t) && kt_test_status(t) == 7);
    kt_test_free(t);
}

static void test_release_and_reclaim(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.alt_screen = true;
    t = start(&r, &options);
    CHECK(!kt_program_reclaim(kt_test_program(t)));
    r.program = kt_test_program(t);
    kt_test_type(t, "l\033");
    /* Released once, not twice; no frame while lent; taken back. */
    CHECK(has(r.log, "L:1001 "));
    CHECK(!strncmp(kt_test_output(t, NULL), "\033[?2026h\033[?1049h\033[H\033[2J", 22));
    CHECK(has(kt_test_output(t, NULL), "updates 2"));
    /* Input read before the hand-off is not replayed after it. */
    kt_test_advance(t, 1000);
    CHECK(!has(r.log, "K:esc"));
    kt_test_type(t, "x");
    CHECK(has(r.log, "K:x "));
    kt_test_free(t);
}

static long live_blocks;
static void *counting_resize(void *context, void *pointer, size_t size) {
    (void)context;
    if (!pointer && size) live_blocks++;
    if (pointer && !size) live_blocks--;
    if (!size) { free(pointer); return NULL; }
    return realloc(pointer, size);
}

static void test_allocator(void) {
    struct recorder r;
    struct kt_options options = {0};
    struct kt_test *t;
    options.allocator.resize = counting_resize;
    options.bracketed_paste = true;
    t = start(&r, &options);
    CHECK(live_blocks > 0);
    r.spawn_options.budget_ms = 100;
    kt_test_type(t, "s");
    r.spawn_options.budget_ms = 0;
    kt_test_type(t, "trs\033[200~pasted\033[201~");
    kt_test_resize(t, 60, 20);
    kt_test_advance(t, 150);
    CHECK(kt_test_exit(t, r.spawn, 0, "output"));
    kt_test_type(t, "s\033[200~unfinished");
    kt_test_free(t);
    CHECK(live_blocks == 0);
    /* Messages left over from a helper's own loop are released too. */
    t = start(&r, &options);
    r.program = kt_test_program(t);
    r.paste_opens_helper = true;
    kt_test_type(t, "\033[200~outer\033[201~\033[200~inner\033[201~");
    kt_test_free(t);
    CHECK(live_blocks == 0);
}

static void headless(void) {
    test_first_frame();
    test_burst_is_one_frame();
    test_idle_is_silent();
    test_frame_cap();
    test_unsynchronized_output();
    test_repaint_overwrites();
    test_clipped_terminal();
    test_escape();
    test_escape_wait_option();
    test_sequence_wait();
    test_paste();
    test_paste_outlasts_waits();
    test_paste_limit();
    test_mouse_and_focus();
    test_cursor_and_title();
    test_restore_sequence();
    test_timers();
    test_cancel_drops_queued_message();
    test_posts_and_order();
    test_signals();
    test_quit();
    test_watch();
    test_spawn_interception();
    test_spawn_budget_and_cancel();
    test_spawn_truncation();
    test_exit_reports_children();
    test_exec_and_suspend();
    test_pump();
    test_pump_inside_update();
    test_release_and_reclaim();
    test_allocator();
    puts("program (headless): frames, input, timers, signals, commands, processes and the pump passed");
}

/* ---- the real backend ---------------------------------------------------- */

/* A private pseudo-terminal for the program, with a process that reads what
 * the program writes so the terminal never fills up. */
struct terminal { int slave; pid_t reader; };

static struct terminal terminal_open(void) {
    struct terminal terminal;
    struct winsize size;
    char sink[4096];
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    CHECK(grantpt(master) == 0);
    CHECK(unlockpt(master) == 0);
    terminal.slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    CHECK(terminal.slave >= 0);
    /* Children the program starts must not keep the terminal open. */
    CHECK(fcntl(terminal.slave, F_SETFD, FD_CLOEXEC) == 0);
    memset(&size, 0, sizeof(size));
    size.ws_col = 20; size.ws_row = 4;
    CHECK(ioctl(terminal.slave, TIOCSWINSZ, &size) == 0);
    terminal.reader = fork();
    CHECK(terminal.reader >= 0);
    if (terminal.reader == 0) {
        close(terminal.slave);
        while (read(master, sink, sizeof(sink)) > 0) { }
        _exit(0);
    }
    close(master);
    return terminal;
}

static void terminal_close(struct terminal *terminal) {
    close(terminal->slave);
    while (waitpid(terminal->reader, NULL, 0) < 0 && errno == EINTR) { }
}

static void pause_ms(long milliseconds) {
    struct timespec time;
    time.tv_sec = milliseconds / 1000; time.tv_nsec = milliseconds % 1000 * 1000000L;
    while (nanosleep(&time, &time) != 0 && errno == EINTR) { }
}

/* Process state letter from ps, or '\0' when there is no such process. */
static char process_state(long pid) {
    char command[64], line[64] = "";
    size_t at = 0;
    FILE *ps;
    snprintf(command, sizeof(command), "ps -o stat= -p %ld", pid);
    ps = popen(command, "r");
    CHECK(ps != NULL);
    if (!fgets(line, sizeof(line), ps)) line[0] = '\0';
    (void)pclose(ps);
    while (line[at] == ' ') at++;
    return line[at] == '\n' ? '\0' : line[at];
}

/* Gone, or dead and only waiting for its parent to collect it. */
static bool process_ended(long pid) {
    int attempt;
    for (attempt = 0; attempt < 300; attempt++) {
        char state = process_state(pid);
        if (!state || state == 'Z') return true;
        pause_ms(10);
    }
    return false;
}

enum { PROBE_SPAWN = 70, PROBE_CANCEL, PROBE_QUIT, PROBE_GUARD, PROBE_KICK, PROBE_TICK, PROBE_LEFT };

/* Starts what the scenario asks for and records what comes back. */
struct probe {
    struct kt_spawn spawn, left;
    long cancel_ms, quit_ms, kick_ms, tick_ms;
    int pipe_fds[2];
    bool watch, hangup;
    uint64_t id, started, first_tick, last_tick;
    int reports, ticks, posted_a, posted_b;
    char watched;
    struct kt_process_result result;
    char output[256];
};

static struct kt_program *volatile posting;
static pid_t hangup_reader;   /* killed on the first tick when set */

static void post_from_handler(int number) {
    struct kt_msg msg;
    (void)number;
    memset(&msg, 0, sizeof(msg));
    msg.type = KT_MSG_USER + 1; msg.tag = 9;
    msg.u.user.a = 41; msg.u.user.b = 42;
    (void)kt_program_post(posting, &msg);
}

static uint64_t monotonic_ms(void) {
    struct timespec time;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * 1000U + (uint64_t)time.tv_nsec / 1000000U;
}

static void probe_init(void *model, struct kt_cmds *cmds) {
    struct probe *probe = model;
    probe->started = monotonic_ms();
    if (probe->spawn.argv) probe->id = kt_cmd_spawn(cmds, PROBE_SPAWN, &probe->spawn);
    if (probe->left.argv) (void)kt_cmd_spawn(cmds, PROBE_LEFT, &probe->left);
    if (probe->cancel_ms) (void)kt_cmd_timer(cmds, PROBE_CANCEL, (uint64_t)probe->cancel_ms, 0);
    if (probe->quit_ms) (void)kt_cmd_timer(cmds, PROBE_QUIT, (uint64_t)probe->quit_ms, 0);
    if (probe->kick_ms) (void)kt_cmd_timer(cmds, PROBE_KICK, (uint64_t)probe->kick_ms, 0);
    if (probe->tick_ms) (void)kt_cmd_timer(cmds, PROBE_TICK, (uint64_t)probe->tick_ms, (uint64_t)probe->tick_ms);
    if (probe->watch) (void)kt_cmd_watch(cmds, 0, probe->pipe_fds[0], POLLIN);
    (void)kt_cmd_timer(cmds, PROBE_GUARD, 20000, 0);
}

static void probe_kick(const struct probe *probe) {
    if (probe->watch) CHECK(write(probe->pipe_fds[1], "k", 1) == 1);
    else CHECK(kill(getpid(), SIGUSR1) == 0);
}

static void probe_timer(struct probe *probe, const struct kt_msg *msg, struct kt_cmds *cmds) {
    if (msg->tag == PROBE_CANCEL) kt_cmd_cancel(cmds, probe->id);
    if (msg->tag == PROBE_QUIT) kt_cmd_quit(cmds, 5);
    if (msg->tag == PROBE_GUARD) kt_cmd_quit(cmds, 99);
    if (msg->tag == PROBE_KICK) probe_kick(probe);
    if (msg->tag != PROBE_TICK) return;
    if (!probe->ticks++) probe->first_tick = msg->u.timer.now_ms;
    probe->last_tick = msg->u.timer.now_ms;
    if (hangup_reader) (void)kill(hangup_reader, SIGKILL);
    else if (probe->ticks == 4) kt_cmd_quit(cmds, 0);
}

static void probe_process(struct probe *probe, const struct kt_msg *msg, struct kt_cmds *cmds) {
    size_t kept = msg->u.process.length;
    if (msg->tag != PROBE_SPAWN) return;
    if (kept > sizeof(probe->output) - 1) kept = sizeof(probe->output) - 1;
    probe->reports++;
    probe->result = msg->u.process;
    memcpy(probe->output, msg->u.process.output, kept);
    probe->output[kept] = '\0';
    probe->result.output = probe->output;
    kt_cmd_quit(cmds, 0);
}

static bool probe_update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct probe *probe = model;
    if (msg->type == KT_MSG_TIMER) probe_timer(probe, msg, cmds);
    if (msg->type == KT_MSG_PROCESS) probe_process(probe, msg, cmds);
    if (msg->type == KT_MSG_FD) {
        CHECK(read(msg->u.fd.fd, &probe->watched, 1) == 1);
        kt_cmd_quit(cmds, 0);
    }
    if (msg->type == KT_MSG_USER + 1) {
        probe->posted_a = (int)msg->u.user.a; probe->posted_b = (int)msg->u.user.b;
        kt_cmd_quit(cmds, (int)msg->tag);
    }
    return false;
}

static void probe_view(const void *model, struct kt_frame *frame) {
    const struct probe *probe = model;
    char line[32];
    snprintf(line, sizeof(line), "reports %d ticks %d", probe->reports, probe->ticks);
    kt_text(kt_frame_canvas(frame), (struct kt_rect){0, 0, kt_frame_columns(frame), 1}, line, KT_BASE);
}

static const struct kt_app probe_app = {probe_init, probe_update, probe_view, NULL};

static int probe_run(struct probe *probe) {
    struct terminal terminal = terminal_open();
    struct kt_options options = {0};
    struct kt_program *program;
    int status;
    options.input_fd = options.output_fd = terminal.slave;
    options.alt_screen = true;
    program = kt_program_new(&probe_app, probe, &options);
    CHECK(program != NULL);
    posting = program;
    if (probe->hangup) hangup_reader = terminal.reader;
    status = kt_program_run(program);
    posting = NULL; hangup_reader = 0;
    kt_program_free(program);
    terminal_close(&terminal);
    return status;
}

static struct probe probe_for(char *const argv[]) {
    struct probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.spawn.argv = argv;
    return probe;
}

static void test_real_capture(void) {
    char *argv[] = {"sh", "-c", "printf 'one\\ntwo\\n'; echo hidden >&2; exit 3", NULL};
    char *merged[] = {"sh", "-c", "echo out; echo err >&2", NULL};
    struct probe probe = probe_for(argv);
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.reports == 1);
    CHECK(probe.result.status == 3);
    CHECK(!strcmp(probe.output, "one\ntwo\n"));
    CHECK(probe.result.length == 8);
    CHECK(!probe.result.timed_out);
    CHECK(!probe.result.cancelled);
    CHECK(!probe.result.truncated);
    CHECK(!probe.result.failed);
    probe = probe_for(merged);
    probe.spawn.merge_stderr = true;
    CHECK(probe_run(&probe) == 0);
    CHECK(!strcmp(probe.output, "out\nerr\n"));
}

static void test_real_environment(void) {
    char *argv[] = {"sh", "-c", "pwd; echo \"$KT_VALUE\"; cat", NULL};
    char *envp[] = {"KT_VALUE=42", "PATH=/usr/bin:/bin", NULL};
    struct probe probe = probe_for(argv);
    probe.spawn.envp = envp; probe.spawn.directory = "/";
    CHECK(probe_run(&probe) == 0);
    /* cat ends at once: standard input is /dev/null. */
    CHECK(!strcmp(probe.output, "/\n42\n"));
    CHECK(probe.result.status == 0);
}

static void test_real_start_failure(void) {
    char *missing[] = {"/nonexistent/kettle-program", NULL};
    char *unknown[] = {"kettle-no-such-command", NULL};
    char *argv[] = {"true", NULL};
    struct probe probe = probe_for(missing);
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.reports == 1);
    CHECK(probe.result.failed);
    CHECK(probe.result.status == 127);
    CHECK(probe.result.length == 0);
    probe = probe_for(unknown);
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.result.failed);
    probe = probe_for(argv);
    probe.spawn.directory = "/nonexistent/kettle-directory";
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.result.failed);
    CHECK(probe.reports == 1);
}

static void test_real_truncation(void) {
    char *argv[] = {"sh", "-c", "printf abcdefghij", NULL};
    char *large[] = {"dd", "if=/dev/zero", "bs=1000", "count=300", NULL};
    struct probe probe = probe_for(argv);
    probe.spawn.max_output = 4;
    CHECK(probe_run(&probe) == 0);
    CHECK(!strcmp(probe.output, "abcd"));
    CHECK(probe.result.truncated);
    CHECK(probe.result.status == 0);
    /* More than a pipe holds: read as it arrives, without blocking the loop. */
    probe = probe_for(large);
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.result.length == 300000);
    CHECK(!probe.result.truncated);
    CHECK(probe.result.status == 0);
}

static void test_real_budget(void) {
    char *argv[] = {"sh", "-c", "sleep 30 & echo $!; wait", NULL};
    char *stubborn[] = {"sh", "-c", "trap '' TERM; echo ready; while :; do sleep 1; done", NULL};
    struct probe probe = probe_for(argv);
    probe.spawn.budget_ms = 150; probe.spawn.grace_ms = 2000;
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.reports == 1);
    CHECK(probe.result.timed_out);
    CHECK(!probe.result.cancelled);
    CHECK(probe.result.status == 128 + SIGTERM);
    /* The background job shared the child's process group and went with it:
     * the output only closed because it did. */
    CHECK(atol(probe.output) > 1);
    CHECK(process_ended(atol(probe.output)));
    /* A child that ignores SIGTERM is killed after the grace period. */
    probe = probe_for(stubborn);
    probe.spawn.budget_ms = 150; probe.spawn.grace_ms = 100;
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.result.timed_out);
    CHECK(probe.result.status == 128 + SIGKILL);
    CHECK(!strcmp(probe.output, "ready\n"));
}

static void test_real_cancel(void) {
    char *argv[] = {"sleep", "30", NULL};
    struct probe probe = probe_for(argv);
    probe.cancel_ms = 50;
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.reports == 1);
    CHECK(probe.result.cancelled);
    CHECK(!probe.result.timed_out);
    CHECK(probe.result.status == 128 + SIGTERM);
}

static long pid_from(const char *path) {
    char text[32] = "";
    int attempt;
    for (attempt = 0; attempt < 300 && !strchr(text, '\n'); attempt++) {
        FILE *file = fopen(path, "r");
        if (file) {
            if (!fgets(text, sizeof(text), file)) text[0] = '\0';
            fclose(file);
        }
        if (!strchr(text, '\n')) pause_ms(10);
    }
    return atol(text);
}

static void test_real_exit_with_children(void) {
    const char *temporary = getenv("TMPDIR");
    char directory[512], owned_path[600], left_path[600];
    char *owned[] = {"sh", "-c", "echo $$ > \"$1\"; exec sleep 30", "sh", owned_path, NULL};
    char *left[] = {"sh", "-c", "echo $$ > \"$1\"; exec sleep 30", "sh", left_path, NULL};
    struct probe probe = probe_for(owned);
    long owned_pid, left_pid;
    snprintf(directory, sizeof(directory), "%s/kettle-program-XXXXXX", temporary ? temporary : "/tmp");
    CHECK(mkdtemp(directory) != NULL);
    snprintf(owned_path, sizeof(owned_path), "%s/owned", directory);
    snprintf(left_path, sizeof(left_path), "%s/left", directory);
    probe.left.argv = left; probe.left.on_exit = KT_SPAWN_LEAVE;
    probe.quit_ms = 300;
    CHECK(probe_run(&probe) == 5);
    owned_pid = pid_from(owned_path); left_pid = pid_from(left_path);
    CHECK(owned_pid > 1);
    CHECK(left_pid > 1);
    /* The owned child was stopped, reaped and reported before run returned. */
    CHECK(probe.reports == 1);
    CHECK(probe.result.cancelled);
    CHECK(kill((pid_t)owned_pid, 0) < 0);
    /* The other one was left running and unreaped. */
    CHECK(process_state(left_pid) != '\0');
    CHECK(process_state(left_pid) != 'Z');
    CHECK(kill((pid_t)left_pid, SIGKILL) == 0);
    CHECK(waitpid((pid_t)left_pid, NULL, 0) == (pid_t)left_pid);
    CHECK(unlink(owned_path) == 0);
    CHECK(unlink(left_path) == 0);
    CHECK(rmdir(directory) == 0);
}

static void test_real_timer_and_watch(void) {
    struct probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.tick_ms = 20;
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.ticks == 4);
    /* Monotonic and never early. */
    CHECK(probe.first_tick >= probe.started + 20);
    CHECK(probe.last_tick >= probe.started + 80);
    memset(&probe, 0, sizeof(probe));
    CHECK(pipe(probe.pipe_fds) == 0);
    probe.watch = true; probe.kick_ms = 20;
    CHECK(probe_run(&probe) == 0);
    CHECK(probe.watched == 'k');
    close(probe.pipe_fds[0]); close(probe.pipe_fds[1]);
}

/* The terminal goes away while the program runs: the next read or write
 * fails, and the program ends with the error status instead of spinning. */
static void test_real_hangup(void) {
    struct probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.tick_ms = 20; probe.hangup = true;
    CHECK(probe_run(&probe) == -1);
    CHECK(probe.ticks >= 1);
}

static void test_real_post(void) {
    struct sigaction action, previous;
    struct probe probe;
    struct kt_msg msg;
    memset(&msg, 0, sizeof(msg));
    memset(&action, 0, sizeof(action));
    action.sa_handler = post_from_handler;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0);
    memset(&probe, 0, sizeof(probe));
    probe.kick_ms = 20;
    /* Posted from a signal handler while the loop sleeps in poll. */
    CHECK(probe_run(&probe) == 9);
    CHECK(probe.posted_a == 41);
    CHECK(probe.posted_b == 42);
    CHECK(sigaction(SIGUSR1, &previous, NULL) == 0);
    CHECK(!kt_program_post(NULL, &msg));
}

static bool same_terminal(const struct termios *a, const struct termios *b) {
    bool flags = a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag && a->c_cflag == b->c_cflag &&
                 a->c_lflag == b->c_lflag;
    bool speeds = cfgetispeed(a) == cfgetispeed(b) && cfgetospeed(a) == cfgetospeed(b);
    return flags && speeds && !memcmp(a->c_cc, b->c_cc, sizeof(a->c_cc));
}

static const int handled_signals[] = {SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGCHLD, SIGCONT, SIGTSTP,
                                      SIGQUIT, SIGPIPE, SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
#define HANDLED (sizeof(handled_signals) / sizeof(*handled_signals))

static void actions_read(struct sigaction actions[HANDLED]) {
    size_t i;
    for (i = 0; i < HANDLED; i++) CHECK(sigaction(handled_signals[i], NULL, &actions[i]) == 0);
}

static bool actions_same(const struct sigaction before[HANDLED]) {
    struct sigaction after[HANDLED];
    size_t i;
    actions_read(after);
    for (i = 0; i < HANDLED; i++) if (after[i].sa_handler != before[i].sa_handler) return false;
    return true;
}

/* The same program object runs twice; each run leaves the terminal, the
 * descriptor flags and the signal dispositions exactly as it found them. */
static void test_real_restoration(void) {
    struct terminal terminal = terminal_open();
    struct kt_options options = {0};
    struct sigaction before[HANDLED];
    struct termios saved, after;
    struct kt_program *program;
    struct probe probe;
    struct kt_msg msg;
    int run, flags;
    memset(&msg, 0, sizeof(msg));
    memset(&probe, 0, sizeof(probe));
    probe.quit_ms = 30;
    options.input_fd = options.output_fd = terminal.slave;
    CHECK(tcgetattr(terminal.slave, &saved) == 0);
    flags = fcntl(terminal.slave, F_GETFL);
    actions_read(before);
    program = kt_program_new(&probe_app, &probe, &options);
    CHECK(program != NULL);
    CHECK(!kt_program_post(program, &msg));
    for (run = 0; run < 2; run++) {
        CHECK(kt_program_run(program) == 5);
        CHECK(tcgetattr(terminal.slave, &after) == 0);
        CHECK(same_terminal(&saved, &after));
        CHECK(fcntl(terminal.slave, F_GETFL) == flags);
        CHECK(actions_same(before));
        CHECK(!kt_program_post(program, &msg));
    }
    kt_program_free(program);
    terminal_close(&terminal);
}

static void test_real_needs_terminal(void) {
    struct kt_options options = {0};
    struct kt_program *program;
    struct probe probe;
    int fds[2];
    memset(&probe, 0, sizeof(probe));
    CHECK(pipe(fds) == 0);
    options.input_fd = fds[0]; options.output_fd = fds[1];
    program = kt_program_new(&probe_app, &probe, &options);
    CHECK(program != NULL);
    CHECK(kt_program_run(program) == -1);
    kt_program_free(program);
    close(fds[0]); close(fds[1]);
}

static void real(void) {
    test_real_needs_terminal();
    test_real_restoration();
    test_real_timer_and_watch();
    test_real_hangup();
    test_real_post();
    test_real_capture();
    test_real_environment();
    test_real_start_failure();
    test_real_truncation();
    test_real_budget();
    test_real_cancel();
    test_real_exit_with_children();
    puts("program (processes): capture, status, environment, start failure, truncation, budget, "
         "cancel, exit policy, post from a handler, timers, watches, hangup and restoration passed");
}

int main(void) {
    headless();
    real();
    return 0;
}
