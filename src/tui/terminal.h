#ifndef HYDRA_TUI_TERMINAL_H
#define HYDRA_TUI_TERMINAL_H
#include <stdbool.h>
/* The stop request shared by the loop and bounded background work. */
void terminal_pipe_signal(void);
bool terminal_stopped(void);
int terminal_exit_status(void);
void terminal_request_stop(int number);

#endif
