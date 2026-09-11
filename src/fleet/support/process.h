#ifndef HYDRA_FLEET_SUPPORT_PROCESS_H
#define HYDRA_FLEET_SUPPORT_PROCESS_H
#include "fleet/fleet.h"

/* in_bytes is the child stdin file offset observed after wait, not the bytes
 * staged by the parent; input_complete is true only when the child consumed
 * the complete request. measurement_complete also requires complete output
 * capture; these are transport process stdio counts, never SSH wire bytes. */
struct f_capture { char *out, *err; size_t in_bytes, out_bytes, err_bytes; int status; bool input_complete, measurement_complete, timeout, cancelled, stop_unknown; };
/* Borrowed log descriptors and stop context; remaining budgets span invocations. */
struct f_control {
    bool (*stop)(void *context);
    void (*observe)(void *context, const char *stdout_text, size_t size);
    void *context;
    unsigned grace_seconds;
    int log_fd[2];
    size_t remaining[2];
    bool truncated, log_error, stop_unknown;
};
extern volatile sig_atomic_t f_stopped;
/* argv/input/control are borrowed. Run initializes caller-owned cap; release its
 * buffers with f_capture_free, including after failure. Free the f_quote result. */
void f_capture_free(struct f_capture *cap);
int f_run(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap);
int f_run_controlled(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap, struct f_control *control);
char *f_quote(const char *value);
#endif
