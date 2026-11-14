#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "snapshot.h"
#include "wake.h"
/* Observes whether each interactive head's terminal is producing output and
 * hands every change to the shell CLI, which appends it to the head's event
 * log. The only signal is tmux's window activity time: the last moment a
 * window's panes wrote anything. Agent CLIs redraw while they work and are
 * silent while they wait, so "active" and "quiet" here are inferences about
 * output, never statements about what an agent is waiting for.
 *
 * Observing starts no process: the times arrive with the session list the
 * head snapshot already asks tmux for at every refresh (snapshot.h), so this
 * runs where that snapshot is read in this process. A hydra child runs only
 * when a head's state changed, plus one every ACTIVITY_WATCH_SECONDS that
 * records that someone was watching: time without an observer is unobserved,
 * which is not the same as quiet. Headless, remote and fleet heads are left to
 * their owners, which report exact evidence. */

/* Silence that makes a head quiet: no output for this long. */
#define ACTIVITY_QUIET_AFTER_SECONDS 10
/* Output must continue for this long before a quiet head counts as active, so
 * a redraw or a single line does not become a pair of changes. Together with
 * the silence above this bounds a flapping head to two changes per
 * ACTIVITY_QUIET_AFTER_SECONDS + ACTIVITY_MIN_ACTIVE_SECONDS. */
#define ACTIVITY_MIN_ACTIVE_SECONDS 4
/* How often a watching observer says so. The shell closes a watch interval
 * after 45 seconds without a report. */
#define ACTIVITY_WATCH_SECONDS 10
#define ACTIVITY_CHILD_BUDGET_MS 10000L
#define ACTIVITY_QUIT_WAIT_MS 2000L
#define ACTIVITY_RECORDERS 2
#define ACTIVITY_RETRY_SECONDS 30
#define ACTIVITY_RETRY_LIMIT_SECONDS 600

enum activity_state { ACTIVITY_UNKNOWN, ACTIVITY_ACTIVE, ACTIVITY_QUIET };

/* A hydra child that records one fact; it is never signalled inside its
 * budget, so it does not leave a lock behind. */
struct activity_child { pid_t pid; struct timespec started; };

struct activity_head {
    char instance[TEXT], branch[TEXT], session[TEXT];
    /* seen: the newest activity time tmux reported. burst: the first output
     * after silence, until it has lasted long enough to count. */
    long long seen, burst, since, retry_at;
    enum activity_state state, recorded, sending;
    struct activity_child recorder;
    unsigned failures;
    bool present;
};

struct native_activity {
    struct activity_child watch;
    long long watched_at;
    size_t count;
    struct activity_head heads[MAX_HEADS];
};

static long child_elapsed_ms(const struct activity_child *child) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - child->started.tv_sec) * 1000L + (long)(now.tv_nsec - child->started.tv_nsec) / 1000000L;
}

/* Local heads with a terminal of their own and a current instance. */
static bool observable(const struct app *app, const struct head *h) {
    return !app->fleet && !h->remote_host[0] && !head_headless(h) && h->branch[0] && h->session[0] &&
           strcmp(h->session, "-") != 0 && !strncmp(h->instance, "instance_", 9) &&
           h->instance[9 + strspn(h->instance + 9, "0123456789abcdef")] == '\0';
}

static bool any_observable(const struct app *app) {
    size_t i;
    for (i = 0; i < app->model.head_count; i++) if (observable(app, &app->model.heads[i])) return true;
    return false;
}

static struct activity_head *tracked(struct native_activity *a, const struct head *h) {
    struct activity_head *entry;
    size_t i;
    for (i = 0; i < a->count; i++) if (!strcmp(a->heads[i].instance, h->instance)) return &a->heads[i];
    if (a->count == MAX_HEADS) return NULL;
    entry = &a->heads[a->count++];
    memset(entry, 0, sizeof(*entry));
    copy_text(entry->instance, sizeof(entry->instance), h->instance);
    return entry;
}

/* Tracks the heads of the current snapshot; an instance that left it is
 * forgotten once no child of its is running. */
static void track_heads(struct app *app, struct native_activity *a) {
    size_t i, kept = 0;
    for (i = 0; i < a->count; i++) a->heads[i].present = false;
    for (i = 0; i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        struct activity_head *entry = observable(app, h) ? tracked(a, h) : NULL;
        if (!entry) continue;
        entry->present = true;
        copy_text(entry->branch, sizeof(entry->branch), h->branch);
        copy_text(entry->session, sizeof(entry->session), h->session);
    }
    for (i = 0; i < a->count; i++) {
        if (!a->heads[i].present && !a->heads[i].recorder.pid) continue;
        if (kept != i) a->heads[kept] = a->heads[i];
        kept++;
    }
    a->count = kept;
}

static void set_state(struct activity_head *entry, enum activity_state state, long long since) {
    if (entry->state == state) return;
    entry->state = state; entry->since = since;
}

/* Quiet begins at the last output; active begins at the first output of a
 * burst that went on for ACTIVITY_MIN_ACTIVE_SECONDS. A window tmux no longer
 * lists produces no output. */
static void infer(struct activity_head *entry, long long activity, long long now) {
    if (activity > entry->seen && activity <= now) entry->seen = activity;
    if (!entry->seen) return;
    if (now - entry->seen >= ACTIVITY_QUIET_AFTER_SECONDS) {
        entry->burst = 0;
        set_state(entry, ACTIVITY_QUIET, entry->seen);
        return;
    }
    if (entry->state == ACTIVITY_ACTIVE) return;
    if (!entry->burst) entry->burst = entry->seen;
    if (entry->seen - entry->burst >= ACTIVITY_MIN_ACTIVE_SECONDS) set_state(entry, ACTIVITY_ACTIVE, entry->burst);
}

/* The newest of the space-separated times of one session's windows. */
static long long newest_time(const char *times) {
    long long newest = 0;
    char *end;
    while (*times) {
        long long value = strtoll(times, &end, 10);
        if (end == times) break;
        if (value > newest) newest = value;
        times = end;
    }
    return newest;
}

/* One "session:time time ..." line of the snapshot's tmux answer. */
static void session_line(struct native_activity *a, char *line, long long *newest) {
    char *colon = strchr(line, ':');
    long long activity;
    size_t i;
    if (!colon) return;
    *colon = '\0';
    activity = newest_time(colon + 1);
    for (i = 0; i < a->count; i++)
        if (activity > newest[i] && !strcmp(a->heads[i].session, line)) newest[i] = activity;
}

/* Infers every tracked head's state from tmux's answer. False when it named
 * no activity time for any of them: nothing was observed. */
static bool observe(struct app *app, struct native_activity *a, char *text) {
    long long newest[MAX_HEADS], now = (long long)time(NULL);
    char *line, *next;
    bool seen = false;
    size_t i;
    track_heads(app, a);
    memset(newest, 0, sizeof(newest));
    for (line = text; *line; line = next) {
        next = line + strcspn(line, "\n");
        if (*next) *next++ = '\0';
        session_line(a, line, newest);
    }
    for (i = 0; i < a->count; i++) {
        if (!a->heads[i].present) continue;
        if (newest[i]) seen = true;
        infer(&a->heads[i], newest[i], now);
    }
    return seen;
}

static bool child_start(struct activity_child *child, char *const argv[], int input) {
    if (!native_detached_start(&child->pid, argv, input)) { child->pid = 0; return false; }
    (void)clock_gettime(CLOCK_MONOTONIC, &child->started);
    return true;
}

/* 0 while the child runs, 1 when it succeeded, -1 when it failed or was
 * stopped at the end of its budget. */
static int child_poll(struct activity_child *child) {
    int status = 0;
    pid_t result;
    if (!child->pid) return -1;
    result = waitpid(child->pid, &status, WNOHANG);
    if (result == 0 && child_elapsed_ms(child) >= ACTIVITY_CHILD_BUDGET_MS) {
        (void)kill(-child->pid, SIGKILL); (void)kill(child->pid, SIGKILL);
        do { result = waitpid(child->pid, &status, 0); } while (result < 0 && errno == EINTR);
        status = -1;
    }
    if (result == 0) { wake_at(&child->started, ACTIVITY_CHILD_BUDGET_MS); return 0; }
    child->pid = 0;
    return result > 0 && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 1 : -1;
}

static const char *state_name(enum activity_state state) {
    return state == ACTIVITY_ACTIVE ? "active" : "quiet";
}

/* The change travels as canonical JSON on the recorder's stdin. */
static bool record_start(struct app *app, struct activity_head *entry) {
    char *argv[] = {(char *)app->hydra, "adapter", "activity", entry->branch, NULL};
    char json[TEXT + 160];
    int pipes[2], length;
    bool started;
    length = snprintf(json, sizeof(json), "{\"schema_version\":1,\"instance_id\":\"%s\",\"kind\":\"activity\",\"state\":\"%s\",\"since\":%lld}\n",
                      entry->instance, state_name(entry->state), entry->since);
    if (length < 0 || (size_t)length >= sizeof(json) || pipe(pipes) != 0) return false;
    (void)fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    started = write(pipes[1], json, (size_t)length) == (ssize_t)length && child_start(&entry->recorder, argv, pipes[0]);
    close(pipes[0]); close(pipes[1]);
    if (started) entry->sending = entry->state;
    return started;
}

static void record_failed(struct activity_head *entry, long long now) {
    long long delay = ACTIVITY_RETRY_SECONDS;
    unsigned i;
    for (i = 0; i < entry->failures && delay < ACTIVITY_RETRY_LIMIT_SECONDS; i++) delay *= 2;
    if (delay > ACTIVITY_RETRY_LIMIT_SECONDS) delay = ACTIVITY_RETRY_LIMIT_SECONDS;
    entry->failures++;
    entry->retry_at = now + delay;
}

/* Reaps finished recorders; returns how many are still running. A refused
 * change is retried later and less often, never on every refresh. */
static size_t reap_recorders(struct native_activity *a, long long now) {
    size_t i, running = 0;
    for (i = 0; i < a->count; i++) {
        struct activity_head *entry = &a->heads[i];
        int result;
        if (!entry->recorder.pid) continue;
        result = child_poll(&entry->recorder);
        if (result == 0) running++;
        else if (result > 0) { entry->recorded = entry->sending; entry->failures = 0; }
        else record_failed(entry, now);
    }
    return running;
}

static bool wants_record(const struct activity_head *entry, long long now) {
    return entry->present && !entry->recorder.pid && entry->state != ACTIVITY_UNKNOWN &&
           entry->state != entry->recorded && now >= entry->retry_at;
}

/* Starts a recorder for each head whose state differs from what was last
 * recorded, a few at a time. */
static void record_changes(struct app *app, struct native_activity *a) {
    long long now = (long long)time(NULL);
    size_t i, running = reap_recorders(a, now);
    for (i = 0; i < a->count && running < ACTIVITY_RECORDERS; i++) {
        struct activity_head *entry = &a->heads[i];
        if (!wants_record(entry, now)) continue;
        if (record_start(app, entry)) running++;
        else record_failed(entry, now);
    }
}

static void watch_report(struct app *app, struct native_activity *a, bool observed) {
    char *argv[] = {(char *)app->hydra, "adapter", "watch", NULL};
    long long now = (long long)time(NULL);
    if (a->watch.pid) (void)child_poll(&a->watch);
    if (!observed || a->watch.pid || (a->watched_at && now - a->watched_at < ACTIVITY_WATCH_SECONDS)) return;
    if (child_start(&a->watch, argv, -1)) a->watched_at = now;
}

/* Called on every round of background work, after the head snapshot's: takes
 * what its tmux command said about window activity, when a refresh brought
 * something new, and looks after the children. Only the interactive local
 * control centre that reads the snapshot itself observes. */
void native_activity_tick(struct app *app) {
    struct native_activity *a = app->activity;
    char *text;
    bool observed = false;
    if (!app->program || app->fleet || !app->store) return;
    text = snapshot_take_activity(app->snapshot);
    if (!a && text && any_observable(app)) a = app->activity = calloc(1, sizeof(*a));
    if (a && text) observed = observe(app, a, text);
    free(text);
    if (!a) return;
    record_changes(app, a);
    watch_report(app, a, observed);
}

/* Gives a child that is still recording a moment to finish, then stops it. */
static void child_finish(struct activity_child *child, long long deadline) {
    const struct timespec pause = {0, 10000000L};
    int status;
    pid_t result;
    if (!child->pid) return;
    while ((result = waitpid(child->pid, &status, WNOHANG)) == 0 && (long long)wake_now() < deadline)
        (void)nanosleep(&pause, NULL);
    if (result == 0) {
        (void)kill(-child->pid, SIGKILL); (void)kill(child->pid, SIGKILL);
        do { result = waitpid(child->pid, &status, 0); } while (result < 0 && errno == EINTR);
    }
    child->pid = 0;
}

/* Leaves no child behind: recorders already running get
 * ACTIVITY_QUIT_WAIT_MS between them to finish their append. */
void native_activity_destroy(struct app *app) {
    struct native_activity *a = app->activity;
    long long deadline = (long long)wake_now() + ACTIVITY_QUIT_WAIT_MS;
    size_t i;
    if (!a) return;
    for (i = 0; i < a->count; i++) child_finish(&a->heads[i].recorder, deadline);
    child_finish(&a->watch, deadline);
    free(a); app->activity = NULL;
}
