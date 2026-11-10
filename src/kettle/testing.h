#ifndef KT_TESTING_H
#define KT_TESTING_H
/* Headless driver for kt_program applications.
 *
 * Runs an application's init/update/view through the real runtime loop with
 * no terminal: the size is fixed, the clock only moves when the test moves it,
 * input is whatever the test feeds, and kt_cmd_spawn / kt_cmd_exec are
 * recorded instead of started. Everything is deterministic and single
 * threaded, so a test reads as a script:
 *
 *     struct kt_test *t = kt_test_new(&app, &model, 40, 10, NULL);
 *     kt_test_type(t, "jj");
 *     assert(kt_test_contains(t, "> third item"));
 *     kt_test_advance(t, 1000);            // one second of timers
 *     kt_test_exit(t, kt_test_process(t, 0)->id, 0, "done\n");
 *     kt_test_free(t);
 *
 * Calls that feed the program also run it until it has nothing left to do,
 * exactly as one wake-up of the real loop would: every available message
 * goes through update, then at most one frame is drawn.
 *
 * Nothing here ever blocks. An update that pumps the loop itself (a modal
 * helper calling kt_program_next) gets false from a wait for input the test
 * has not fed yet or for time the test has not advanced: queue a helper's
 * whole input with kt_test_feed before the step that opens it. */
#include "program.h"

struct kt_test;

/* Starts the application on a columns x rows terminal: init runs, the first
 * KT_MSG_RESIZE is delivered and the first frame is drawn. `options` may be
 * NULL; its descriptors are ignored. Frames are not rate limited unless
 * options->fps is set. Returns NULL when memory is unavailable. */
struct kt_test *kt_test_new(const struct kt_app *app, void *model, int columns, int rows,
                            const struct kt_options *options);
void kt_test_free(struct kt_test *test);
/* The program underneath, for kt_program_post and the pump functions. */
struct kt_program *kt_test_program(struct kt_test *test);

/* ---- driving ----------------------------------------------------------- */

/* Raw terminal input, through the same decoder as a real terminal: keys,
 * escape sequences, SGR mouse reports, bracketed paste. */
void kt_test_input(struct kt_test *test, const void *bytes, size_t length);
void kt_test_type(struct kt_test *test, const char *text);
/* The same without running the loop, to arrange several things for one
 * wake-up; kt_test_step then runs it. */
void kt_test_feed(struct kt_test *test, const void *bytes, size_t length);
void kt_test_step(struct kt_test *test);
/* Delivers a message, as kt_program_post would. */
void kt_test_send(struct kt_test *test, const struct kt_msg *msg);
void kt_test_resize(struct kt_test *test, int columns, int rows);
/* SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGCHLD, SIGWINCH, SIGTSTP or SIGCONT as
 * the runtime would see it. */
void kt_test_signal(struct kt_test *test, int number);
/* Moves the clock forward, stopping at every deadline on the way (timers,
 * the Escape wait, the frame cap, process budgets) so each fires on time. */
void kt_test_advance(struct kt_test *test, uint64_t milliseconds);

/* ---- observing --------------------------------------------------------- */

bool kt_test_running(const struct kt_test *test);
/* The status kt_program_run would have returned once the program stopped. */
int kt_test_status(const struct kt_test *test);
uint64_t kt_test_now(const struct kt_test *test);
/* How long the real loop would now sleep: milliseconds until its next own
 * deadline, or -1 when only an event can wake it. */
int kt_test_sleep(struct kt_test *test);
/* Text of the frame last drawn, trailing blanks removed. The pointers stay
 * valid until the next call of either function. Rows outside the frame, and
 * every row after the program stopped, are empty. */
const char *kt_test_row(struct kt_test *test, int row);
const char *kt_test_screen(struct kt_test *test);   /* rows joined with '\n' */
bool kt_test_contains(struct kt_test *test, const char *text);
/* Where the terminal cursor is shown; false while it is hidden. */
bool kt_test_cursor(const struct kt_test *test, int *x, int *y);
/* What puts the terminal back as it was found: while the program runs, the
 * bytes it would write if it stopped now; afterwards, the bytes it wrote. */
const char *kt_test_restore(const struct kt_test *test);
/* Frames written so far; each one left the runtime as a single write. */
size_t kt_test_frames(const struct kt_test *test);
/* The bytes of the most recent frame: mode changes, cursor addressing, cells. */
const char *kt_test_output(const struct kt_test *test, size_t *length);

/* ---- processes --------------------------------------------------------- */

/* A kt_cmd_spawn request. Nothing is started: the test finishes it with
 * kt_test_exit. A process the runtime signals (budget, cancel, program exit)
 * ends as that signal would end it: status 128 + signal, no further output. */
struct kt_test_process {
    uint64_t id, tag;
    struct kt_spawn spawn;   /* argv, envp, directory and limits as requested */
    bool running;
    int signal;              /* last signal the runtime sent, 0 for none */
};
size_t kt_test_process_count(const struct kt_test *test);
/* In the order spawned; NULL beyond the end. A record stays valid, and keeps
 * its final state, until kt_test_free. */
const struct kt_test_process *kt_test_process(const struct kt_test *test, size_t index);
/* Ends a running process: it wrote `output` (may be NULL) and exited with
 * `status`. False when `id` is not a running process. */
bool kt_test_exit(struct kt_test *test, uint64_t id, int status, const char *output);

/* kt_cmd_exec requests are recorded and return at once. */
size_t kt_test_exec_count(const struct kt_test *test);
char *const *kt_test_exec(const struct kt_test *test, size_t index);
/* Status reported by KT_MSG_EXEC for the following kt_cmd_exec calls (default 0). */
void kt_test_exec_status(struct kt_test *test, int status);
/* Times the program suspended itself (kt_cmd_suspend or SIGTSTP). */
size_t kt_test_suspends(const struct kt_test *test);

#endif
