#define _POSIX_C_SOURCE 200809L
#include "program_internal.h"
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/* The loop: gather what happened (one wait), turn it into messages, hand them
 * to update one at a time, perform the commands update queued, and draw at
 * most one frame once the gathered messages are used up. Nothing here touches
 * the operating system directly; see struct kt_backend. */

/* ---- memory ------------------------------------------------------------ */

static void *library_resize(void *context, void *pointer, size_t size) {
    (void)context;
    if (!size) { free(pointer); return NULL; }
    return realloc(pointer, size);
}

void *kt_program_allocate(struct kt_program *p, void *pointer, size_t size) {
    return p->options.allocator.resize(p->options.allocator.context, pointer, size);
}

void *kt_program_grow(struct kt_program *p, void *items, size_t *capacity, size_t needed, size_t size) {
    size_t next = *capacity ? *capacity : 8;
    void *grown;
    if (needed <= *capacity) return items;
    while (next < needed) next *= 2;
    grown = kt_program_allocate(p, items, next * size);
    if (grown) *capacity = next;
    return grown;
}

static int bounded(int value, int fallback, int maximum) {
    if (value < 1) return fallback;
    return value > maximum ? maximum : value;
}

static void options_normalize(struct kt_options *o, const struct kt_options *given) {
    memset(o, 0, sizeof(*o));
    if (given) *o = *given;
    if (!o->output_fd) o->output_fd = 1;
    o->fps = bounded(o->fps, 60, 1000);
    o->escape_ms = bounded(o->escape_ms, 50, 10000);
    o->max_columns = bounded(o->max_columns, 512, 4096);
    o->max_rows = bounded(o->max_rows, 256, 4096);
    if (!o->paste_limit) o->paste_limit = 1024U * 1024U;
    if (!o->allocator.resize) { o->allocator.resize = library_resize; o->allocator.context = NULL; }
}

/* ---- message queue ------------------------------------------------------ */

struct kt_msg kt_program_message(uint32_t type, uint64_t tag) {
    struct kt_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type; msg.tag = tag;
    return msg;
}

/* Inserts at `index` messages behind the front; `owned` is freed after the
 * message has been through update. */
static bool queue_insert(struct kt_program *p, size_t index, const struct kt_msg *msg, void *owned) {
    struct kt_queued *items, *slot;
    size_t at;
    if (p->queue_head == p->queue_count) p->queue_head = p->queue_count = 0;
    if (!index && p->queue_head) {
        slot = &p->queue[--p->queue_head];
    } else {
        items = kt_program_grow(p, p->queue, &p->queue_capacity, p->queue_count + 1, sizeof(*items));
        if (!items) { (void)kt_program_allocate(p, owned, 0); return false; }
        p->queue = items;
        at = p->queue_head + index;
        memmove(items + at + 1, items + at, (p->queue_count - at) * sizeof(*items));
        p->queue_count++;
        slot = &items[at];
    }
    slot->msg = *msg; slot->owned = owned;
    return true;
}

bool kt_program_push(struct kt_program *p, const struct kt_msg *msg, void *owned) {
    return queue_insert(p, p->queue_count - p->queue_head, msg, owned);
}

static void held_release(struct kt_program *p) {
    if (p->held) p->held = kt_program_allocate(p, p->held, 0);
}

static bool queue_pop(struct kt_program *p, struct kt_msg *msg) {
    if (p->queue_head == p->queue_count) return false;
    *msg = p->queue[p->queue_head].msg;
    p->held = p->queue[p->queue_head++].owned;
    return true;
}

/* A cancelled timer or watch must not be heard from again, including through
 * a message that was already waiting. */
static void queue_purge(struct kt_program *p, uint64_t id) {
    size_t from, to = p->queue_head;
    for (from = p->queue_head; from < p->queue_count; from++) {
        const struct kt_msg *msg = &p->queue[from].msg;
        bool timer = msg->type == KT_MSG_TIMER && msg->u.timer.id == id;
        bool watch = msg->type == KT_MSG_FD && msg->u.fd.id == id;
        if (!timer && !watch) p->queue[to++] = p->queue[from];
    }
    p->queue_count = to;
}

void kt_program_fail(struct kt_program *p) {
    if (p->running) { p->running = false; p->status = -1; }
}

static uint64_t now(const struct kt_program *p) { return p->backend->now(p->context); }

/* ---- terminal input ----------------------------------------------------- */

/* Whole events only, so a truncated paste never ends inside a character. */
static void paste_append(struct kt_program *p, const char *bytes, size_t length) {
    char *grown;
    if (p->paste_truncated || length > p->options.paste_limit - p->paste_length) {
        p->paste_truncated = true;
        return;
    }
    grown = kt_program_grow(p, p->paste, &p->paste_capacity, p->paste_length + length + 1, 1);
    if (!grown) { p->paste_truncated = true; return; }
    p->paste = grown;
    memcpy(p->paste + p->paste_length, bytes, length);
    p->paste_length += length;
}

static void paste_finish(struct kt_program *p) {
    struct kt_msg msg = kt_program_message(KT_MSG_PASTE, 0);
    if (p->paste) p->paste[p->paste_length] = '\0';
    msg.u.paste.bytes = p->paste ? p->paste : "";
    msg.u.paste.length = p->paste_length;
    msg.u.paste.truncated = p->paste_truncated;
    (void)kt_program_push(p, &msg, p->paste);
    p->paste = NULL; p->paste_length = p->paste_capacity = 0;
    p->pasting = p->paste_truncated = false;
}

static uint32_t key_type(const struct kt_program *p, const struct kt_event *event) {
    if (event->type == KT_MOUSE) return KT_MSG_MOUSE;
    if (!p->options.focus_events || event->length != 3) return KT_MSG_KEY;
    if (!memcmp(event->bytes, "\033[I", 3)) return KT_MSG_FOCUS;
    return memcmp(event->bytes, "\033[O", 3) ? KT_MSG_KEY : KT_MSG_BLUR;
}

static void input_event(struct kt_program *p, const struct kt_event *event) {
    struct kt_msg msg;
    switch (event->type) {
    case KT_PASTE_BEGIN: p->pasting = true; return;
    case KT_PASTE: paste_append(p, event->bytes, event->length); return;
    case KT_PASTE_END: if (p->pasting) paste_finish(p); return;
    default: break;
    }
    msg = kt_program_message(key_type(p, event), 0);
    if (msg.type == KT_MSG_KEY || msg.type == KT_MSG_MOUSE) msg.u.event = *event;
    (void)kt_program_push(p, &msg, NULL);
}

/* Decodes buffered bytes until one message is ready. A sequence left open at
 * the end of the buffer starts the wait that tells a lone Escape apart. */
static bool input_decode(struct kt_program *p) {
    struct kt_event event;
    if (p->input_at == p->input_length) return false;
    while (p->input_at < p->input_length && p->queue_head == p->queue_count)
        if (kt_input_feed(&p->decoder, p->input[p->input_at++], &event)) input_event(p, &event);
    if (p->input_at == p->input_length && p->decoder.length)
        p->escape_at = now(p) + (uint64_t)p->options.escape_ms;
    return true;
}

static void input_read(struct kt_program *p) {
    long count = p->backend->read(p->context, p->input, sizeof(p->input));
    if (count > 0) {
        p->input_at = 0; p->input_length = (size_t)count;
        p->escape_at = KT_NEVER;
    } else if (count == KT_READ_END) p->input_closed = true;
}

static void escape_flush(struct kt_program *p) {
    struct kt_event event;
    if (p->escape_at == KT_NEVER || p->input_at < p->input_length || now(p) < p->escape_at) return;
    p->escape_at = KT_NEVER;
    if (kt_input_flush(&p->decoder, &event)) input_event(p, &event);
}

/* ---- commands ----------------------------------------------------------- */

static struct kt_cmd *command(struct kt_cmds *cmds, enum kt_cmd_type type) {
    struct kt_cmd *items;
    if (!cmds || !cmds->program) return NULL;
    items = kt_program_grow(cmds->program, cmds->items, &cmds->capacity, cmds->count + 1, sizeof(*items));
    if (!items) return NULL;
    cmds->items = items;
    memset(&items[cmds->count], 0, sizeof(*items));
    items[cmds->count].type = type;
    return &items[cmds->count++];
}

static uint64_t command_id(struct kt_cmds *cmds, struct kt_cmd *cmd, uint64_t tag) {
    cmd->id = ++cmds->program->next_id; cmd->tag = tag;
    return cmd->id;
}

void kt_cmd_quit(struct kt_cmds *cmds, int status) {
    struct kt_cmd *cmd = command(cmds, KT_CMD_QUIT);
    if (cmd) cmd->u.status = status;
}

uint64_t kt_cmd_timer(struct kt_cmds *cmds, uint64_t tag, uint64_t delay_ms, uint64_t interval_ms) {
    struct kt_cmd *cmd = command(cmds, KT_CMD_TIMER);
    if (!cmd) return 0;
    cmd->u.timer.delay = delay_ms; cmd->u.timer.interval = interval_ms;
    return command_id(cmds, cmd, tag);
}

uint64_t kt_cmd_watch(struct kt_cmds *cmds, uint64_t tag, int fd, short events) {
    struct kt_cmd *cmd = fd < 0 ? NULL : command(cmds, KT_CMD_WATCH);
    if (!cmd) return 0;
    cmd->u.watch.fd = fd; cmd->u.watch.events = events;
    return command_id(cmds, cmd, tag);
}

static size_t list_count(char *const *list) {
    size_t count = 0;
    while (list && list[count]) count++;
    return count;
}

static size_t list_bytes(char *const *list) {
    size_t bytes = 0, i;
    for (i = 0; list && list[i]; i++) bytes += strlen(list[i]) + 1;
    return bytes;
}

static char **list_copy(char *const *list, char ***slots, char **strings) {
    char **copy = *slots;
    size_t i;
    if (!list) return NULL;
    for (i = 0; list[i]; i++) {
        size_t size = strlen(list[i]) + 1;
        memcpy(*strings, list[i], size);
        copy[i] = *strings; *strings += size;
    }
    copy[i] = NULL; *slots += i + 1;
    return copy;
}

/* The block holds both pointer arrays, then every string. */
void *kt_spawn_copy(const struct kt_allocator *allocator, const struct kt_spawn *from,
                    struct kt_spawn *to) {
    size_t pointers = list_count(from->argv) + list_count(from->envp) + 2;
    size_t directory = from->directory ? strlen(from->directory) + 1 : 0;
    size_t bytes = list_bytes(from->argv) + list_bytes(from->envp) + directory;
    char **block = allocator->resize(allocator->context, NULL, pointers * sizeof(*block) + bytes + 1);
    char **slots = block, *strings;
    if (!block) return NULL;
    strings = (char *)(block + pointers);
    *to = *from;
    to->argv = list_copy(from->argv, &slots, &strings);
    to->envp = list_copy(from->envp, &slots, &strings);
    to->directory = NULL;
    if (directory) { memcpy(strings, from->directory, directory); to->directory = strings; }
    return block;
}

static struct kt_cmd *command_spawn(struct kt_cmds *cmds, enum kt_cmd_type type, const struct kt_spawn *spawn) {
    struct kt_cmd *cmd;
    struct kt_spawn copy;
    void *block;
    if (!cmds || !cmds->program || !spawn || !spawn->argv || !spawn->argv[0]) return NULL;
    block = kt_spawn_copy(&cmds->program->options.allocator, spawn, &copy);
    cmd = block ? command(cmds, type) : NULL;
    if (!cmd) { if (block) (void)kt_program_allocate(cmds->program, block, 0); return NULL; }
    cmd->u.spawn.spec = copy; cmd->u.spawn.block = block;
    return cmd;
}

uint64_t kt_cmd_spawn(struct kt_cmds *cmds, uint64_t tag, const struct kt_spawn *spawn) {
    struct kt_cmd *cmd = command_spawn(cmds, KT_CMD_SPAWN, spawn);
    return cmd ? command_id(cmds, cmd, tag) : 0;
}

void kt_cmd_exec(struct kt_cmds *cmds, uint64_t tag, char *const argv[]) {
    struct kt_spawn spawn;
    struct kt_cmd *cmd;
    memset(&spawn, 0, sizeof(spawn));
    spawn.argv = argv;
    cmd = command_spawn(cmds, KT_CMD_EXEC, &spawn);
    if (cmd) cmd->tag = tag;
}

void kt_cmd_cancel(struct kt_cmds *cmds, uint64_t id) {
    struct kt_cmd *cmd = id ? command(cmds, KT_CMD_CANCEL) : NULL;
    if (cmd) cmd->id = id;
}

void kt_cmd_suspend(struct kt_cmds *cmds) { (void)command(cmds, KT_CMD_SUSPEND); }
void kt_cmd_repaint(struct kt_cmds *cmds) { (void)command(cmds, KT_CMD_REPAINT); }

void kt_cmd_post(struct kt_cmds *cmds, const struct kt_msg *msg) {
    struct kt_cmd *cmd = msg ? command(cmds, KT_CMD_POST) : NULL;
    if (cmd) cmd->u.msg = *msg;
}

/* ---- performing commands ------------------------------------------------ */

static void timer_start(struct kt_program *p, const struct kt_cmd *cmd) {
    struct kt_timer *timers = kt_program_grow(p, p->timers, &p->timer_capacity, p->timer_count + 1, sizeof(*timers));
    struct kt_timer *timer;
    if (!timers) return;
    p->timers = timers;
    timer = &timers[p->timer_count++];
    timer->id = cmd->id; timer->tag = cmd->tag;
    timer->at = now(p) + cmd->u.timer.delay; timer->interval = cmd->u.timer.interval;
}

static void watch_start(struct kt_program *p, const struct kt_cmd *cmd) {
    struct kt_watch *watches = kt_program_grow(p, p->watches, &p->watch_capacity, p->watch_count + 1, sizeof(*watches));
    struct kt_watch *watch;
    if (!watches) return;
    p->watches = watches;
    watch = &watches[p->watch_count++];
    watch->id = cmd->id; watch->tag = cmd->tag;
    watch->fd = cmd->u.watch.fd; watch->events = cmd->u.watch.events;
}

static void timer_remove(struct kt_program *p, size_t index) {
    memmove(p->timers + index, p->timers + index + 1, (--p->timer_count - index) * sizeof(*p->timers));
}

static void watch_remove(struct kt_program *p, size_t index) {
    memmove(p->watches + index, p->watches + index + 1, (--p->watch_count - index) * sizeof(*p->watches));
}

static void cancel(struct kt_program *p, uint64_t id) {
    size_t i;
    if (kt_child_cancel(p, id)) return;
    for (i = 0; i < p->timer_count; i++) if (p->timers[i].id == id) { timer_remove(p, i); break; }
    for (i = 0; i < p->watch_count; i++) if (p->watches[i].id == id) { watch_remove(p, i); break; }
    queue_purge(p, id);
}

/* The terminal goes back to the state it was found in while something else
 * uses it; the next frame enters and repaints from nothing. */
static void terminal_release(struct kt_program *p) {
    kt_frame_leave(p);
    (void)p->backend->raw(p->context, false);
}

static void terminal_reclaim(struct kt_program *p) {
    if (!p->backend->raw(p->context, true)) kt_program_fail(p);
}

static void exec_run(struct kt_program *p, const struct kt_cmd *cmd) {
    struct kt_msg msg = kt_program_message(KT_MSG_EXEC, cmd->tag);
    terminal_release(p);
    msg.u.exec.status = p->backend->exec(p->context, cmd->u.spawn.spec.argv);
    terminal_reclaim(p);
    (void)kt_program_push(p, &msg, NULL);
}

static void suspend(struct kt_program *p) {
    struct kt_msg msg = kt_program_message(KT_MSG_RESUME, 0);
    terminal_release(p);
    p->backend->suspend(p->context);
    terminal_reclaim(p);
    (void)kt_program_push(p, &msg, NULL);
}

static void quit(struct kt_program *p, int status) {
    if (p->running) { p->running = false; p->status = status; }
}

static void perform_one(struct kt_program *p, struct kt_cmd *cmd, size_t *posts) {
    switch (cmd->type) {
    case KT_CMD_QUIT: quit(p, cmd->u.status); break;
    case KT_CMD_TIMER: timer_start(p, cmd); break;
    case KT_CMD_WATCH: watch_start(p, cmd); break;
    case KT_CMD_SPAWN: kt_child_start(p, cmd->id, cmd->tag, &cmd->u.spawn.spec); break;
    case KT_CMD_EXEC: exec_run(p, cmd); break;
    case KT_CMD_CANCEL: cancel(p, cmd->id); break;
    case KT_CMD_SUSPEND: suspend(p); break;
    /* Posts go ahead of everything already waiting, in the order queued. */
    case KT_CMD_POST: (void)queue_insert(p, (*posts)++, &cmd->u.msg, NULL); break;
    case KT_CMD_REPAINT: kt_present_invalidate(&p->presenter); break;
    }
}

static void commands_release(struct kt_program *p) {
    size_t i;
    for (i = 0; i < p->cmds.count; i++) {
        struct kt_cmd *cmd = &p->cmds.items[i];
        if (cmd->type == KT_CMD_SPAWN || cmd->type == KT_CMD_EXEC)
            (void)kt_program_allocate(p, cmd->u.spawn.block, 0);
    }
    p->cmds.count = 0;
}

static void perform(struct kt_program *p) {
    size_t i, posts = 0;
    for (i = 0; i < p->cmds.count; i++) perform_one(p, &p->cmds.items[i], &posts);
    commands_release(p);
}

/* ---- gathering ---------------------------------------------------------- */

static void resized(struct kt_program *p, bool first) {
    struct kt_msg msg = kt_program_message(KT_MSG_RESIZE, 0);
    int columns = p->terminal_columns ? p->terminal_columns : 80;
    int rows = p->terminal_rows ? p->terminal_rows : 24;
    (void)p->backend->size(p->context, &columns, &rows);
    if (!kt_frame_resize(p, columns, rows)) { kt_program_fail(p); return; }
    msg.u.resize.columns = p->canvas.width; msg.u.resize.rows = p->canvas.height;
    if (first) (void)queue_insert(p, 0, &msg, NULL);
    else (void)kt_program_push(p, &msg, NULL);
}

/* A process that was stopped behind the runtime's back finds the terminal in
 * whatever state the shell left it. */
static void continued(struct kt_program *p) {
    struct kt_msg msg = kt_program_message(KT_MSG_RESUME, 0);
    terminal_reclaim(p);
    kt_frame_reassert(p);
    (void)kt_program_push(p, &msg, NULL);
}

static void signals_take(struct kt_program *p, unsigned signals) {
    static const struct { unsigned wake; int number; } reported[] = {
        {KT_WAKE_HANGUP, SIGHUP}, {KT_WAKE_INTERRUPT, SIGINT}, {KT_WAKE_TERMINATE, SIGTERM},
        {KT_WAKE_CHILD, SIGCHLD}
    };
    size_t i;
    if (signals & KT_WAKE_STOP) suspend(p);
    else if (signals & KT_WAKE_CONTINUE) continued(p);
    if (signals & KT_WAKE_RESIZE) resized(p, false);
    for (i = 0; i < sizeof(reported) / sizeof(*reported); i++) {
        struct kt_msg msg = kt_program_message(KT_MSG_SIGNAL, 0);
        if (!(signals & reported[i].wake)) continue;
        msg.u.signal.number = reported[i].number;
        (void)kt_program_push(p, &msg, NULL);
    }
}

static struct kt_timer *timer_due(struct kt_program *p, uint64_t time) {
    struct kt_timer *due = NULL;
    size_t i;
    for (i = 0; i < p->timer_count; i++)
        if (p->timers[i].at <= time && (!due || p->timers[i].at < due->at)) due = &p->timers[i];
    return due;
}

/* Earliest first. A repeating timer that fell behind reports once and
 * resumes on its original grid, so it neither drifts nor floods. */
static void timers_fire(struct kt_program *p) {
    uint64_t time = now(p);
    struct kt_timer *timer;
    while ((timer = timer_due(p, time)) != NULL) {
        struct kt_msg msg = kt_program_message(KT_MSG_TIMER, timer->tag);
        msg.u.timer.id = timer->id; msg.u.timer.now_ms = time;
        if (timer->interval) timer->at += ((time - timer->at) / timer->interval + 1) * timer->interval;
        else timer_remove(p, (size_t)(timer - p->timers));
        (void)kt_program_push(p, &msg, NULL);
    }
}

static void watches_report(struct kt_program *p, const struct pollfd *fds) {
    size_t i;
    for (i = 0; i < p->watch_count; i++) {
        struct kt_msg msg = kt_program_message(KT_MSG_FD, p->watches[i].tag);
        if (!fds[i].revents) continue;
        msg.u.fd.id = p->watches[i].id; msg.u.fd.fd = p->watches[i].fd; msg.u.fd.revents = fds[i].revents;
        (void)kt_program_push(p, &msg, NULL);
    }
    /* A descriptor that is not open would otherwise be reported forever. */
    for (i = p->watch_count; i-- > 0;) if (fds[i].revents & POLLNVAL) watch_remove(p, i);
}

/* fds[0] and fds[1] are the backend's; watches follow, then child pipes. */
static bool poll_prepare(struct kt_program *p, size_t *count) {
    size_t i, at = 2;
    struct pollfd *fds;
    *count = 2 + p->watch_count + p->child_count;
    fds = kt_program_grow(p, p->fds, &p->fd_capacity, *count, sizeof(*fds));
    if (!fds) return false;
    p->fds = fds;
    memset(fds, 0, *count * sizeof(*fds));
    fds[0].fd = fds[1].fd = -1;
    for (i = 0; i < p->watch_count; i++, at++) { fds[at].fd = p->watches[i].fd; fds[at].events = p->watches[i].events; }
    for (i = 0; i < p->child_count; i++, at++) { fds[at].fd = p->children[i].fd; fds[at].events = POLLIN; }
    return true;
}

/* One wait and everything it found, in a fixed order: signals, posted
 * messages, timers, watched descriptors, children, then terminal input. */
static void collect(struct kt_program *p, int timeout) {
    struct kt_ready ready;
    struct kt_msg posted;
    size_t count, watches = p->watch_count;
    ready.input = !p->input_closed; ready.signals = 0;
    if (!poll_prepare(p, &count) || !p->backend->wait(p->context, p->fds, count, timeout, &ready)) {
        kt_program_fail(p);
        return;
    }
    signals_take(p, ready.signals);
    while (p->backend->posted(p->context, &posted)) (void)kt_program_push(p, &posted, NULL);
    timers_fire(p);
    watches_report(p, p->fds + 2);
    kt_children_service(p, p->fds + 2 + watches);
    if (ready.input) input_read(p);
    escape_flush(p);
}

static uint64_t deadline(const struct kt_program *p) {
    uint64_t at = p->escape_at, child = kt_children_deadline(p);
    size_t i;
    for (i = 0; i < p->timer_count; i++) if (p->timers[i].at < at) at = p->timers[i].at;
    return child < at ? child : at;
}

/* Milliseconds from now until `at`, no more than limit (-1 = no limit). */
static int until(const struct kt_program *p, uint64_t at, int limit) {
    uint64_t time, wait;
    if (at == KT_NEVER) return limit;
    time = now(p);
    wait = at > time ? at - time : 0;
    if (wait > (uint64_t)INT_MAX) wait = (uint64_t)INT_MAX;
    return limit >= 0 && (uint64_t)limit < wait ? limit : (int)wait;
}

static bool pending(const struct kt_program *p) {
    return p->queue_head != p->queue_count || p->input_at < p->input_length;
}

/* ---- program ------------------------------------------------------------ */

struct kt_program *kt_program_new_backend(const struct kt_app *app, void *model,
                                          const struct kt_options *options,
                                          const struct kt_backend *backend, void *context) {
    struct kt_options normal;
    struct kt_program *p;
    options_normalize(&normal, options);
    if (!app || !app->update || !app->view || !backend) return NULL;
    p = normal.allocator.resize(normal.allocator.context, NULL, sizeof(*p));
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    p->app = app; p->model = model; p->options = normal;
    p->backend = backend; p->context = context;
    p->cmds.program = p;
    p->frame_interval = (uint64_t)(1000 / normal.fps);
    p->escape_at = KT_NEVER;
    return p;
}

struct kt_program *kt_program_new(const struct kt_app *app, void *model, const struct kt_options *options) {
    struct kt_options normal;
    struct kt_program *p;
    void *context;
    options_normalize(&normal, options);
    context = kt_posix_backend_new(&normal);
    if (!context) return NULL;
    p = kt_program_new_backend(app, model, options, &kt_posix_backend, context);
    if (!p) kt_posix_backend.destroy(context);
    return p;
}

static void state_release(struct kt_program *p) {
    struct kt_msg msg;
    held_release(p);
    while (queue_pop(p, &msg)) held_release(p);
    commands_release(p);
    if (p->out) fclose(p->out);
    free(p->out_bytes);
    p->out = NULL; p->out_bytes = NULL;
    p->queue = kt_program_allocate(p, p->queue, 0);
    p->cmds.items = kt_program_allocate(p, p->cmds.items, 0);
    p->timers = kt_program_allocate(p, p->timers, 0);
    p->watches = kt_program_allocate(p, p->watches, 0);
    p->children = kt_program_allocate(p, p->children, 0);
    p->fds = kt_program_allocate(p, p->fds, 0);
    p->paste = kt_program_allocate(p, p->paste, 0);
    p->cells = kt_program_allocate(p, p->cells, 0);
    p->previous = kt_program_allocate(p, p->previous, 0);
    p->queue_head = p->queue_count = p->queue_capacity = p->cmds.capacity = 0;
    p->timer_count = p->timer_capacity = p->watch_count = p->watch_capacity = 0;
    p->child_count = p->child_capacity = p->fd_capacity = p->cell_capacity = 0;
    p->paste_length = p->paste_capacity = p->input_at = p->input_length = 0;
}

bool kt_program_begin(struct kt_program *p) {
    if (!p || p->begun || !p->backend->begin(p->context)) return false;
    p->begun = p->running = true;
    p->dirty = p->input_closed = p->pasting = p->paste_truncated = false;
    p->status = 0; p->terminal_columns = p->terminal_rows = 0;
    p->escape_at = KT_NEVER; p->frame_at = 0;
    kt_input_init(&p->decoder);
    memset(&p->applied, 0, sizeof(p->applied));
    p->reassert = false;
    p->out = open_memstream(&p->out_bytes, &p->out_size);
    if (!p->out) { kt_program_end(p); return false; }
    if (p->app->init) { p->app->init(p->model, &p->cmds); perform(p); }
    resized(p, true);
    if (p->status < 0) { kt_program_end(p); return false; }
    return true;
}

static bool alive(struct kt_program *p) {
    if (p->running && p->input_closed) kt_program_fail(p);
    return p->running;
}

bool kt_program_next(struct kt_program *p, struct kt_msg *msg, int timeout_ms) {
    uint64_t end = KT_NEVER;
    bool waited = false;
    if (!p || !p->begun || !msg) return false;
    held_release(p);
    if (timeout_ms >= 0) end = now(p) + (uint64_t)timeout_ms;
    while (p->running) {
        int left;
        if (queue_pop(p, msg)) return true;
        if (input_decode(p)) continue;
        left = until(p, end, -1);
        if (!alive(p) || (waited && !left)) break;
        collect(p, until(p, deadline(p), left));
        waited = true;
    }
    return false;
}

static bool quits_unhandled(const struct kt_msg *msg) {
    int number = msg->u.signal.number;
    return msg->type == KT_MSG_SIGNAL && (number == SIGINT || number == SIGTERM || number == SIGHUP);
}

bool kt_program_dispatch(struct kt_program *p, const struct kt_msg *msg) {
    bool handled;
    if (!p || !p->running || !msg) return false;
    handled = p->app->update(p->model, msg, &p->cmds);
    perform(p);
    if (!handled && quits_unhandled(msg)) quit(p, 128 + msg->u.signal.number);
    p->dirty = true;
    return p->running;
}

bool kt_program_render(struct kt_program *p) {
    return p && p->running && kt_frame_render(p);
}

/* A frame is drawn only when the messages of the last wake-up are used up, so
 * a burst of input costs one frame and a busy source cannot starve drawing. */
static void frame_when_due(struct kt_program *p) {
    if (p->running && p->dirty && !pending(p) && now(p) >= p->frame_at) (void)kt_frame_render(p);
}

bool kt_program_turn(struct kt_program *p, int limit_ms) {
    struct kt_msg msg;
    frame_when_due(p);
    if (!kt_program_next(p, &msg, p->dirty ? until(p, p->frame_at, limit_ms) : limit_ms)) return false;
    (void)kt_program_dispatch(p, &msg);
    return true;
}

int kt_program_sleep(struct kt_program *p) {
    int wait = until(p, deadline(p), -1);
    if (pending(p)) return 0;
    return p->dirty ? until(p, p->frame_at, wait) : wait;
}

int kt_program_run(struct kt_program *p) {
    if (!kt_program_begin(p)) return -1;
    while (p->running) (void)kt_program_turn(p, -1);
    kt_program_end(p);
    return p->status;
}

int kt_program_status(const struct kt_program *p) { return p ? p->status : -1; }

/* Every spawn reports exactly once: children stopped here still reach update. */
static void processes_report(struct kt_program *p) {
    struct kt_msg msg;
    while (queue_pop(p, &msg)) {
        if (msg.type == KT_MSG_PROCESS) (void)p->app->update(p->model, &msg, &p->cmds);
        commands_release(p);
        held_release(p);
    }
}

void kt_program_end(struct kt_program *p) {
    if (!p || !p->begun) return;
    p->running = false;
    held_release(p);
    terminal_release(p);
    kt_children_stop(p);
    processes_report(p);
    p->backend->end(p->context);
    state_release(p);
    p->begun = false;
}

void kt_program_free(struct kt_program *p) {
    struct kt_allocator allocator;
    if (!p) return;
    kt_program_end(p);
    if (p->backend->destroy) p->backend->destroy(p->context);
    allocator = p->options.allocator;
    (void)allocator.resize(allocator.context, p, 0);
}

bool kt_program_post(struct kt_program *p, const struct kt_msg *msg) {
    return p && msg && p->backend->post(p->context, msg);
}
