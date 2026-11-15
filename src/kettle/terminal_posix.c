#define _POSIX_C_SOURCE 200809L
#include "posix.h"
#include <sys/ioctl.h>
#include <unistd.h>

bool kt_terminal_size(int *cols, int *rows) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || !size.ws_col || !size.ws_row) return false;
    *cols = size.ws_col > 512 ? 512 : size.ws_col;
    *rows = size.ws_row > 256 ? 256 : size.ws_row;
    return true;
}
