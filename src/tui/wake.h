#ifndef HYDRA_TUI_WAKE_H
#define HYDRA_TUI_WAKE_H
#include "../kettle/program.h"
#include <time.h>
/* The loop's wait set, declared by whoever has work in flight (wake.c). */
#define WAKE_TAG UINT64_C(0x77616b65)
uint64_t wake_now(void);
/* Starts a round of declarations; what is not declared again is dropped. */
void wake_reset(void);
/* Poll again when fd has any of events (poll(2) flags). */
void wake_fd(int fd, short events);
/* Poll again after milliseconds, or milliseconds after a monotonic time. */
void wake_after(long milliseconds);
void wake_at(const struct timespec *since, long milliseconds);
/* Makes the loop wait for exactly what was declared since wake_reset. */
void wake_apply(struct kt_cmds *cmds);
void wake_timer_fired(uint64_t id);
void wake_fd_closed(uint64_t id);
/* The loop ended: its watches and timer are gone with it. */
void wake_forget(void);
#endif
