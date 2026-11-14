#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
#include "snapshot.h"
#include "store.h"
#include "wake.h"
#include <sys/utsname.h>
/* A refresh of the local head snapshot (snapshot.h). In order: the project is
 * located once (one git command for the life of the process), the store is
 * read in this process, and then, side by side, tmux lists the live sessions,
 * git counts uncommitted files in the worktrees that need it, and the shell
 * CLI describes leftover worktrees if the store shows any could exist. The
 * rows are written when the last of them has ended.
 *
 * git is the only cost that could grow with the number of heads, so it is
 * rationed. A worktree is asked again when its index or HEAD changed, when it
 * is the selected head's, or when its turn comes: a fixed number of unchanged
 * worktrees per refresh, least recently read first. A count can therefore lag
 * an edit that touched neither index nor HEAD by a few refreshes; it cannot
 * lag the selected head, a commit or a staged change. */

#define SNAPSHOT_PROBES 4U
#define SNAPSHOT_ROTATION 4U

enum { DIFF_IDLE, DIFF_QUEUED, DIFF_RUNNING };

/* The uncommitted files of one worktree, and what its index and HEAD looked
 * like when they were counted. */
struct diff_entry {
    char worktree[STORE_PATH];
    struct store_stamp index, head;
    unsigned count;
    unsigned long long read_at;
    bool wanted;
    int state;
};

struct diff_probe {
    struct native_capture job;
    size_t entry;
    struct store_stamp index, head;
};

struct native_snapshot {
    char hydra[STORE_PATH];
    struct store_location where;
    bool located;
    struct store_cache cache;
    struct diff_entry *diffs;
    size_t diff_count, diff_capacity;
    unsigned long long refresh;
    /* The refresh in flight. */
    bool active, read, failed, timed_out;
    struct timespec started;
    long budget_ms;
    char selected[TEXT];
    struct store_snapshot store;
    struct native_capture locate, sessions, leftovers;
    char *session_text, *leftover_rows;
    /* What tmux said about window activity at the latest refresh that asked
     * it, until snapshot_take_activity hands it over. */
    char *activity_text;
    struct diff_probe probes[SNAPSHOT_PROBES];
};

/* ---- where the records are ----------------------------------------------- */

static void physical_path(const char *path, char *out, size_t size) {
    char *resolved = path[0] ? realpath(path, NULL) : NULL;
    copy_text(out, size, resolved ? resolved : "");
    free(resolved);
}

/* HYDRA_HOME as the CLI resolves it, and this machine's name as lock owners
 * record it. */
static void locate_home(struct store_location *where) {
    const char *home = getenv("HYDRA_HOME"), *root = getenv("HYDRA_STATE_V2_ROOT"), *user = getenv("HOME");
    struct utsname machine;
    char fallback[STORE_PATH];
    if (!home || !home[0]) {
        store_path(fallback, sizeof(fallback), "%s/.hydra", user ? user : "");
        home = fallback;
    }
    physical_path(home, where->home, sizeof(where->home));
    if (!where->home[0]) copy_text(where->home, sizeof(where->home), home);
    if (root && root[0]) copy_text(where->state_root, sizeof(where->state_root), root);
    else store_path(where->state_root, sizeof(where->state_root), "%s/state/v2", where->home);
    if (gethostname(where->host, sizeof(where->host)) == 0 && where->host[0]) { where->host[sizeof(where->host) - 1U] = '\0'; return; }
    copy_text(where->host, sizeof(where->host), uname(&machine) == 0 ? machine.nodename : "unknown");
}

static long remaining_ms(const struct native_snapshot *s) {
    struct timespec now;
    long elapsed;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = (long)(now.tv_sec - s->started.tv_sec) * 1000L + (long)(now.tv_nsec - s->started.tv_nsec) / 1000000L;
    return elapsed < s->budget_ms ? s->budget_ms - elapsed : 1L;
}

/* A finished command's output as text; NULL when it produced no complete
 * output. Records whether it ran out of time. */
static char *command_text(struct native_snapshot *s, struct native_capture *job) {
    bool timed_out = job->timed_out, success;
    FILE *output = native_capture_result(job, &success);
    char *text = NULL;
    long size;
    if (timed_out) s->timed_out = true;
    if (!output) return NULL;
    if (fseek(output, 0, SEEK_END) == 0 && (size = ftell(output)) >= 0 && fseek(output, 0, SEEK_SET) == 0 &&
        (text = malloc((size_t)size + 1U)) != NULL)
        text[fread(text, 1, (size_t)size, output)] = '\0';
    fclose(output);
    return text;
}

/* git names the common directory and the top level, one per line; outside a
 * repository it names neither, and the question is asked again next time. */
static void locate_accept(struct native_snapshot *s, char *text) {
    char *toplevel = text ? strchr(text, '\n') : NULL;
    if (toplevel) {
        *toplevel++ = '\0';
        toplevel[strcspn(toplevel, "\n")] = '\0';
        copy_text(s->where.toplevel, sizeof(s->where.toplevel), toplevel);
    }
    if (text) physical_path(text, s->where.common, sizeof(s->where.common));
    s->located = s->where.common[0] != '\0';
    free(text);
}

static void locate_start(struct native_snapshot *s) {
    char *argv[] = {(char *)"git", (char *)"rev-parse", (char *)"--git-common-dir", (char *)"--show-toplevel", NULL};
    s->where.common[0] = '\0'; s->where.toplevel[0] = '\0';
    (void)native_capture_start(&s->locate, argv, remaining_ms(s));
}

/* ---- uncommitted files ---------------------------------------------------- */

/* The index and HEAD of a worktree: a linked worktree's .git is a file that
 * names its directory. Stamps of what cannot be found are all zero. */
static void worktree_stamps(const char *worktree, struct store_stamp *index, struct store_stamp *head) {
    char dotgit[STORE_PATH], line[STORE_PATH], directory[STORE_PATH] = "", path[STORE_PATH];
    struct stat info;
    store_path(dotgit, sizeof(dotgit), "%s/.git", worktree);
    if (stat(dotgit, &info) == 0 && S_ISDIR(info.st_mode)) copy_text(directory, sizeof(directory), dotgit);
    else if (store_first_line(dotgit, line, sizeof(line)) && !strncmp(line, "gitdir: ", 8)) {
        if (line[8] == '/') copy_text(directory, sizeof(directory), line + 8);
        else store_path(directory, sizeof(directory), "%s/%s", worktree, line + 8);
    }
    store_path(path, sizeof(path), "%s/index", directory);
    (void)store_stamp(directory[0] ? path : "", index);
    store_path(path, sizeof(path), "%s/HEAD", directory);
    (void)store_stamp(directory[0] ? path : "", head);
}

static struct diff_entry *diff_find(struct native_snapshot *s, const char *worktree) {
    size_t i;
    for (i = 0; i < s->diff_count; i++) if (!strcmp(s->diffs[i].worktree, worktree)) return &s->diffs[i];
    return NULL;
}

static struct diff_entry *diff_add(struct native_snapshot *s, const char *worktree) {
    struct diff_entry *entry = diff_find(s, worktree);
    if (entry) return entry;
    if (s->diff_count == s->diff_capacity) {
        size_t next = s->diff_capacity ? s->diff_capacity * 2U : 16U;
        struct diff_entry *grown = realloc(s->diffs, next * sizeof(*grown));
        if (!grown) return NULL;
        s->diffs = grown; s->diff_capacity = next;
    }
    entry = &s->diffs[s->diff_count++];
    memset(entry, 0, sizeof(*entry));
    copy_text(entry->worktree, sizeof(entry->worktree), worktree);
    return entry;
}

/* Forgets worktrees no head of this refresh uses. Nothing is being read yet. */
static void diffs_compact(struct native_snapshot *s) {
    size_t from, to = 0;
    for (from = 0; from < s->diff_count; from++) if (s->diffs[from].wanted) s->diffs[to++] = s->diffs[from];
    s->diff_count = to;
}

/* Queues the worktrees that must be read: never read, changed, or selected. */
static void diffs_queue_changed(struct native_snapshot *s) {
    size_t i;
    for (i = 0; i < s->store.head_count; i++) {
        const struct store_head *h = &s->store.heads[i];
        struct diff_entry *entry = h->worktree[0] ? diff_add(s, h->worktree) : NULL;
        struct store_stamp index, head;
        if (!entry) continue;
        entry->wanted = true;
        worktree_stamps(entry->worktree, &index, &head);
        if (!entry->read_at || !strcmp(h->branch, s->selected) ||
            !store_stamp_equal(&index, &entry->index) || !store_stamp_equal(&head, &entry->head)) entry->state = DIFF_QUEUED;
    }
}

/* Queues the unchanged worktrees whose turn it is: the least recently read. */
static void diffs_queue_rotation(struct native_snapshot *s) {
    size_t turn, i;
    for (turn = 0; turn < SNAPSHOT_ROTATION; turn++) {
        struct diff_entry *oldest = NULL;
        for (i = 0; i < s->diff_count; i++) {
            struct diff_entry *entry = &s->diffs[i];
            if (entry->state == DIFF_IDLE && (!oldest || entry->read_at < oldest->read_at)) oldest = entry;
        }
        if (!oldest) return;
        oldest->state = DIFF_QUEUED;
    }
}

static void diffs_plan(struct native_snapshot *s) {
    size_t i;
    for (i = 0; i < s->diff_count; i++) { s->diffs[i].wanted = false; s->diffs[i].state = DIFF_IDLE; }
    diffs_queue_changed(s);
    diffs_compact(s);
    diffs_queue_rotation(s);
}

static void probe_start(struct native_snapshot *s, struct diff_probe *probe, size_t entry) {
    struct diff_entry *e = &s->diffs[entry];
    /* Reading a worktree must not take the locks its agent's own git needs. */
    char *argv[] = {(char *)"git", (char *)"--no-optional-locks", (char *)"-C", e->worktree, (char *)"status", (char *)"--short", NULL};
    probe->entry = entry;
    worktree_stamps(e->worktree, &probe->index, &probe->head);
    e->state = DIFF_RUNNING;
    if (native_capture_start(&probe->job, argv, remaining_ms(s))) return;
    /* A command that cannot start says nothing, which counts as no files. */
    e->count = 0; e->read_at = s->refresh; e->state = DIFF_IDLE;
}

static void probe_accept(struct native_snapshot *s, struct diff_probe *probe) {
    struct diff_entry *e = &s->diffs[probe->entry];
    char *text = command_text(s, &probe->job), *line;
    e->count = 0;
    for (line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : line + strlen(line)) e->count++;
    free(text);
    e->index = probe->index; e->head = probe->head;
    e->read_at = s->refresh; e->state = DIFF_IDLE;
}

/* Finished reads are taken and waiting ones started, a few at a time. True
 * while any is running or waiting. */
static bool probes_step(struct native_snapshot *s) {
    size_t slot, next = 0;
    bool busy = false;
    for (slot = 0; slot < SNAPSHOT_PROBES; slot++) {
        struct diff_probe *probe = &s->probes[slot];
        if (probe->job.pid && native_capture_step(&probe->job)) probe_accept(s, probe);
        while (!probe->job.pid && next < s->diff_count) {
            if (s->diffs[next].state == DIFF_QUEUED) probe_start(s, probe, next);
            next++;
        }
        if (probe->job.pid) busy = true;
    }
    return busy;
}

static bool diff_lookup(void *context, const char *worktree, unsigned *count) {
    const struct diff_entry *entry = diff_find(context, worktree);
    if (!entry || !entry->read_at) return false;
    *count = entry->count;
    return true;
}

/* ---- one refresh ---------------------------------------------------------- */

/* tmux lists a live session as its name, a colon (no session name holds
 * one) and when each of its windows last produced output. The rows are written
 * from the names; the whole answer is kept for snapshot_take_activity, so the
 * one tmux command of a refresh also serves whoever watches activity. */
static void sessions_accept(struct native_snapshot *s, char *text) {
    char *from, *to;
    free(s->activity_text);
    s->activity_text = text ? strdup(text) : NULL;
    s->session_text = text;
    for (from = to = text; from && *from; ) {
        size_t line = strcspn(from, "\n"), name = strcspn(from, ":\n");
        memmove(to, from, name);
        to += name; from += line;
        if (*from == '\n') { *to++ = '\n'; from++; }
    }
    if (to) *to = '\0';
}

/* Reads the store, then asks tmux, git and the CLI for what it cannot say. */
static void refresh_read(struct native_snapshot *s) {
    char *sessions[] = {(char *)"tmux", (char *)"list-sessions", (char *)"-F", (char *)"#{session_name}:#{W:#{window_activity} }", NULL};
    char *leftovers[] = {s->hydra, (char *)"tui", (char *)"--orphan-rows", NULL};
    s->read = true;
    if (!store_read(&s->where, &s->cache, &s->store)) { s->failed = true; return; }
    if (s->store.interactive) (void)native_capture_start(&s->sessions, sessions, remaining_ms(s));
    if (s->store.leftovers) (void)native_capture_start(&s->leftovers, leftovers, remaining_ms(s));
    diffs_plan(s);
}

/* True while the refresh still waits for a command. */
static bool refresh_advance(struct native_snapshot *s) {
    bool busy;
    if (s->locate.pid) {
        if (!native_capture_step(&s->locate)) return true;
        locate_accept(s, command_text(s, &s->locate));
    }
    if (!s->read) refresh_read(s);
    if (s->failed) return false;
    if (s->sessions.pid && native_capture_step(&s->sessions)) sessions_accept(s, command_text(s, &s->sessions));
    if (s->leftovers.pid && native_capture_step(&s->leftovers)) s->leftover_rows = command_text(s, &s->leftovers);
    busy = probes_step(s);
    return busy || s->sessions.pid || s->leftovers.pid;
}

static FILE *refresh_rows(struct native_snapshot *s) {
    FILE *rows = s->failed || s->timed_out ? NULL : tmpfile();
    if (!rows) return NULL;
    if (store_write(&s->store, s->session_text, diff_lookup, s, s->leftover_rows, rows) && fseek(rows, 0, SEEK_SET) == 0) return rows;
    fclose(rows);
    return NULL;
}

static void refresh_end(struct native_snapshot *s) {
    store_snapshot_free(&s->store);
    free(s->session_text); s->session_text = NULL;
    free(s->leftover_rows); s->leftover_rows = NULL;
    s->active = false;
}

void snapshot_begin(struct native_snapshot *s, const char *selected, long budget_ms) {
    if (s->active) return;
    s->active = true; s->read = s->failed = s->timed_out = false;
    s->refresh++;
    s->budget_ms = budget_ms;
    (void)clock_gettime(CLOCK_MONOTONIC, &s->started);
    copy_text(s->selected, sizeof(s->selected), selected);
    if (!s->located) locate_start(s);
}

bool snapshot_active(const struct native_snapshot *s) {
    return s && s->active;
}

bool snapshot_step(struct native_snapshot *s, FILE **rows, bool *timed_out) {
    *rows = NULL; *timed_out = false;
    if (!s->active || refresh_advance(s)) return false;
    *rows = refresh_rows(s);
    *timed_out = s->timed_out;
    refresh_end(s);
    return true;
}

void snapshot_cancel(struct native_snapshot *s) {
    size_t slot;
    if (!s || !s->active) return;
    native_capture_destroy(&s->locate);
    native_capture_destroy(&s->sessions);
    native_capture_destroy(&s->leftovers);
    for (slot = 0; slot < SNAPSHOT_PROBES; slot++) native_capture_destroy(&s->probes[slot].job);
    refresh_end(s);
}

char *snapshot_take_activity(struct native_snapshot *s) {
    char *text = s ? s->activity_text : NULL;
    if (s) s->activity_text = NULL;
    return text;
}

const struct store_location *snapshot_where(const struct native_snapshot *s) {
    return s && s->located ? &s->where : NULL;
}

struct native_snapshot *snapshot_new(const char *hydra) {
    struct native_snapshot *s = calloc(1, sizeof(*s));
    size_t slot;
    if (!s) return NULL;
    copy_text(s->hydra, sizeof(s->hydra), hydra);
    s->locate.fd = s->sessions.fd = s->leftovers.fd = -1;
    for (slot = 0; slot < SNAPSHOT_PROBES; slot++) s->probes[slot].job.fd = -1;
    locate_home(&s->where);
    return s;
}

void snapshot_free(struct native_snapshot *s) {
    if (!s) return;
    snapshot_cancel(s);
    store_cache_free(&s->cache);
    free(s->diffs);
    free(s->activity_text);
    free(s);
}

int snapshot_print(const char *hydra, FILE *out) {
    struct native_snapshot *s = snapshot_new(hydra);
    const struct timespec pause = {0, 1000000L};
    FILE *rows = NULL;
    bool timed_out = false;
    char bytes[8192];
    size_t length;
    if (!s) return 1;
    snapshot_begin(s, "", HYDRA_TUI_LOCAL_CAPTURE_BUDGET_MS);
    while (!snapshot_step(s, &rows, &timed_out)) { wake_reset(); (void)nanosleep(&pause, NULL); }
    snapshot_free(s);
    if (!rows) { fputs(timed_out ? "hydra-tui: reading the head snapshot timed out\n" : "hydra-tui: the head snapshot could not be read\n", stderr); return 1; }
    while ((length = fread(bytes, 1, sizeof(bytes), rows)) > 0) (void)fwrite(bytes, 1, length, out);
    fclose(rows);
    return fflush(out) == 0 && !ferror(out) ? 0 : 1;
}
