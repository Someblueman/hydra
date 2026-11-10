#define _POSIX_C_SOURCE 200809L
#include "program_internal.h"
#include <signal.h>
#include <string.h>

/* kt_cmd_spawn children. Each one moves through: running, optionally
 * stopping (SIGTERM sent, SIGKILL after the grace period), output closed,
 * reaped. The leader is reaped only after its output closed, so its process
 * group id stays reserved for as long as the group may still be signalled.
 * Whatever happens, exactly one KT_MSG_PROCESS is queued. */

#define DEFAULT_OUTPUT (1024U * 1024U)
#define DEFAULT_GRACE_MS 500

static uint64_t now(const struct kt_program *p) { return p->backend->now(p->context); }

static void child_remove(struct kt_program *p, size_t index) {
    memmove(p->children + index, p->children + index + 1,
            (--p->child_count - index) * sizeof(*p->children));
}

static void child_report(struct kt_program *p, struct kt_child *child, int status) {
    struct kt_msg msg = kt_program_message(KT_MSG_PROCESS, child->tag);
    struct kt_process_result *result = &msg.u.process;
    if (child->output) child->output[child->length] = '\0';
    result->id = child->id;
    result->status = status < 0 ? 128 + SIGKILL : status;
    result->timed_out = child->timed_out; result->cancelled = child->cancelled;
    result->truncated = child->truncated; result->failed = child->failed || status < 0;
    result->output = child->output ? child->output : "";
    result->length = child->length;
    (void)kt_program_push(p, &msg, child->output);
    child->output = NULL;
}

void kt_child_start(struct kt_program *p, uint64_t id, uint64_t tag, const struct kt_spawn *spawn) {
    struct kt_child *children = kt_program_grow(p, p->children, &p->child_capacity, p->child_count + 1, sizeof(*children));
    struct kt_child child;
    memset(&child, 0, sizeof(child));
    child.id = id; child.tag = tag; child.fd = -1;
    child.limit = spawn->max_output ? spawn->max_output : DEFAULT_OUTPUT;
    child.grace = (uint64_t)(spawn->grace_ms > 0 ? spawn->grace_ms : DEFAULT_GRACE_MS);
    child.leave = spawn->on_exit == KT_SPAWN_LEAVE;
    child.kill_at = child.abandon_at = KT_NEVER;
    child.budget_at = spawn->budget_ms > 0 ? now(p) + (uint64_t)spawn->budget_ms : KT_NEVER;
    if (!children || !p->backend->spawn(p->context, spawn, id, tag, &child.pid, &child.fd)) {
        if (children) p->children = children;
        child.failed = true;
        child_report(p, &child, 127);
        return;
    }
    p->children = children;
    children[p->child_count++] = child;
}

static void child_close(struct kt_program *p, struct kt_child *child) {
    p->backend->child_close(p->context, child->pid, child->fd);
    child->fd = -1; child->eof = true;
}

/* Output beyond the limit is read and dropped so the child is not blocked. */
static void child_append(struct kt_program *p, struct kt_child *child, const char *bytes, size_t length) {
    size_t room = child->limit - child->length;
    char *grown;
    if (length > room) { length = room; child->truncated = true; }
    if (!length) return;
    grown = kt_program_grow(p, child->output, &child->capacity, child->length + length + 1, 1);
    if (!grown) { child->truncated = child->failed = true; return; }
    child->output = grown;
    memcpy(child->output + child->length, bytes, length);
    child->length += length;
}

static void child_drain(struct kt_program *p, struct kt_child *child) {
    char bytes[8192];
    int chunk;
    for (chunk = 0; chunk < 16 && !child->eof; chunk++) {
        long count = p->backend->child_read(p->context, child->pid, child->fd, bytes, sizeof(bytes));
        if (count > 0) child_append(p, child, bytes, (size_t)count);
        else if (count == KT_READ_NONE) return;
        else {
            if (count != KT_READ_END) child->failed = true;
            child_close(p, child);
        }
    }
}

static void child_stop(struct kt_program *p, struct kt_child *child) {
    if (child->stopping) return;
    child->stopping = true;
    child->budget_at = KT_NEVER;
    child->kill_at = now(p) + child->grace;
    p->backend->child_signal(p->context, child->pid, SIGTERM);
}

static void child_deadlines(struct kt_program *p, struct kt_child *child, uint64_t time) {
    if (time >= child->budget_at) { child->timed_out = true; child_stop(p, child); }
    if (child->stopping && !child->killed && time >= child->kill_at) {
        child->killed = true;
        child->abandon_at = time + child->grace;
        p->backend->child_signal(p->context, child->pid, SIGKILL);
    }
}

/* True when the child is finished and reported. A killed child whose output
 * never closes (a descendant escaped the session) is given up on rather than
 * tracked forever. */
static bool child_service(struct kt_program *p, struct kt_child *child, bool readable, uint64_t time) {
    int status = -1;
    if (readable && !child->eof) child_drain(p, child);
    child_deadlines(p, child, time);
    if (!child->eof && child->killed && time >= child->abandon_at) {
        child->failed = true;
        child_close(p, child);
    }
    if (!child->eof) return false;
    if (!p->backend->child_reap(p->context, child->pid, &status) && time < child->abandon_at) return false;
    child_report(p, child, status);
    return true;
}

void kt_children_service(struct kt_program *p, const struct pollfd *fds) {
    uint64_t time = now(p);
    size_t i = 0, slot;
    for (slot = 0; i < p->child_count; slot++) {
        struct kt_child *child = &p->children[i];
        bool readable = fds && fds[slot].revents;
        if (!readable && p->backend->child_ready) readable = p->backend->child_ready(p->context, child->pid);
        if (child_service(p, child, readable, time)) child_remove(p, i);
        else i++;
    }
}

uint64_t kt_children_deadline(const struct kt_program *p) {
    uint64_t at = KT_NEVER;
    size_t i;
    for (i = 0; i < p->child_count; i++) {
        const struct kt_child *child = &p->children[i];
        uint64_t next = child->killed ? child->abandon_at : child->stopping ? child->kill_at : child->budget_at;
        if (next < at) at = next;
    }
    return at;
}

bool kt_child_cancel(struct kt_program *p, uint64_t id) {
    size_t i;
    for (i = 0; i < p->child_count; i++) {
        if (p->children[i].id != id) continue;
        p->children[i].cancelled = true;
        child_stop(p, &p->children[i]);
        return true;
    }
    return false;
}

/* A child the program was told to leave alone keeps running unobserved. */
static void children_release(struct kt_program *p) {
    size_t i = 0;
    while (i < p->child_count) {
        struct kt_child *child = &p->children[i];
        if (!child->leave) { child->cancelled = true; child_stop(p, child); i++; continue; }
        if (!child->eof) child_close(p, child);
        (void)kt_program_allocate(p, child->output, 0);
        child_remove(p, i);
    }
}

/* Waits for a child pipe or the next stop deadline, a second at most: the
 * program is ending and must not hang on a wake-up that never comes. */
static bool children_wait(struct kt_program *p) {
    struct kt_ready ready = {false, 0};
    struct kt_msg dropped;
    size_t i, count = 2 + p->child_count;
    uint64_t at = kt_children_deadline(p), time = now(p);
    struct pollfd *fds = kt_program_grow(p, p->fds, &p->fd_capacity, count, sizeof(*fds));
    int timeout = at <= time ? 0 : at - time < 1000 ? (int)(at - time) : 1000;
    if (!fds) return false;
    p->fds = fds;
    memset(fds, 0, count * sizeof(*fds));
    fds[0].fd = fds[1].fd = -1;
    for (i = 0; i < p->child_count; i++) { fds[2 + i].fd = p->children[i].fd; fds[2 + i].events = POLLIN; }
    if (!p->backend->wait(p->context, fds, count, timeout, &ready)) return false;
    /* Nothing is delivered any more; posted messages must not keep the wait awake. */
    while (p->backend->posted(p->context, &dropped)) { }
    kt_children_service(p, fds + 2);
    return true;
}

/* Program exit: stop what is still running and wait for it, the same way a
 * cancelled child is stopped while the loop runs. */
void kt_children_stop(struct kt_program *p) {
    children_release(p);
    while (p->child_count && children_wait(p)) { }
    while (p->child_count) {
        (void)kt_program_allocate(p, p->children[p->child_count - 1].output, 0);
        p->child_count--;
    }
}
