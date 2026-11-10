#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
#include "wake.h"
#include <poll.h>
/* The interactive control centre as a kettle program. The runtime owns the
 * terminal, input decoding, signals and output; this file routes its messages:
 * input to the handlers (input.c), everything else to the background pollers,
 * which then say what they are waiting for (wake.c). Nothing here runs on a
 * tick: with no work in flight the only wake-up is the refresh timer.
 *
 * Helpers that wait inside a key handler (prompts, confirmations, captured
 * actions) stay synchronous by running the loop themselves through
 * modal_wait; view draws them over the frame. */

enum { TIMER_REFRESH = 1, TIMER_CLOCK };
#define MSG_CLOCK (KT_MSG_USER + 1U)
#define MODAL_FRAME_MS 16U

/* ---- background work ---------------------------------------------------- */

/* While a captured action runs, the handler that started it holds what it
 * read from the model, so observations wait for it; agent panes do not. */
static void poll_work(struct app *app, bool request) {
    size_t i;
    if (!app->action_depth) {
        native_observations_tick(app, request);
        if (request && app->preview && !native_terminal_focused(app)) capture_preview(app);
        native_setup_tick(app);
    }
    native_terminals_pump(app);
    for (i = 0; i < app->modal_count; i++)
        if (app->modals[i].tick) app->modals[i].tick(app, app->modals[i].context);
}

/* Polls everything once and makes the loop wait for what the pollers named.
 * A poller can open a helper that polls again from inside; the round it
 * interrupted is then repeated, so nothing it declared is lost. */
void loop_service(struct app *app, bool request) {
    unsigned serial;
    do {
        serial = ++app->service_serial;
        wake_reset();
        poll_work(app, request);
        request = false;
    } while (serial != app->service_serial);
    if (app->cmds) wake_apply(app->cmds);
}

/* ---- messages ----------------------------------------------------------- */

static bool clock_seen;
void clock_mark(void) { clock_seen = true; }

static bool on_timer(struct app *app, const struct kt_msg *msg, struct kt_cmds *cmds) {
    if (msg->tag == WAKE_TAG) wake_timer_fired(msg->u.timer.id);
    /* Ages on screen advance once a second, and only while one is shown. */
    if (msg->tag == TIMER_CLOCK && !app->clock_drawn) {
        kt_cmd_cancel(cmds, app->clock_timer);
        app->clock_timer = 0; app->clock_armed = false;
    }
    return msg->tag == TIMER_REFRESH;
}

static void on_signal(int number) {
    if (number == SIGINT || number == SIGTERM || number == SIGHUP || number == SIGQUIT) terminal_request_stop(number);
}

/* What every message ends with: a theme change repaints each cell (its
 * colours changed, its text did not), and a quit ends the loop. */
static void settle(struct app *app, struct kt_cmds *cmds) {
    if (app->frame_theme != app->theme) { app->frame_theme = app->theme; kt_cmd_repaint(cmds); }
    if (!app->running || terminal_stopped()) kt_cmd_quit(cmds, terminal_exit_status());
}

static bool loop_update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct app *app = model;
    bool request = false;
    app->cmds = cmds;
    switch (msg->type) {
    case KT_MSG_RESIZE: app->cols = msg->u.resize.columns; app->rows = msg->u.resize.rows; break;
    case KT_MSG_KEY: case KT_MSG_MOUSE: input_event(app, &msg->u.event); break;
    case KT_MSG_PASTE: input_paste(app, msg->u.paste.bytes, msg->u.paste.length, msg->u.paste.truncated); break;
    case KT_MSG_TIMER: request = on_timer(app, msg, cmds); break;
    case KT_MSG_FD: if (msg->u.fd.revents & POLLNVAL) wake_fd_closed(msg->u.fd.id); break;
    case KT_MSG_SIGNAL: on_signal(msg->u.signal.number); break;
    case MSG_CLOCK: if (!app->clock_timer) app->clock_timer = kt_cmd_timer(cmds, TIMER_CLOCK, 1000, 1000); break;
    default: break;
    }
    loop_service(app, request);
    settle(app, cmds);
    return true;
}

static void loop_init(void *model, struct kt_cmds *cmds) {
    struct app *app = model;
    app->cmds = cmds;
    app->started = app->running = true;
    app->frame_theme = app->theme;
    (void)kt_cmd_timer(cmds, TIMER_REFRESH, (uint64_t)HYDRA_TUI_REFRESH_SECONDS * 1000U,
                       (uint64_t)HYDRA_TUI_REFRESH_SECONDS * 1000U);
    loop_service(app, true);
}

/* Drawing also records where things are (rows for the mouse, pane sizes for
 * attach clients), which is why the model is written to here. */
static void loop_view(const void *model, struct kt_frame *frame) {
    struct app *app = (struct app *)model;
    struct kt_msg clock;
    size_t i;
    app->target = kt_frame_canvas(frame);
    app->cols = app->target->width; app->rows = app->target->height;
    memset(&app->frame, 0, sizeof(app->frame));
    clock_seen = false;
    render(app, 0U, false);
    app->clock_drawn = clock_seen;
    for (i = 0; app->frame.cells && i < app->modal_count; i++)
        if (app->modals[i].draw) app->modals[i].draw(app, app->modals[i].context);
    app->target = NULL;
    if (!app->clock_drawn || app->clock_armed || !app->program) return;
    memset(&clock, 0, sizeof(clock));
    clock.type = MSG_CLOCK;
    app->clock_armed = kt_program_post(app->program, &clock);
}

const struct kt_app tui_app = {loop_init, loop_update, loop_view, dashboard_style};

/* ---- helpers that wait inside a handler --------------------------------- */

bool modal_push(struct app *app, const struct modal *modal) {
    if (app->modal_count == MODAL_DEPTH) return false;
    app->modals[app->modal_count++] = *modal;
    app->modal_dirty = true;
    return true;
}

void modal_pop(struct app *app) {
    if (app->modal_count) app->modal_count--;
    app->modal_dirty = true;
}

/* Draws the frame when something changed, no more often than the display
 * can show; *timeout is how long to sleep before it is due. False once the
 * program has stopped. */
static bool modal_frame(struct app *app, int *timeout) {
    unsigned long long now = wake_now();
    *timeout = -1;
    if (!app->modal_dirty) return true;
    if (now < app->modal_frame_at) { *timeout = (int)(app->modal_frame_at - now); return true; }
    if (!kt_program_render(app->program)) return false;
    app->modal_frame_at = now + MODAL_FRAME_MS;
    app->modal_dirty = false;
    return true;
}

/* Runs the loop until there is something for the waiting helper. MODAL_INPUT:
 * msg is a key, mouse or paste message, the helper's to use or drop.
 * MODAL_WOKE: other messages took their ordinary path, background work
 * included. MODAL_STOPPED: the program is ending, or there is no loop; the
 * helper must give up. */
enum modal_event modal_wait(struct app *app, struct kt_msg *msg) {
    struct kt_program *program = app->program;
    int timeout;
    if (!program || !app->running || terminal_stopped()) return MODAL_STOPPED;
    if (!kt_program_next(program, msg, 0)) {
        loop_service(app, false);
        if (!modal_frame(app, &timeout)) return MODAL_STOPPED;
        if (!kt_program_next(program, msg, timeout)) return timeout < 0 ? MODAL_STOPPED : MODAL_WOKE;
    }
    app->modal_dirty = true;
    if (msg->type == KT_MSG_KEY || msg->type == KT_MSG_MOUSE || msg->type == KT_MSG_PASTE) return MODAL_INPUT;
    (void)kt_program_dispatch(program, msg);
    return app->running && !terminal_stopped() ? MODAL_WOKE : MODAL_STOPPED;
}

/* ---- lending the terminal ----------------------------------------------- */

static void disposition(int number, void (*handler)(int), struct sigaction *previous) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(number, &action, previous);
}

/* Gives the terminal, as it was found, to a foreground child of the caller.
 * The terminal's stop key stops Hydra with the child, so the shell sees its
 * job stop. With shield the interrupt and quit keys reach only the child. */
bool terminal_lend(struct app *app, struct terminal_loan *loan, bool shield) {
    struct sigaction current;
    memset(loan, 0, sizeof(*loan));
    if (!app->program || !kt_program_release(app->program)) return false;
    loan->lent = true; loan->shield = shield;
    if (sigaction(SIGTSTP, NULL, &current) == 0 && current.sa_handler != SIG_IGN) {
        disposition(SIGTSTP, SIG_DFL, &loan->stop);
        loan->stop_changed = true;
    }
    if (shield) {
        disposition(SIGINT, SIG_IGN, &loan->interrupt);
        disposition(SIGQUIT, SIG_IGN, &loan->quit);
    }
    return true;
}

/* Takes the terminal back; the next frame repaints all of it. False when the
 * terminal could not be taken back: the control centre then ends. */
bool terminal_return(struct app *app, struct terminal_loan *loan) {
    if (!loan->lent) return false;
    loan->lent = false;
    if (loan->shield) {
        (void)sigaction(SIGINT, &loan->interrupt, NULL);
        (void)sigaction(SIGQUIT, &loan->quit, NULL);
    }
    if (loan->stop_changed) (void)sigaction(SIGTSTP, &loan->stop, NULL);
    if (app->program && kt_program_reclaim(app->program)) return true;
    app->running = false;
    return false;
}

/* ---- entry -------------------------------------------------------------- */

static bool terminal_ready(struct app *app) {
    const char *term = getenv("TERM");
    struct winsize size;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("hydra-tui requires interactive stdin/stdout; use --headless-fixture for deterministic rendering\n", stderr);
        return false;
    }
    if (term == NULL || strcmp(term, "dumb") == 0) {
        fputs("hydra-tui cannot use TERM=dumb; use hydra list and hydra switch in this terminal\n", stderr);
        return false;
    }
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
        app->cols = size.ws_col; app->rows = size.ws_row;
    }
    if (app->cols < 40 || app->rows < 10) {
        fputs("hydra-tui requires at least 40 columns by 10 rows; enlarge the terminal, or use hydra list and hydra switch\n", stderr);
        return false;
    }
    return true;
}

/* A keys file that was not applied as written is the first thing shown. */
static void startup_notice(struct app *app) {
    const char *problem = native_keys(app)->problem;
    copy_text(app->notice, sizeof(app->notice), problem[0] ? problem : "Loading snapshot...");
}

/* Escape acts after 20 ms; a key split by a slow link has 150 ms to arrive
 * whole. Mouse reports cover presses, releases and drags. */
void tui_options(const struct app *app, struct kt_options *options) {
    memset(options, 0, sizeof(*options));
    options->alt_screen = options->bracketed_paste = true;
    options->mouse = KT_MOUSE_DRAG;
    options->ascii = app->ascii;
    options->escape_ms = 20; options->sequence_ms = 150;
    options->paste_limit = 4U * 1024U * 1024U;
}

/* The signals that end the control centre end it also when it was started
 * with them ignored (under nohup, as a background job): the runtime leaves an
 * ignored signal ignored, so they are given their default first. restore puts
 * back what was found, for the clean-up that follows the loop. */
static const int ending_signals[] = {SIGINT, SIGTERM, SIGHUP};
#define ENDING_SIGNALS (sizeof(ending_signals) / sizeof(ending_signals[0]))

static void signals_heed(struct sigaction found[ENDING_SIGNALS], bool restore) {
    size_t i;
    for (i = 0; i < ENDING_SIGNALS; i++) {
        if (restore) { (void)sigaction(ending_signals[i], &found[i], NULL); continue; }
        if (sigaction(ending_signals[i], NULL, &found[i]) != 0) continue;
        if (found[i].sa_handler == SIG_IGN) (void)signal(ending_signals[i], SIG_DFL);
    }
}

/* 0 on quit and 128 + signal after a signal. 3: no usable terminal. 4: the
 * terminal could not be put into raw mode. A terminal that went away while
 * running ends like the hangup it is. */
int interactive_main(struct app *app) {
    struct sigaction found[ENDING_SIGNALS];
    struct kt_options options;
    struct kt_program *program;
    int status;
    if (!terminal_ready(app)) return 3;
    startup_notice(app);
    if (tcgetattr(STDIN_FILENO, &app->saved) != 0) return 4;
    tui_options(app, &options);
    program = kt_program_new(&tui_app, app, &options);
    if (!program) return 4;
    app->program = program;
    memset(found, 0, sizeof(found));
    signals_heed(found, false);
    status = kt_program_run(program);
    signals_heed(found, true);
    app->program = NULL; app->cmds = NULL;
    app->modal_count = 0;
    wake_forget();
    kt_program_free(program);
    if (status >= 0) return status;
    if (!app->started) return 4;
    return terminal_stopped() ? terminal_exit_status() : 128 + SIGHUP;
}
