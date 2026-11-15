#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

/* Whether, and because of which signal, the control centre should end. The
 * loop records the signals it is told about; bounded work in progress
 * (captures, actions) reads the flag to wind down instead of waiting out its
 * budget. Deterministic rendering has no loop and catches a closed pipe here. */
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t stop_signal;

static void signal_handler(int signal_number) {
    stop_signal = signal_number;
    stop_requested = 1;
}

void terminal_pipe_signal(void) { signal(SIGPIPE, signal_handler); }
bool terminal_stopped(void) { return stop_requested != 0; }
int terminal_exit_status(void) { return stop_signal == 0 ? 0 : 128 + stop_signal; }
void terminal_request_stop(int number) { signal_handler(number); }
