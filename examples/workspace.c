#define _POSIX_C_SOURCE 200809L
#include "workspace_demo.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The workspace demo as a kt_program application. The embedded shell's PTY is
 * a watched descriptor in the runtime's loop: its output is drawn as soon as
 * it arrives and typing reaches it without waiting for a tick. */

static void child_send(struct demo *d, const void *bytes, size_t length) {
    if (!kt_pty_enqueue(&d->child, bytes, length))
        snprintf(d->notice, sizeof(d->notice), "Input not delivered: %s", d->child.finished ? "child exited" : "input queue full");
}

/* A paste reaches the shell whole, marked as a paste only if the program
 * running there asked for that. */
static void demo_paste(struct demo *d, const char *bytes, size_t length) {
    if (!d->model || d->workspace.focus != 3) return;
    if (d->model->bracketed_paste) child_send(d, "\033[200~", 6);
    child_send(d, bytes, length);
    if (d->model->bracketed_paste) child_send(d, "\033[201~", 6);
}

/* Keep a scrolled-back view on the same lines while new output arrives. */
static void history_anchor(struct demo *d, uint64_t before) {
    size_t *scroll = &d->workspace.panes[3].scroll;
    uint64_t added = d->model->history_serial - before;
    if (!*scroll || !added) return;
    if (*scroll > d->model->history_count) *scroll = d->model->history_count;
    *scroll = added > d->model->history_count - *scroll ? d->model->history_count : *scroll + (size_t)added;
}

/* The watch is level-triggered: what is not read now is reported again. */
static bool child_read(struct demo *d) {
    uint64_t before = d->model->history_serial;
    unsigned char bytes[4096];
    size_t chunk;
    for (chunk = 0; chunk < 8 && !d->child.eof; chunk++) {
        ssize_t length = kt_pty_read(&d->child, bytes, sizeof(bytes));
        if (length > 0) kt_term_feed(d->model, bytes, (size_t)length);
        else if (length == 0) kt_term_finish(d->model);
        else if (errno != EAGAIN && errno != EINTR) return false;
        else break;
    }
    history_anchor(d, before);
    return true;
}

/* Panes follow the terminal size and dragged dividers; the shell's terminal
 * follows its pane. */
static bool demo_layout(struct demo *d) {
    struct kt_rect pane;
    if (d->columns < 2 || d->rows < 3) return true;
    (void)kt_workspace_layout(&d->workspace, (struct kt_rect){0, 1, d->columns, d->rows - 2});
    pane = d->workspace.panes[1].bounds;
    d->workspace.panes[1].scroll = kt_tree_scroll(&d->tree, d->workspace.panes[1].scroll, pane.height - 3);
    pane = d->workspace.panes[3].bounds;
    if (!d->model || pane.width <= 2 || pane.height <= 3) return true;
    if (pane.width - 2 == d->model->primary.canvas.width && pane.height - 3 == d->model->primary.canvas.height) return true;
    return kt_term_resize(d->model, pane.width - 2, pane.height - 3) && kt_pty_resize(&d->child, pane.width - 2, pane.height - 3);
}

/* Readable while the shell lives; writable only while input is queued. */
static void child_watch(struct demo *d, struct kt_cmds *cmds) {
    short wanted = 0;
    if (!d->child.eof) wanted = d->child.pending_length ? POLLIN | POLLOUT : POLLIN;
    if (wanted == d->watching) return;
    if (d->watching) kt_cmd_cancel(cmds, d->watch);
    d->watch = wanted ? kt_cmd_watch(cmds, 0, d->child.fd, wanted) : 0;
    d->watching = d->watch ? wanted : 0;
}

/* Runs after every message: whatever changed, the layout, the shell's
 * terminal, its queued input and the watch are brought back in step. */
static bool demo_settle(struct demo *d, struct kt_cmds *cmds) {
    char reply[256];
    if (!demo_layout(d)) return false;
    if (!d->model) return true;
    if (!d->child.finished && d->model->reply_length) {
        size_t length = kt_term_take_reply(d->model, reply, sizeof(reply));
        if (!kt_pty_enqueue(&d->child, reply, length)) return false;
    }
    if (!kt_pty_flush(&d->child)) return false;
    child_watch(d, cmds);
    return true;
}

static void init(void *model, struct kt_cmds *cmds) {
    struct demo *d = model;
    if (!d->model) return;
    if (!kt_pty_spawn(&d->child, d->child_argv[0], d->child_argv, &d->settings, 80, 24)) {
        d->spawn_error = errno ? errno : EIO;
        kt_cmd_quit(cmds, 1);
        return;
    }
    d->workspace.focus = 3;
    child_watch(d, cmds);
}

static bool update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct demo *d = model;
    bool working = true;
    switch (msg->type) {
    case KT_MSG_RESIZE: d->columns = msg->u.resize.columns; d->rows = msg->u.resize.rows; break;
    case KT_MSG_KEY: case KT_MSG_MOUSE:
        if (!demo_handle(d, &msg->u.event)) kt_cmd_quit(cmds, 0);
        break;
    case KT_MSG_PASTE: demo_paste(d, msg->u.paste.bytes, msg->u.paste.length); break;
    case KT_MSG_FD: working = !(msg->u.fd.revents & POLLNVAL) && child_read(d) && kt_pty_reap(&d->child); break;
    case KT_MSG_SIGNAL:
        /* Anything but a child's exit is left to the runtime, which quits. */
        if (msg->u.signal.number != SIGCHLD) return false;
        working = !d->model || kt_pty_reap(&d->child);
        break;
    default: return false;
    }
    if (!working || !demo_settle(d, cmds)) kt_cmd_quit(cmds, 1);
    return true;
}

static void view(const void *model, struct kt_frame *frame) {
    if (kt_frame_columns(frame) >= 2 && kt_frame_rows(frame) >= 3) demo_draw(kt_frame_canvas(frame), model);
}

static bool demo_storage(struct demo *d, struct kt_cell **screens, struct kt_cell **history) {
    const size_t capacity = 512U * 256U, history_rows = 512;
    *screens = calloc(capacity * 2, sizeof(**screens));
    *history = calloc(512 * history_rows, sizeof(**history));
    d->model = calloc(1, sizeof(*d->model));
    return *screens && *history && d->model &&
        kt_term_init(d->model, *screens, *screens + capacity, 512, 256, *history, history_rows, 80, 24);
}

static void demo_content(struct demo *d) {
    static const char *labels[] = {"Engine", "renderer", "input", "Unicode", "Workspace", "navigation", "layout", "Terminal", "shell", "parser", "history", "PTY"};
    size_t n;
    kt_workspace_init(&d->workspace, 16, 5);
    (void)kt_workspace_split(&d->workspace, 0, KT_COLUMNS, 240);
    (void)kt_workspace_split(&d->workspace, 2, KT_ROWS, 600);
    for (n = 0; n < 12; n++) d->nodes[n] = (struct kt_tree_node){labels[n], n, (n == 0 || n == 4 || n == 7) ? 0U : 1U, true, KT_BASE};
    (void)kt_tree_init(&d->tree, d->nodes, 12);
}

static int demo_run(struct demo *d) {
    static const struct kt_app app = {init, update, view, demo_colors};
    struct kt_options options = {0};
    struct kt_program *program;
    int status;
    options.alt_screen = options.bracketed_paste = true;
    options.mouse = KT_MOUSE_DRAG;
    /* The shell starts with the settings this terminal had before raw mode. */
    if (d->model && tcgetattr(STDIN_FILENO, &d->settings) != 0) return -1;
    program = kt_program_new(&app, d, &options);
    if (!program) return 1;
    status = kt_program_run(program);
    kt_program_free(program);
    kt_pty_close(&d->child);
    return status;
}

int main(int argc, char **argv) {
    static char *shell_argv[] = {"/bin/sh", "-i", NULL};
    struct kt_cell *screens = NULL, *history = NULL;
    struct demo demo;
    bool shell = false;
    int status = 1;
    memset(&demo, 0, sizeof(demo)); demo.child.fd = -1; demo.child.pid = -1;
    demo.child_argv = shell_argv;
    if (argc == 2 && !strcmp(argv[1], "--shell")) shell = true;
    else if (argc > 2 && !strcmp(argv[1], "--command")) { shell = true; demo.child_argv = argv + 2; }
    else if (argc != 1) { fputs("Usage: kettle-workspace [--shell | --command PROGRAM ARGS...]\n", stderr); return 2; }
    demo_content(&demo);
    if (!shell || demo_storage(&demo, &screens, &history)) status = demo_run(&demo);
    if (status < 0) { fputs("An interactive terminal is required\n", stderr); status = 1; }
    if (demo.spawn_error) fprintf(stderr, "Unable to start the embedded child: %s\n", strerror(demo.spawn_error));
    free(demo.model); free(history); free(screens);
    return status;
}
