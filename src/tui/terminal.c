#define _POSIX_C_SOURCE 200809L
#include "terminal.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static struct app *active_app;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t stop_signal;

/* Terminal control, rendering, palette actions, and interactive input. */

static void write_terminal(const char *data, size_t length) {
    size_t written = 0U;
    while (written < length) {
        ssize_t result = write(STDOUT_FILENO, data + written, length - written);
        if (result > 0) {
            written += (size_t)result;
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

void restore_terminal(struct app *app) {
    if (app != NULL && app->raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &app->saved);
        app->raw = false;
    }
    if (isatty(STDOUT_FILENO)) {
        static const char reset[] = "\033[?1000l\033[?1006l\033[?2004l\033[0m\033[?25h\n";
        write_terminal(reset, sizeof(reset) - 1U);
    }
}

void cleanup_terminal(void) { restore_terminal(active_app); }

static void signal_handler(int signal_number) {
    stop_signal = signal_number;
    stop_requested = 1;
}

int enter_raw(struct app *app) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &app->saved) != 0) return -1;
    raw = app->saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    app->raw = true;
    {
        static const char modes[] = "\033[?25l\033[?1000h\033[?1006h\033[?2004h";
        write_terminal(modes, sizeof(modes) - 1U);
    }
    return 0;
}

void update_size(struct app *app) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
        app->cols = size.ws_col;
        app->rows = size.ws_row;
    }
}


void terminal_pipe_signal(void) { signal(SIGPIPE, signal_handler); }
void terminal_watch(struct app *app) {
    active_app = app;
    atexit(cleanup_terminal);
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler); signal(SIGHUP, signal_handler);
    signal(SIGPIPE, signal_handler);
}
bool terminal_stopped(void) { return stop_requested != 0; }
int terminal_exit_status(void) { return stop_signal == 0 ? 0 : 128 + stop_signal; }
