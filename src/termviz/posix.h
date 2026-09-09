#ifndef TV_POSIX_H
#define TV_POSIX_H
/* Optional POSIX adapter. The portable core never includes this header. */
#include <stdbool.h>
#include <termios.h>
struct tv_terminal { struct termios saved; int flags; bool active; };
/* Caller owns struct; begin/end must run on the same thread and terminal.
 * Caller handles signals and must call end before exiting or delegating input. */
bool tv_terminal_begin(struct tv_terminal *terminal);
void tv_terminal_end(struct tv_terminal *terminal);
bool tv_terminal_size(int *columns, int *rows);
#endif
