#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "testing.h"
#include "program_internal.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/* The headless backend: the same loop as a real terminal, with a clock the
 * test advances, input the test feeds and processes the test finishes. Only
 * watched descriptors are real; they are polled without waiting. */

struct fake_process {
    struct kt_test_process info;
    void *block;
    char *output;
    size_t length, taken;
    int status;
    bool closed;
};
struct fake_exec { struct kt_spawn spawn; void *block; };

struct kt_test {
    struct kt_program *program;
    struct kt_allocator allocator;
    uint64_t now;
    int columns, rows, exec_status;
    unsigned signals;
    unsigned char *input;
    size_t input_length, input_capacity;
    struct kt_msg *posts;
    size_t post_at, post_count, post_capacity;
    char *frame;
    size_t frame_length, frame_capacity, frames, suspends;
    struct fake_process **processes;   /* each allocated alone: records never move */
    size_t process_count, process_capacity;
    struct fake_exec *execs;
    size_t exec_count, exec_capacity;
    char *text;
    char restore[KT_LEAVE_MAX + 1], left[KT_LEAVE_MAX + 1];
};

static void *library_resize(void *context, void *pointer, size_t size) {
    (void)context;
    if (!size) { free(pointer); return NULL; }
    return realloc(pointer, size);
}

/* Room for one more item, or NULL. */
static void *grown(void *items, size_t count, size_t *capacity, size_t size) {
    size_t next = *capacity ? *capacity * 2 : 8;
    void *larger;
    if (count < *capacity) return items;
    larger = realloc(items, next * size);
    if (larger) *capacity = next;
    return larger;
}

/* ---- backend ------------------------------------------------------------ */

static uint64_t test_now(void *context) { return ((struct kt_test *)context)->now; }
static bool test_begin(void *context) { (void)context; return true; }
static void test_end(void *context) { (void)context; }
static bool test_raw(void *context, bool enable) { (void)context; (void)enable; return true; }
static void test_restore(void *context, const char *bytes, size_t length) {
    struct kt_test *t = context;
    memcpy(t->restore, bytes, length);
    t->restore[length] = '\0';
}

static void test_leave(void *context) {
    struct kt_test *t = context;
    memcpy(t->left, t->restore, sizeof(t->left));
    t->restore[0] = '\0';
}

static bool test_size(void *context, int *columns, int *rows) {
    struct kt_test *t = context;
    *columns = t->columns; *rows = t->rows;
    return true;
}

static bool test_write(void *context, const char *bytes, size_t length) {
    struct kt_test *t = context;
    if (length + 1 > t->frame_capacity) {
        char *frame = realloc(t->frame, length + 1);
        if (!frame) return false;
        t->frame = frame; t->frame_capacity = length + 1;
    }
    memcpy(t->frame, bytes, length);
    t->frame[length] = '\0';
    t->frame_length = length;
    t->frames++;
    return true;
}

static long test_read(void *context, void *buffer, size_t capacity) {
    struct kt_test *t = context;
    size_t count = t->input_length < capacity ? t->input_length : capacity;
    if (!count) return KT_READ_NONE;
    memcpy(buffer, t->input, count);
    memmove(t->input, t->input + count, t->input_length - count);
    t->input_length -= count;
    return (long)count;
}

static bool test_wait(void *context, struct pollfd *fds, size_t count, int timeout_ms,
                      struct kt_ready *ready) {
    struct kt_test *t = context;
    size_t i;
    (void)timeout_ms;
    fds[0].fd = fds[1].fd = -1;
    if (count > 2 && poll(fds, (nfds_t)count, 0) < 0)
        for (i = 0; i < count; i++) fds[i].revents = 0;
    ready->input = ready->input && t->input_length > 0;
    ready->signals = t->signals;
    t->signals = 0;
    return true;
}

static bool test_post(void *context, const struct kt_msg *msg) {
    struct kt_test *t = context;
    struct kt_msg *posts = grown(t->posts, t->post_count, &t->post_capacity, sizeof(*posts));
    if (!posts) return false;
    t->posts = posts;
    posts[t->post_count++] = *msg;
    return true;
}

static bool test_posted(void *context, struct kt_msg *msg) {
    struct kt_test *t = context;
    if (t->post_at == t->post_count) { t->post_at = t->post_count = 0; return false; }
    *msg = t->posts[t->post_at++];
    return true;
}

static struct fake_process *process_of(struct kt_test *t, pid_t pid) {
    return pid >= 1 && (size_t)pid <= t->process_count ? t->processes[pid - 1] : NULL;
}

static bool test_spawn(void *context, const struct kt_spawn *spawn, uint64_t id, uint64_t tag,
                       pid_t *pid, int *fd) {
    struct kt_test *t = context;
    struct fake_process **processes = grown(t->processes, t->process_count, &t->process_capacity, sizeof(*processes));
    struct fake_process *process = calloc(1, sizeof(*process));
    if (processes) t->processes = processes;
    if (process) process->block = kt_spawn_copy(&t->allocator, spawn, &process->info.spawn);
    if (!processes || !process || !process->block) { free(process); return false; }
    process->info.id = id; process->info.tag = tag; process->info.running = true;
    processes[t->process_count] = process;
    *pid = (pid_t)++t->process_count; *fd = -1;
    return true;
}

static bool test_child_ready(void *context, pid_t pid) {
    const struct fake_process *process = process_of(context, pid);
    return process && !process->info.running && !process->closed;
}

static long test_child_read(void *context, pid_t pid, int fd, void *buffer, size_t capacity) {
    struct fake_process *process = process_of(context, pid);
    size_t count;
    (void)fd;
    if (!process || process->info.running) return KT_READ_NONE;
    count = process->length - process->taken;
    if (count > capacity) count = capacity;
    if (!count) return KT_READ_END;
    memcpy(buffer, process->output + process->taken, count);
    process->taken += count;
    return (long)count;
}

static void test_child_signal(void *context, pid_t pid, int signal_number) {
    struct fake_process *process = process_of(context, pid);
    if (!process) return;
    process->info.signal = signal_number;
    if (process->info.running) { process->info.running = false; process->status = 128 + signal_number; }
}

static bool test_child_reap(void *context, pid_t pid, int *status) {
    const struct fake_process *process = process_of(context, pid);
    if (!process || process->info.running) return false;
    *status = process->status;
    return true;
}

static void test_child_close(void *context, pid_t pid, int fd) {
    struct fake_process *process = process_of(context, pid);
    (void)fd;
    if (process) process->closed = true;
}

static int test_exec(void *context, char *const argv[]) {
    struct kt_test *t = context;
    struct fake_exec *execs = grown(t->execs, t->exec_count, &t->exec_capacity, sizeof(*execs));
    struct kt_spawn spawn;
    if (!execs) return -1;
    t->execs = execs;
    memset(&spawn, 0, sizeof(spawn));
    spawn.argv = argv;
    execs[t->exec_count].block = kt_spawn_copy(&t->allocator, &spawn, &execs[t->exec_count].spawn);
    if (!execs[t->exec_count].block) return -1;
    t->exec_count++;
    return t->exec_status;
}

static void test_suspend(void *context) { ((struct kt_test *)context)->suspends++; }

static const struct kt_backend test_backend = {
    test_now, test_begin, test_end, test_raw, test_size, test_write, test_read,
    test_wait, test_restore, test_leave, test_post, test_posted, test_spawn, test_child_ready,
    test_child_read, test_child_signal, test_child_reap, test_child_close,
    test_exec, test_suspend, NULL
};

/* ---- lifetime ----------------------------------------------------------- */

struct kt_test *kt_test_new(const struct kt_app *app, void *model, int columns, int rows,
                            const struct kt_options *options) {
    struct kt_test *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->allocator.resize = library_resize;
    t->now = 1000; t->columns = columns; t->rows = rows;
    t->program = kt_program_new_backend(app, model, options, &test_backend, t);
    if (t->program && !(options && options->fps > 0)) t->program->frame_interval = 0;
    if (!t->program || !kt_program_begin(t->program)) { kt_test_free(t); return NULL; }
    kt_test_step(t);
    return t;
}

void kt_test_free(struct kt_test *t) {
    size_t i;
    if (!t) return;
    kt_program_free(t->program);
    for (i = 0; i < t->process_count; i++) {
        free(t->processes[i]->block); free(t->processes[i]->output);
        free(t->processes[i]);
    }
    for (i = 0; i < t->exec_count; i++) free(t->execs[i].block);
    free(t->processes); free(t->execs); free(t->posts);
    free(t->input); free(t->frame); free(t->text);
    free(t);
}

struct kt_program *kt_test_program(struct kt_test *t) { return t->program; }

/* ---- driving ------------------------------------------------------------ */

/* A program that never settles (a watched descriptor nobody reads) must not
 * hang its test. */
#define STEP_TURNS 4096

void kt_test_step(struct kt_test *t) {
    int turns;
    for (turns = 0; turns < STEP_TURNS && kt_program_turn(t->program, 0); turns++) { }
    if (!t->program->running) kt_program_end(t->program);
}

void kt_test_feed(struct kt_test *t, const void *bytes, size_t length) {
    unsigned char *input = t->input;
    if (t->input_length + length > t->input_capacity) {
        input = realloc(t->input, t->input_length + length);
        if (!input) return;
        t->input = input; t->input_capacity = t->input_length + length;
    }
    if (length) memcpy(input + t->input_length, bytes, length);
    t->input_length += length;
}

void kt_test_input(struct kt_test *t, const void *bytes, size_t length) {
    kt_test_feed(t, bytes, length);
    kt_test_step(t);
}

void kt_test_type(struct kt_test *t, const char *text) { kt_test_input(t, text, strlen(text)); }

void kt_test_send(struct kt_test *t, const struct kt_msg *msg) {
    (void)kt_program_post(t->program, msg);
    kt_test_step(t);
}

void kt_test_resize(struct kt_test *t, int columns, int rows) {
    t->columns = columns; t->rows = rows;
    t->signals |= KT_WAKE_RESIZE;
    kt_test_step(t);
}

void kt_test_signal(struct kt_test *t, int number) {
    static const struct { int number; unsigned wake; } known[] = {
        {SIGINT, KT_WAKE_INTERRUPT}, {SIGTERM, KT_WAKE_TERMINATE}, {SIGHUP, KT_WAKE_HANGUP},
        {SIGCHLD, KT_WAKE_CHILD}, {SIGCONT, KT_WAKE_CONTINUE}, {SIGTSTP, KT_WAKE_STOP},
        {SIGWINCH, KT_WAKE_RESIZE}
    };
    size_t i;
    for (i = 0; i < sizeof(known) / sizeof(*known); i++)
        if (known[i].number == number) t->signals |= known[i].wake;
    kt_test_step(t);
}

void kt_test_advance(struct kt_test *t, uint64_t milliseconds) {
    uint64_t end = t->now + milliseconds;
    int hops;
    for (hops = 0; hops < 1000000 && t->program->running; hops++) {
        int sleep = kt_program_sleep(t->program);
        if (sleep < 0 || t->now + (uint64_t)sleep > end) break;
        /* A deadline that stays due would pin the clock; move on regardless. */
        t->now += sleep ? (uint64_t)sleep : 1;
        if (t->now > end) t->now = end;
        kt_test_step(t);
    }
    t->now = end;
    kt_test_step(t);
}

/* ---- observing ---------------------------------------------------------- */

bool kt_test_running(const struct kt_test *t) { return t->program->running; }
int kt_test_status(const struct kt_test *t) { return kt_program_status(t->program); }
uint64_t kt_test_now(const struct kt_test *t) { return t->now; }
int kt_test_sleep(struct kt_test *t) { return t->program->running ? kt_program_sleep(t->program) : -1; }
size_t kt_test_frames(const struct kt_test *t) { return t->frames; }

const char *kt_test_output(const struct kt_test *t, size_t *length) {
    if (length) *length = t->frame_length;
    return t->frame ? t->frame : "";
}

const char *kt_test_restore(const struct kt_test *t) {
    return t->program->running ? t->restore : t->left;
}

bool kt_test_cursor(const struct kt_test *t, int *x, int *y) {
    const struct kt_modes *modes = &t->program->applied;
    if (!modes->screen || modes->cursor_hidden) return false;
    if (x) *x = modes->cursor_x;
    if (y) *y = modes->cursor_y;
    return true;
}

static size_t cell_text(const struct kt_cell *cell, char *out) {
    size_t used = kt_utf8_encode(cell->glyph, out);
    unsigned i;
    for (i = 0; i < cell->combining_count && i < KT_COMBINING_MAX; i++)
        used += kt_utf8_encode(cell->combining[i], out + used);
    return used;
}

/* Appends one row without its trailing blanks; returns the new length. */
static size_t row_text(const struct kt_canvas *canvas, int row, char *out, size_t used) {
    size_t end = used;
    int x;
    for (x = 0; x < canvas->width; x++) {
        const struct kt_cell *cell = &canvas->cells[(size_t)row * (size_t)canvas->stride + (size_t)x];
        if (!cell->width) continue;
        used += cell_text(cell, out + used);
        if (cell->glyph != ' ' || cell->combining_count) end = used;
    }
    return end;
}

/* Rows first..last of the frame as text, or "" when there is no frame. */
static const char *rows_text(struct kt_test *t, int first, int last) {
    const struct kt_canvas *canvas = &t->program->canvas;
    size_t used = 0, room;
    char *text;
    int row;
    if (!t->program->cells || first < 0 || last >= canvas->height || first > last) return "";
    room = (size_t)(last - first + 1) * ((size_t)canvas->width * 4U * (KT_COMBINING_MAX + 1U) + 1U) + 1U;
    text = realloc(t->text, room);
    if (!text) return "";
    t->text = text;
    for (row = first; row <= last; row++) {
        used = row_text(canvas, row, text, used);
        if (row < last) text[used++] = '\n';
    }
    text[used] = '\0';
    return text;
}

const char *kt_test_row(struct kt_test *t, int row) { return rows_text(t, row, row); }

const char *kt_test_screen(struct kt_test *t) {
    return rows_text(t, 0, t->program->canvas.height - 1);
}

bool kt_test_contains(struct kt_test *t, const char *text) {
    return strstr(kt_test_screen(t), text) != NULL;
}

/* ---- processes ---------------------------------------------------------- */

size_t kt_test_process_count(const struct kt_test *t) { return t->process_count; }

const struct kt_test_process *kt_test_process(const struct kt_test *t, size_t index) {
    return index < t->process_count ? &t->processes[index]->info : NULL;
}

bool kt_test_exit(struct kt_test *t, uint64_t id, int status, const char *output) {
    size_t i, length = output ? strlen(output) : 0;
    for (i = 0; i < t->process_count; i++) {
        struct fake_process *process = t->processes[i];
        if (process->info.id != id || !process->info.running) continue;
        process->output = length ? malloc(length) : NULL;
        if (length && !process->output) return false;
        if (length) memcpy(process->output, output, length);
        process->length = length; process->status = status;
        process->info.running = false;
        kt_test_step(t);
        return true;
    }
    return false;
}

size_t kt_test_exec_count(const struct kt_test *t) { return t->exec_count; }

char *const *kt_test_exec(const struct kt_test *t, size_t index) {
    return index < t->exec_count ? t->execs[index].spawn.argv : NULL;
}

void kt_test_exec_status(struct kt_test *t, int status) { t->exec_status = status; }
size_t kt_test_suspends(const struct kt_test *t) { return t->suspends; }
