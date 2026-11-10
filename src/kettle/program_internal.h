#ifndef KT_PROGRAM_INTERNAL_H
#define KT_PROGRAM_INTERNAL_H
/* Shared by the runtime's loop (program.c, program_frame.c, program_child.c),
 * its POSIX backend (program_posix.c) and the headless backend (testing.c).
 * Not an application interface.
 *
 * Everything the loop needs from the outside world goes through struct
 * kt_backend, so the real terminal and the test driver run the same loop. */
#include "program.h"
#include <poll.h>

#define KT_NEVER UINT64_MAX
#define KT_TITLE_MAX 256
#define KT_LEAVE_MAX 192

/* Signals a backend reports from its wait. */
enum kt_wake {
    KT_WAKE_RESIZE = 1, KT_WAKE_INTERRUPT = 2, KT_WAKE_TERMINATE = 4, KT_WAKE_HANGUP = 8,
    KT_WAKE_CHILD = 16, KT_WAKE_CONTINUE = 32, KT_WAKE_STOP = 64
};
/* In: whether terminal input is wanted. Out: what the wait found. */
struct kt_ready { bool input; unsigned signals; };

/* Results of the two read operations besides a positive byte count. */
enum { KT_READ_END = 0, KT_READ_NONE = -1, KT_READ_ERROR = -2 };

struct kt_backend {
    uint64_t (*now)(void *context);                 /* monotonic milliseconds */
    /* Takes the terminal (raw mode, signal handlers, wake-up channel) and
     * gives it back exactly as found. */
    bool (*begin)(void *context);
    void (*end)(void *context);
    /* Switches between raw and the saved cooked mode while begun. */
    bool (*raw)(void *context, bool enable);
    bool (*size)(void *context, int *columns, int *rows);
    /* Writes all of `bytes`: one write unless the terminal accepts less. */
    bool (*write)(void *context, const char *bytes, size_t length);
    long (*read)(void *context, void *buffer, size_t capacity);
    /* Blocks up to timeout_ms (-1 = until something happens). fds[0] and
     * fds[1] belong to the backend; the rest are the loop's watches and child
     * pipes, whose revents the backend fills. False ends the program. */
    bool (*wait)(void *context, struct pollfd *fds, size_t count, int timeout_ms,
                 struct kt_ready *ready);
    /* Remembers the bytes that undo the terminal modes now in effect, also
     * for exit paths that cannot run the loop's teardown (exit(), fatal
     * signals). leave writes them and forgets them. */
    void (*restore)(void *context, const char *bytes, size_t length);
    void (*leave)(void *context);
    /* kt_program_post and its receiving end. */
    bool (*post)(void *context, const struct kt_msg *msg);
    bool (*posted)(void *context, struct kt_msg *msg);
    /* Child processes. `pid` identifies the child to the other operations;
     * `fd` is polled for its output when it is a real descriptor. */
    bool (*spawn)(void *context, const struct kt_spawn *spawn, uint64_t id, uint64_t tag,
                  pid_t *pid, int *fd);
    bool (*child_ready)(void *context, pid_t pid);  /* optional: output without a descriptor */
    long (*child_read)(void *context, pid_t pid, int fd, void *buffer, size_t capacity);
    void (*child_signal)(void *context, pid_t pid, int signal_number);
    /* Nonblocking. True once the child is gone; status is the exit code,
     * 128 + signal, or -1 when it could not be learned. */
    bool (*child_reap)(void *context, pid_t pid, int *status);
    void (*child_close)(void *context, pid_t pid, int fd);
    int (*exec)(void *context, char *const argv[]);
    void (*suspend)(void *context);
    void (*destroy)(void *context);
};

struct kt_queued { struct kt_msg msg; void *owned; };
struct kt_timer { uint64_t id, tag, at, interval; };
struct kt_watch { uint64_t id, tag; int fd; short events; };
struct kt_child {
    uint64_t id, tag, grace, budget_at, kill_at, abandon_at;
    pid_t pid;
    int fd;
    char *output;
    size_t length, capacity, limit;
    bool eof, stopping, killed, timed_out, cancelled, truncated, failed, leave;
};

enum kt_cmd_type {
    KT_CMD_QUIT, KT_CMD_TIMER, KT_CMD_WATCH, KT_CMD_SPAWN, KT_CMD_CANCEL,
    KT_CMD_EXEC, KT_CMD_SUSPEND, KT_CMD_POST, KT_CMD_REPAINT
};
struct kt_cmd {
    enum kt_cmd_type type;
    uint64_t id, tag;
    union {
        int status;
        struct { uint64_t delay, interval; } timer;
        struct { int fd; short events; } watch;
        struct { struct kt_spawn spec; void *block; } spawn;
        struct kt_msg msg;
    } u;
};
struct kt_cmds { struct kt_program *program; struct kt_cmd *items; size_t count, capacity; };

/* Terminal state the runtime has put into effect; all zero is the terminal
 * as it was found. */
struct kt_modes {
    bool screen, paste, focus, cursor_hidden, titled;
    enum kt_mouse_mode mouse;
    int cursor_x, cursor_y;
    char title[KT_TITLE_MAX];
};
struct kt_frame {
    struct kt_canvas canvas;
    bool cursor, titled;
    int cursor_x, cursor_y;
    enum kt_mouse_mode mouse;
    char title[KT_TITLE_MAX];
};

struct kt_program {
    const struct kt_app *app;
    void *model;
    struct kt_options options;
    const struct kt_backend *backend;
    void *context;
    bool begun, running, dirty;
    int status;
    /* Messages wait here between a wake-up and update. */
    struct kt_queued *queue;
    size_t queue_head, queue_count, queue_capacity;
    void *held;
    /* Terminal input is decoded lazily, one message at a time. */
    struct kt_input decoder;
    unsigned char input[4096];
    size_t input_at, input_length;
    bool input_closed, pasting, paste_truncated;
    uint64_t escape_at;
    char *paste;
    size_t paste_length, paste_capacity;
    /* Effects. */
    struct kt_cmds cmds;
    uint64_t next_id;
    struct kt_timer *timers;
    size_t timer_count, timer_capacity;
    struct kt_watch *watches;
    size_t watch_count, watch_capacity;
    struct kt_child *children;
    size_t child_count, child_capacity;
    struct pollfd *fds;
    size_t fd_capacity;
    /* Frame. */
    struct kt_cell *cells, *previous;
    size_t cell_capacity;
    struct kt_canvas canvas;
    struct kt_presenter presenter;
    int terminal_columns, terminal_rows;
    FILE *out;
    char *out_bytes;
    size_t out_size;
    uint64_t frame_at, frame_interval;
    struct kt_modes applied;
    bool reassert;
    char leave[KT_LEAVE_MAX];
    size_t leave_length;
};

/* program.c */
struct kt_program *kt_program_new_backend(const struct kt_app *app, void *model,
                                          const struct kt_options *options,
                                          const struct kt_backend *backend, void *context);
void *kt_program_allocate(struct kt_program *p, void *pointer, size_t size);
/* Grows an array to hold `needed` items; returns the array or NULL. */
void *kt_program_grow(struct kt_program *p, void *items, size_t *capacity, size_t needed, size_t size);
bool kt_program_push(struct kt_program *p, const struct kt_msg *msg, void *owned);
struct kt_msg kt_program_message(uint32_t type, uint64_t tag);
void kt_program_fail(struct kt_program *p);
/* One iteration of kt_program_run: renders when a frame is due, then waits up
 * to limit_ms (-1 = as long as the loop would) for a message and dispatches
 * it. False when no message arrived. */
bool kt_program_turn(struct kt_program *p, int limit_ms);
/* Milliseconds until the loop would wake by itself, -1 when only an event
 * can wake it. */
int kt_program_sleep(struct kt_program *p);
/* Deep copy of argv, envp and directory into one block the caller frees. */
void *kt_spawn_copy(const struct kt_allocator *allocator, const struct kt_spawn *from,
                    struct kt_spawn *to);

/* program_frame.c */
bool kt_frame_resize(struct kt_program *p, int columns, int rows);
bool kt_frame_render(struct kt_program *p);
/* Writes what undoes the applied modes and forgets them; the next frame
 * enters the screen again. */
void kt_frame_leave(struct kt_program *p);
/* The terminal may have lost the applied modes: the next frame sets them all
 * again. Until then they still count as applied and are undone on exit. */
void kt_frame_reassert(struct kt_program *p);

/* program_child.c */
void kt_child_start(struct kt_program *p, uint64_t id, uint64_t tag, const struct kt_spawn *spawn);
bool kt_child_cancel(struct kt_program *p, uint64_t id);
void kt_children_service(struct kt_program *p, const struct pollfd *fds);
uint64_t kt_children_deadline(const struct kt_program *p);
void kt_children_stop(struct kt_program *p);

/* program_posix.c */
extern const struct kt_backend kt_posix_backend;
void *kt_posix_backend_new(const struct kt_options *options);

#endif
