#ifndef HYDRA_TUI_PROCESS_H
#define HYDRA_TUI_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/* One owner for each bounded child. The deadline includes waiting after EOF. */
struct output_child {
    pid_t pid;
    int fd;
    struct timespec started;
    long budget_ms;
    bool timed_out;
};
/* Caller owns child and buffers. A successful start must be paired with finish.
 * finish closes/reaps the child even after a failed read or output limit. */
int output_start(struct output_child *child, char *const argv[], long budget_ms);
ssize_t output_read(struct output_child *child, char *buffer, size_t size);
int output_finish(struct output_child *child, bool failed);

#endif
