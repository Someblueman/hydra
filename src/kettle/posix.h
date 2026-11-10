#ifndef KT_POSIX_H
#define KT_POSIX_H
/* Optional POSIX adapter. The portable core never includes this header.
 * Terminal modes, input and signals belong to the runtime (program.h); this
 * is only for output that is not a program, such as a report sized to the
 * terminal it is printed on. */
#include <stdbool.h>
/* Size of the terminal on standard output, capped at 512 x 256. */
bool kt_terminal_size(int *columns, int *rows);
#endif
