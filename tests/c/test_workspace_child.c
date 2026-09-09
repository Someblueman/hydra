#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Controlled real PTY client: exercise alternate screen, cursor, styles and
 * actual input/resize. It does not use the termviz implementation. */
int main(void) {
    struct termios original, raw;
    struct winsize size;
    char key;
    if (tcgetattr(STDIN_FILENO, &original) != 0) return 2;
    raw = original; raw.c_lflag &= (tcflag_t)~(ICANON | ECHO); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return 2;
    printf("\033[?1049h\033[2J\033[H\033[38;2;40;180;220mALT_READY\033[0m\033[2;1Hcursor and styles\033[?1h\033[?2004h");
    fflush(stdout);
    while (read(STDIN_FILENO, &key, 1) == 1) {
        if (key == 'q') break;
        if (key == 'r' && ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0) {
            printf("\033[3;1H\033[2KSIZE:%u:%u", size.ws_row, size.ws_col); fflush(stdout);
        }
        if (key == 'x') {
            printf("\033]52;c;SHOULD_NOT_ESCAPE\007\033PIGNORED_PAYLOAD\033\\\033[4;1HSAFE_OUTPUT"); fflush(stdout);
        }
    }
    printf("\033[?1l\033[?2004l\033[?1049lAFTER_ALT\r\n"); fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    return 7;
}
