#define _POSIX_C_SOURCE 200809L
#include "workspace_demo.h"
#include "posix.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int signum) { stopped = signum; }
static long long milliseconds(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
    struct tv_terminal terminal = {0};
    struct demo demo;
    struct tv_input input;
    struct tv_event event;
    struct tv_canvas canvas;
    struct tv_presenter presenter;
    struct tv_cell *cells = NULL, *previous = NULL;
    struct tv_cell *screens = NULL, *history = NULL;
    const size_t history_rows = 512;
    char *shell_argv[] = {"/bin/sh", "-i", NULL};
    char **child_argv = shell_argv;
    bool shell = false;
    struct sigaction action;
    const size_t capacity = 512U * 256U;
    int cols = 80, rows = 24, result = 1;
    bool running = true;
    long long last_input = 0;
    memset(&demo, 0, sizeof(demo)); demo.child.fd = -1; demo.child.pid = -1;
    if (argc > 1 && !strcmp(argv[1], "--shell")) shell = true;
    else if (argc > 2 && !strcmp(argv[1], "--command")) { shell = true; child_argv = argv + 2; }
    else if (argc != 1) { fputs("Usage: termviz-workspace [--shell | --command PROGRAM ARGS...]\n", stderr); return 2; }
    if (shell && argc > 2 && !strcmp(argv[1], "--shell")) return 2;
    cells = calloc(capacity, sizeof(*cells)); previous = calloc(capacity, sizeof(*previous));
    if (!cells || !previous) goto cleanup;
    if (shell) {
        screens = calloc(capacity * 2, sizeof(*screens));
        history = calloc(512 * history_rows, sizeof(*history));
        demo.model = calloc(1, sizeof(*demo.model));
        if (!screens || !history || !demo.model) goto cleanup;
        if (!tv_term_init(demo.model, screens, screens + capacity, 512, 256, history, history_rows, 80, 24)) goto cleanup;
    }
    tv_workspace_init(&demo.workspace, 16, 5);
    (void)tv_workspace_split(&demo.workspace, 0, TV_COLUMNS, 240);
    (void)tv_workspace_split(&demo.workspace, 2, TV_ROWS, 600);
    {
        static const char *labels[] = {"Engine", "renderer", "input", "Unicode", "Workspace", "navigation", "layout", "Terminal", "shell", "parser", "history", "PTY"};
        size_t n;
        for (n = 0; n < 12; n++) demo.nodes[n] = (struct tv_tree_node){labels[n], n, (n == 0 || n == 4 || n == 7) ? 0U : 1U, true, TV_BASE};
        (void)tv_tree_init(&demo.tree, demo.nodes, 12);
    }
    tv_input_init(&input);
    (void)tv_present_init(&presenter, previous, capacity);
    memset(&action, 0, sizeof(action)); action.sa_handler = stop; sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL); sigaction(SIGHUP, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    if (!tv_terminal_begin(&terminal)) { fputs("An interactive terminal is required\n", stderr); goto cleanup; }
    if (shell) {
        if (!tv_pty_spawn(&demo.child, child_argv[0], child_argv, &terminal.saved, 80, 24)) {
            tv_terminal_end(&terminal); perror("Unable to start the embedded child"); goto cleanup;
        }
        demo.workspace.focus = 3;
    }
    result = 0;
    while (running && !stopped) {
        struct pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {demo.child.fd, POLLIN, 0}};
        nfds_t nfds = shell && !demo.child.eof ? 2 : 1;
        unsigned char bytes[4096];
        ssize_t length;
        int ready;
        size_t i;
        if (!tv_terminal_size(&cols, &rows) || cols < 2 || rows < 3) { result = 2; break; }
        (void)tv_init(&canvas, cells, capacity, cols - 1, rows, true);
        (void)tv_workspace_layout(&demo.workspace, (struct tv_rect){0,1,cols-1,rows-2});
        if (shell) {
            struct tv_rect r = demo.workspace.panes[3].bounds;
            if (r.width > 2 && r.height > 3 && (r.width - 2 != demo.model->primary.canvas.width || r.height - 3 != demo.model->primary.canvas.height)) {
                if (!tv_term_resize(demo.model, r.width - 2, r.height - 3) || !tv_pty_resize(&demo.child, r.width - 2, r.height - 3)) { result = 1; goto cleanup; }
            }
            if (!tv_pty_reap(&demo.child)) { result = 1; goto cleanup; }
            if (demo.child.pending_length) fds[1].events |= POLLOUT;
        }
        demo_draw(&canvas, &demo);
        if (!tv_present(&presenter, &canvas, stdout, demo_colors, NULL)) { result = 1; goto cleanup; }
        ready = poll(fds, nfds, 40);
        if (ready < 0 && errno != EINTR) { result = 1; goto cleanup; }
        if (input.length && milliseconds() - last_input >= 40 && tv_input_flush(&input, &event)) running = demo_handle(&demo, &event);
        if (ready > 0 && (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL))) { result = 1; goto cleanup; }
        if (ready > 0 && nfds == 2) {
            if (fds[1].revents & POLLNVAL) { result = 1; goto cleanup; }
            if (fds[1].revents & (POLLIN | POLLHUP)) {
                uint64_t before = demo.model->history_serial;
                size_t chunks;
                for (chunks = 0; chunks < 8; chunks++) {
                    length = tv_pty_read(&demo.child, bytes, sizeof(bytes));
                    if (length > 0) tv_term_feed(demo.model, bytes, (size_t)length);
                    else {
                        if (length == 0) tv_term_finish(demo.model);
                        else if (errno != EAGAIN && errno != EINTR) { result = 1; goto cleanup; }
                        break;
                    }
                }
                if (demo.workspace.panes[3].scroll && demo.model->history_serial > before) {
                    uint64_t added = demo.model->history_serial - before;
                    size_t *scroll = &demo.workspace.panes[3].scroll;
                    if (*scroll > demo.model->history_count) *scroll = demo.model->history_count;
                    *scroll = added > demo.model->history_count - *scroll ? demo.model->history_count : *scroll + (size_t)added;
                }
            }
            if ((fds[1].revents & POLLOUT) && !tv_pty_flush(&demo.child)) { result = 1; goto cleanup; }
        }
        if (shell && !demo.child.finished && demo.model->reply_length) {
            char reply[256]; size_t n = tv_term_take_reply(demo.model, reply, sizeof(reply));
            if (!tv_pty_enqueue(&demo.child, reply, n)) { result = 1; goto cleanup; }
        }
        if (ready > 0 && (fds[0].revents & POLLIN)) {
            length = read(STDIN_FILENO, bytes, sizeof(bytes));
            if (length > 0) last_input = milliseconds();
            if (length < 0 && errno != EAGAIN && errno != EINTR) { result = 1; goto cleanup; }
            for (i = 0; length > 0 && i < (size_t)length && running; i++)
                if (tv_input_feed(&input, bytes[i], &event)) running = demo_handle(&demo, &event);
        }
    }
    if (stopped) result = 128 + stopped;
cleanup:
    tv_pty_close(&demo.child);
    tv_terminal_end(&terminal);
    free(demo.model); free(history); free(screens);
    free(previous); free(cells);
    return result;
}
