#ifndef KT_PROGRAM_H
#define KT_PROGRAM_H
/* kettle runtime: an Elm-style program loop for terminal applications.
 *
 *   model   application state, owned by the application and mutated in place
 *   update  the only place the model changes; receives one message at a time
 *   view    draws the model into a cell canvas; must not change the model
 *   cmds    effects described as data (timers, child processes, fd watches,
 *           quit); the runtime performs them and reports back with a message
 *
 * The runtime owns the event loop, terminal modes, signals, the frame buffers
 * and output. update and view never run concurrently, and never re-enter
 * unless update pumps the loop itself (see "pumping from inside update"). One
 * thread drives everything; kt_program_post is the only cross-thread entry.
 *
 * This is the optional POSIX layer (poll, sigaction, fork). It allocates with
 * the supplied allocator. The portable core (kettle.h, input.h, keymap.h, ...)
 * stays allocation-free and never includes this header. */
#include "kettle.h"
#include "input.h"
#include <sys/types.h>

struct kt_program;
struct kt_cmds;
struct kt_frame;

/* ---- messages --------------------------------------------------------- */

enum kt_msg_type {
    KT_MSG_KEY,      /* u.event: one decoded key, raw bytes included          */
    KT_MSG_MOUSE,    /* u.event: SGR mouse press/release/motion/wheel         */
    KT_MSG_PASTE,    /* u.paste: one whole bracketed paste                    */
    KT_MSG_RESIZE,   /* u.resize: sent before the first view and on SIGWINCH  */
    KT_MSG_FOCUS,    /* terminal gained focus (needs focus_events)            */
    KT_MSG_BLUR,     /* terminal lost focus                                   */
    KT_MSG_TIMER,    /* u.timer: a kt_cmd_timer deadline passed               */
    KT_MSG_FD,       /* u.fd: a watched descriptor is ready                   */
    KT_MSG_PROCESS,  /* u.process: a kt_cmd_spawn child finished              */
    KT_MSG_EXEC,     /* u.exec: a kt_cmd_exec child returned the terminal     */
    KT_MSG_SIGNAL,   /* u.signal: SIGINT/SIGTERM/SIGHUP/SIGQUIT/SIGCHLD arrived */
    KT_MSG_RESUME,   /* the program continued after a suspend or stop signal  */
    KT_MSG_USER = 0x1000  /* applications number their own types from here   */
};

struct kt_process_result {
    uint64_t id;            /* the id kt_cmd_spawn returned                   */
    int status;             /* exit code, or 128 + signal                     */
    bool timed_out;         /* stopped because budget_ms passed               */
    bool cancelled;         /* stopped by kt_cmd_cancel or program exit       */
    bool truncated;         /* output exceeded max_output and was cut         */
    bool failed;            /* could not start, or its output or status was lost */
    const char *output;     /* captured stdout, NUL-terminated                */
    size_t length;          /* bytes in output, excluding the terminator      */
};

/* A message is a plain value. Pointers inside it (paste bytes, process output)
 * are owned by the runtime and stay valid only until update returns (with the
 * pump below: until the next kt_program_next at the same level); copy what the
 * model keeps. `tag` is the caller's routing value from the command that
 * produced the message (0 for input and signals).
 *
 * KT_MSG_SIGNAL: an unhandled SIGINT, SIGTERM, SIGHUP or SIGQUIT quits with
 * the terminal restored. In raw mode the terminal's interrupt and quit keys
 * are ordinary keys; the signals come from outside, or from the keyboard while
 * the terminal is lent out. SIGCHLD arrives after the runtime has reaped its
 * own kt_cmd_spawn children, so an application that owns other children (a PTY
 * child) can reap them; unhandled it is ignored. */
struct kt_msg {
    uint32_t type;
    uint64_t tag;
    union {
        struct kt_event event;
        struct { const char *bytes; size_t length; bool truncated; } paste;
        struct { int columns, rows; } resize;
        struct { uint64_t id, now_ms; } timer;
        struct { uint64_t id; int fd; short revents; } fd;
        struct kt_process_result process;
        struct { int status; } exec;
        struct { int number; } signal;
        struct { void *pointer; uint64_t a, b; } user;
    } u;
};

/* ---- commands ---------------------------------------------------------- */

/* Commands are queued during init/update and performed after it returns.
 * Queuing several is a batch: they run independently. To sequence work, issue
 * the next command when the previous one's message arrives.
 * Functions that return an id return 0 when the command could not be queued. */

/* Stop the loop after this update; kt_program_run returns `status`. */
void kt_cmd_quit(struct kt_cmds *cmds, int status);

/* KT_MSG_TIMER after delay_ms; if interval_ms > 0, again every interval_ms
 * until cancelled. Timers use a monotonic clock and never fire early. */
uint64_t kt_cmd_timer(struct kt_cmds *cmds, uint64_t tag, uint64_t delay_ms, uint64_t interval_ms);

/* KT_MSG_FD whenever `fd` has any of `events` (poll(2) flags) or an error.
 * Level-triggered: update must consume the readiness (read, write or cancel),
 * or it is reported again. The runtime never reads or closes the descriptor;
 * cancel the watch before closing it. A watch whose descriptor is not open is
 * reported once with POLLNVAL and then cancelled.
 * This is how PTYs, sockets and pipes join the loop instead of being polled. */
uint64_t kt_cmd_watch(struct kt_cmds *cmds, uint64_t tag, int fd, short events);

/* What happens to a child that is still running when the program exits. */
enum kt_spawn_exit {
    KT_SPAWN_KILL,   /* default: SIGTERM its process group, SIGKILL after grace_ms */
    KT_SPAWN_LEAVE   /* stop tracking it: its output pipe is closed, it keeps
                        running, is not reaped and reports no KT_MSG_PROCESS     */
};
struct kt_spawn {
    char *const *argv;        /* argv[0] is resolved with PATH; no shell        */
    char *const *envp;        /* NULL inherits the environment                  */
    const char *directory;    /* NULL keeps the working directory               */
    long budget_ms;           /* 0 = no time limit                              */
    long grace_ms;            /* SIGTERM-to-SIGKILL delay; 0 = 500              */
    size_t max_output;        /* 0 = 1 MiB; more output sets `truncated`        */
    bool merge_stderr;        /* false sends stderr to /dev/null                */
    enum kt_spawn_exit on_exit;
};
/* Runs a child in its own session with stdin from /dev/null and stdout
 * captured without blocking the loop; KT_MSG_PROCESS reports the outcome
 * exactly once, also on timeout, cancellation and start failure. argv, envp
 * and directory are copied before this returns.
 * The child is finished when it has exited and every process holding its
 * output has closed it. Children still running when the program ends are
 * stopped and reported (cancelled = true) to update from kt_program_end,
 * before kt_program_run returns; commands queued by those updates are dropped. */
uint64_t kt_cmd_spawn(struct kt_cmds *cmds, uint64_t tag, const struct kt_spawn *spawn);

/* Stops a timer, watch or spawned child by id. Unknown ids are ignored. A
 * cancelled child still produces its KT_MSG_PROCESS (cancelled = true); a
 * cancelled timer or watch produces nothing further. */
void kt_cmd_cancel(struct kt_cmds *cmds, uint64_t id);

/* Hands the terminal to a foreground child (an editor, a sign-in flow): leaves
 * raw mode and the alternate screen, runs argv with the terminal as its stdio,
 * waits, restores every mode, repaints, then sends KT_MSG_EXEC. Input typed
 * while the child ran belongs to the child. */
void kt_cmd_exec(struct kt_cmds *cmds, uint64_t tag, char *const argv[]);

/* Stops the process the way Ctrl-Z does in a cooked terminal: restores the
 * terminal, raises SIGTSTP for the process group, and on SIGCONT re-enters,
 * repaints and sends KT_MSG_RESUME. The runtime does the same when SIGTSTP
 * arrives from outside. Nothing stops when SIGTSTP was ignored at startup. */
void kt_cmd_suspend(struct kt_cmds *cmds);

/* Delivers `msg` (copied) after the current update, before further input. For
 * a component reporting to its parent. Pointers in it must outlive delivery. */
void kt_cmd_post(struct kt_cmds *cmds, const struct kt_msg *msg);

/* Forces the next frame to repaint every cell, for when something else wrote
 * to the terminal. */
void kt_cmd_repaint(struct kt_cmds *cmds);

/* ---- view -------------------------------------------------------------- */

enum kt_mouse_mode { KT_MOUSE_OFF, KT_MOUSE_BUTTONS, KT_MOUSE_DRAG, KT_MOUSE_ALL };

/* The frame is cleared to KT_BASE before view runs. Terminal state set here is
 * declarative: the runtime compares it with the previous frame and emits only
 * the changes, and restores everything on exit. */
struct kt_canvas *kt_frame_canvas(struct kt_frame *frame);
int  kt_frame_columns(const struct kt_frame *frame);
int  kt_frame_rows(const struct kt_frame *frame);
/* Show the terminal cursor at a cell this frame; hidden unless called, and
 * when the cell is outside the frame. */
void kt_frame_cursor(struct kt_frame *frame, int x, int y);
/* Per-frame override of the kt_options default. */
void kt_frame_mouse(struct kt_frame *frame, enum kt_mouse_mode mode);
/* Window title while frames ask for one (copied, at most 255 bytes). The
 * terminal's previous title returns when a frame omits it and on exit. */
void kt_frame_title(struct kt_frame *frame, const char *title);

/* ---- program ----------------------------------------------------------- */

struct kt_app {
    /* Optional. Runs once, before the first message. */
    void (*init)(void *model, struct kt_cmds *cmds);
    /* Required. The return value says whether the message was handled. The
     * runtime uses it for one thing: an unhandled KT_MSG_SIGNAL quits with
     * status 128 + signal. Components use it to route input. */
    bool (*update)(void *model, const struct kt_msg *msg, struct kt_cmds *cmds);
    /* Required. Draws the whole frame. Called at most once per frame interval
     * and only after at least one message was processed. */
    void (*view)(const void *model, struct kt_frame *frame);
    /* Maps a cell's semantic style to SGR bytes written to `out`. NULL emits no
     * styling. (Replaced by themes in the style layer.) */
    void (*style)(void *model, FILE *out, enum kt_style style);
};

struct kt_allocator {
    /* realloc semantics: size 0 frees and returns NULL. NULL uses the C library. */
    void *(*resize)(void *context, void *pointer, size_t size);
    void *context;
};

struct kt_options {
    int input_fd, output_fd;        /* default 0 and 1; both must be terminals    */
    bool alt_screen;                /* use the alternate screen                   */
    bool bracketed_paste;           /* deliver pastes as KT_MSG_PASTE             */
    bool focus_events;              /* deliver KT_MSG_FOCUS / KT_MSG_BLUR         */
    enum kt_mouse_mode mouse;       /* default mouse reporting                    */
    bool no_synchronized_output;    /* do not bracket frames with DEC mode 2026   */
    bool ascii;                     /* canvas draws ASCII fallbacks               */
    int fps;                        /* frame cap; 0 = 60                          */
    int escape_ms;                  /* wait before a lone ESC is Escape; 0 = 50   */
    /* Wait before an escape sequence that began but did not finish is given
     * up; 0 = escape_ms. A longer value lets a key split by a slow link
     * arrive whole while Escape stays prompt. What was read of a sequence
     * given up is dropped, and so is the rest of it if it still arrives. An
     * open bracketed paste is never given up. */
    int sequence_ms;
    size_t paste_limit;             /* 0 = 1 MiB; longer pastes are truncated     */
    int max_columns, max_rows;      /* 0 = 512 x 256; larger terminals are clipped */
    struct kt_allocator allocator;
};

/* Returns NULL when memory is unavailable. Nothing touches the terminal until
 * kt_program_run. `app` and `model` must outlive the program. */
struct kt_program *kt_program_new(const struct kt_app *app, void *model, const struct kt_options *options);
/* Runs until kt_cmd_quit or an unhandled signal. Returns the quit status, or
 * -1 when the terminal could not be set up or failed while running (a write
 * error, the input closing, memory running out). The terminal is restored on
 * every return path, at exit() and from a fatal-signal handler. One program
 * runs at a time per process. */
int kt_program_run(struct kt_program *program);
void kt_program_free(struct kt_program *program);

/* Thread-safe and async-signal-safe: queues a message (copied) for update and
 * wakes the loop. Pointers in it must stay valid until it is delivered. Returns
 * false when the program is not running or the message could not be queued
 * (the queue is a pipe; a full pipe refuses instead of blocking). Other
 * threads must stop posting before kt_program_end or kt_program_free. */
bool kt_program_post(struct kt_program *program, const struct kt_msg *msg);

/* ---- owning the loop yourself ------------------------------------------
 * kt_program_run is begin, then next/dispatch/render in a loop, then end. Code
 * that must keep its own loop (a migration in progress, a modal helper) can
 * use them directly; the runtime still owns terminal modes, input decoding,
 * signals and commands. */
/* Enters the terminal, runs init and queues the first KT_MSG_RESIZE. */
bool kt_program_begin(struct kt_program *program);
/* Waits up to timeout_ms (-1 = forever) for the next message. Returns false on
 * timeout. Performs queued commands and due timers while waiting. */
bool kt_program_next(struct kt_program *program, struct kt_msg *msg, int timeout_ms);
/* Calls update for one message, then performs the commands it queued. Returns
 * false once the program should stop; kt_program_status gives the status. */
bool kt_program_dispatch(struct kt_program *program, const struct kt_msg *msg);
/* Calls view and writes the changed cells as one write, now, ignoring the frame cap. */
bool kt_program_render(struct kt_program *program);
int  kt_program_status(const struct kt_program *program);
/* Restores the terminal, stops and reports remaining children, releases
 * everything begin acquired. The program can begin again afterwards. */
void kt_program_end(struct kt_program *program);

/* ---- pumping from inside update -----------------------------------------
 * A helper that update calls and that must not return before it has an answer
 * (a prompt, a confirmation, a wait for a child it started) can run the loop
 * itself with kt_program_next, kt_program_dispatch and kt_program_render,
 * instead of reading the terminal behind the runtime's back:
 *
 *     while (!answered && kt_program_next(program, &msg, -1)) {
 *         if (msg.type == KT_MSG_KEY) answered = on_key(model, &msg.u.event);
 *         else kt_program_dispatch(program, &msg);     // timers, watches, signals
 *         kt_program_render(program);                  // view draws the prompt too
 *     }
 *
 * - Commands queued so far, by this update or an earlier one, are performed
 *   when kt_program_next is entered, so a timer or watch the helper asked for
 *   is live while it waits.
 * - kt_program_dispatch calls update again for the message it is given. The
 *   application decides which messages its helper keeps and which take the
 *   ordinary path, and must leave its model fit for update and view whenever
 *   it pumps. Helpers may nest.
 * - The message the outer update was called with stays valid, pointers
 *   included, until that update returns.
 * - kt_program_next returns false once the program stops (kt_cmd_quit, an
 *   unhandled signal, a failure). The helper must then give up and return:
 *   nothing further arrives, and the loop ends when the outer update returns.
 *   Under the headless driver it also returns false when it would have to
 *   wait for something the test has not supplied. */

/* Lends the terminal to the caller, from inside update: raw mode, the
 * alternate screen and every mode the runtime set are undone, as for a
 * kt_cmd_exec child, and nothing is drawn or read until kt_program_reclaim.
 * For a hand-off that update must finish itself, such as running a foreground
 * child and then waiting for Enter. While the terminal is lent out its keys
 * raise signals again; they reach update as KT_MSG_SIGNAL after the reclaim
 * unless the caller changes their disposition for the duration, and a caller
 * that waits for a child should let the stop key stop both, as kt_cmd_exec
 * does. False when the program is not running or the terminal is already lent. */
bool kt_program_release(struct kt_program *program);
/* Takes the terminal back: raw mode again, unread input discarded, and the
 * next frame enters the screen and repaints every cell. False when the
 * terminal was not lent out or could not be taken back; the program then
 * stops with status -1. */
bool kt_program_reclaim(struct kt_program *program);

#endif
