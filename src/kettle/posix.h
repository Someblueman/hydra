#ifndef KT_POSIX_H
#define KT_POSIX_H
/* Optional POSIX adapter. The portable core never includes this header. */
#include <stdbool.h>
#include <termios.h>
struct kt_terminal { struct termios saved; int flags; bool active; };
/* Caller owns struct; begin/end must run on the same thread and terminal.
 * Caller handles signals and must call end before exiting or delegating input. */
bool kt_terminal_begin(struct kt_terminal *terminal);
void kt_terminal_end(struct kt_terminal *terminal);
bool kt_terminal_size(int *columns, int *rows);
#endif
