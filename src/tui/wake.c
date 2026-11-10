#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "wake.h"
/* What the loop is waiting for. Background work is polled by its owners
 * (captures, attach clients, setup children); each time they are polled they
 * say here which descriptor or deadline would make polling them again
 * worthwhile. wake_apply turns that into the loop's watches and one timer, so
 * the loop sleeps until there is something to do and no owner needs to know
 * about the loop. Nothing waits here: with no loop the calls only record. */

#define WAKE_WATCHES 48

struct wake_watch { int fd; short events; uint64_t id; bool wanted; };

static struct wake_watch watches[WAKE_WATCHES];
static size_t watch_count;
static uint64_t wanted_at, armed_at, armed_id;

uint64_t wake_now(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

void wake_reset(void) {
    size_t i;
    for (i = 0; i < watch_count; i++) watches[i].wanted = false;
    wanted_at = 0;
}

void wake_fd(int fd, short events) {
    size_t i;
    if (fd < 0 || !events) return;
    for (i = 0; i < watch_count; i++)
        if (watches[i].fd == fd && watches[i].events == events) { watches[i].wanted = true; return; }
    if (watch_count == WAKE_WATCHES) { wake_after(40); return; }
    watches[watch_count++] = (struct wake_watch){fd, events, 0, true};
}

/* Milliseconds from now; the earliest request wins. */
void wake_after(long milliseconds) {
    uint64_t at = wake_now() + (uint64_t)(milliseconds > 0 ? milliseconds : 0);
    if (!wanted_at || at < wanted_at) wanted_at = at;
}

/* A timespec taken from the monotonic clock plus milliseconds, as a wake-up. */
void wake_at(const struct timespec *since, long milliseconds) {
    uint64_t at = (uint64_t)since->tv_sec * 1000U + (uint64_t)since->tv_nsec / 1000000U +
                  (uint64_t)(milliseconds > 0 ? milliseconds : 0);
    uint64_t now = wake_now();
    if (at < now) at = now;
    /* Rounded up, so the owner's own clock agrees the time has come. */
    at++;
    if (!wanted_at || at < wanted_at) wanted_at = at;
}

static void watches_apply(struct kt_cmds *cmds) {
    size_t from, to = 0;
    for (from = 0; from < watch_count; from++) {
        struct wake_watch *w = &watches[from];
        if (!w->wanted) { if (w->id) kt_cmd_cancel(cmds, w->id); continue; }
        if (!w->id) w->id = kt_cmd_watch(cmds, WAKE_TAG, w->fd, w->events);
        watches[to++] = *w;
    }
    watch_count = to;
}

void wake_apply(struct kt_cmds *cmds) {
    uint64_t now = wake_now();
    watches_apply(cmds);
    if (wanted_at == armed_at) return;
    if (armed_id) kt_cmd_cancel(cmds, armed_id);
    armed_id = 0; armed_at = wanted_at;
    if (wanted_at) armed_id = kt_cmd_timer(cmds, WAKE_TAG, wanted_at > now ? wanted_at - now : 0, 0);
}

/* The loop reports what woke it: a fired timer is spent, and a descriptor the
 * loop found closed is no longer watched. */
void wake_timer_fired(uint64_t id) {
    if (id == armed_id) { armed_id = 0; armed_at = 0; }
}

void wake_fd_closed(uint64_t id) {
    size_t i;
    for (i = 0; i < watch_count; i++)
        if (watches[i].id == id) { watches[i] = watches[--watch_count]; return; }
}

void wake_forget(void) {
    watch_count = 0;
    wanted_at = armed_at = armed_id = 0;
}
