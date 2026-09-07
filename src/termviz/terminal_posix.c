#define _POSIX_C_SOURCE 200809L
#include "posix.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

bool tv_terminal_begin(struct tv_terminal *t) {
    struct termios raw;
    t->active = false;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &t->saved) != 0) return false;
    t->flags = fcntl(STDIN_FILENO, F_GETFL);
    if (t->flags < 0) return false;
    raw = t->saved;
    raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= (tcflag_t)~(CSIZE | PARENB); raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    t->active = true;
    if (fputs("\033[?1049h\033[?25l\033[?1002h\033[?1006h\033[?2004h", stdout) == EOF || fflush(stdout) != 0) {
        tv_terminal_end(t); return false;
    }
    return true;
}

void tv_terminal_end(struct tv_terminal *t) {
    if (!t->active) return;
    (void)fputs("\033[0m\033[?2004l\033[?1006l\033[?1002l\033[?25h\033[?1049l", stdout);
    (void)fflush(stdout);
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &t->saved);
    (void)fcntl(STDIN_FILENO, F_SETFL, t->flags);
    t->active = false;
}

bool tv_terminal_size(int *cols, int *rows) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || !size.ws_col || !size.ws_row) return false;
    *cols = size.ws_col > 512 ? 512 : size.ws_col;
    *rows = size.ws_row > 256 ? 256 : size.ws_row;
    return true;
}
