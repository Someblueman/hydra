#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_drive.h"
#include "fleet/support/files.h"
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The native run driver's event log: sequence numbers, and the .events.lock
 * protocol it shares with workflow_event in lib/workflow_runtime.sh. The
 * cases that wait on a lock run side by side, each in its own run directory. */

const char *f_home, *f_hydra;

/* A failed check ends the case; a function, so a case reads as a sequence. */
#if defined(__GNUC__) || defined(__clang__)
static void check(const char *file, int line, bool passed, const char *format, ...) __attribute__((format(printf, 4, 5)));
#endif
static void check(const char *file, int line, bool passed, const char *format, ...) {
    va_list arguments;
    if (passed) return;
    fprintf(stderr, "FAIL %s:%d: ", file, line);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    exit(1);
}
#define CHECK(condition, ...) check(__FILE__, __LINE__, (condition), __VA_ARGS__)

static char root[F_PATH];

static void put(const char *dir, const char *name, const char *text) {
    char path[F_PATH]; FILE *file;
    CHECK(!f_path(path, sizeof(path), dir, name), "path %s/%s", dir, name);
    CHECK((file = fopen(path, "w")) != NULL, "open %s", path);
    fputs(text, file);
    CHECK(!fclose(file), "write %s", path);
}

static void append(const char *dir, const char *name, const char *text) {
    char path[F_PATH]; FILE *file;
    CHECK(!f_path(path, sizeof(path), dir, name), "path %s/%s", dir, name);
    CHECK((file = fopen(path, "a")) != NULL, "open %s", path);
    fputs(text, file);
    CHECK(!fclose(file), "write %s", path);
}

static bool present(const char *dir, const char *name) {
    char path[F_PATH]; struct stat st;
    return !f_path(path, sizeof(path), dir, name) && !lstat(path, &st);
}

static void new_run(struct wdr_run *run, char *dir, const char *name) {
    CHECK(!f_path(dir, F_PATH, root, name), "run path %s", name);
    CHECK(!mkdir(dir, 0700), "mkdir %s", dir);
    put(dir, "run-id", "run_drive_test\n");
    memset(run, 0, sizeof(*run));
    run->dir = dir;
}

/* The lock as a writer leaves it when it dies: the directory, its claim and
 * the owner record (none when owner is NULL). */
static void hold_lock(const char *dir, bool claim, const char *owner) {
    char lock[F_PATH];
    CHECK(!f_path(lock, sizeof(lock), dir, ".events.lock"), "lock path");
    CHECK(!mkdir(lock, 0700), "mkdir %s", lock);
    if (claim) put(lock, ".claim", "");
    if (owner) put(lock, "owner-pid", owner);
}

/* The PID of a process that cannot be running. A reaped child's PID is not
 * that: with a small pid_max it is reused within seconds, and its new owner
 * makes the lock look held. 4194304 is above the highest PID Linux issues
 * (pid_max is at most 4194304) and above macOS's 99998. */
static long dead_pid(void) {
    return 4194304L;
}

/* A process that exited with status 0. */
static bool finished(pid_t pid) {
    int status;
    return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status);
}

static void pause_ms(long ms) {
    struct timespec left;
    left.tv_sec = ms / 1000; left.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&left, &left)) {}
}

static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* The sequence numbers and types of the recorded events, "1 a 2 b ". */
static void trace(const char *dir, char *out, size_t size) {
    char path[F_PATH], line[8192]; FILE *file; size_t used = 0;
    out[0] = '\0';
    CHECK(!f_path(path, sizeof(path), dir, "events.jsonl"), "events path");
    if (!(file = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), file)) {
        const char *sequence = strstr(line, "\"sequence\":"), *type = strstr(line, "\"type\":\"");
        long number; size_t length;
        if (!sequence || !type) { used += (size_t)snprintf(out + used, size - used, "? "); continue; }
        number = strtol(sequence + 11, NULL, 10);
        type += 8; length = strcspn(type, "\"");
        used += (size_t)snprintf(out + used, size - used, "%ld %.*s ", number, (int)length, type);
        CHECK(used < size, "trace of %s is too long", dir);
    }
    fclose(file);
}

static void expect_trace(const char *dir, const char *expected, const char *what) {
    char found[4096];
    trace(dir, found, sizeof(found));
    CHECK(!strcmp(found, expected), "%s: expected '%s', found '%s'", what, expected, found);
}

/* The lock is released and nothing of its protocol is left beside the
 * events, except the one entry a case expects. */
static void expect_released(const char *dir, const char *what, const char *expected) {
    DIR *entries = opendir(dir); struct dirent *entry;
    CHECK(entries != NULL, "%s: open %s", what, dir);
    if (!entries) return;
    while ((entry = readdir(entries)))
        CHECK(strncmp(entry->d_name, ".events.lock", 12) || (expected && !strcmp(entry->d_name, expected)),
            "%s: %s is left in the run directory", what, entry->d_name);
    closedir(entries);
}

static void sequences(void) {
    struct wdr_run run, other; char dir[F_PATH], path[F_PATH], padding[2048];
    new_run(&run, dir, "sequences");
    CHECK(!wdr_event(&run, NULL, "run.created", ""), "first event");
    CHECK(!wdr_event(&run, "a", "step.ready", "x=\"1\"\tend"), "second event");
    expect_trace(dir, "1 run.created 2 step.ready ", "events are numbered from 1");
    /* A shell writer appends between two events of the driver. */
    append(dir, "events.jsonl", "{\"schema_version\":1,\"sequence\":3,\"type\":\"step.running\"}\n");
    CHECK(!wdr_event(&run, NULL, "run.note", ""), "event after a foreign line");
    expect_trace(dir, "1 run.created 2 step.ready 3 step.running 4 run.note ", "lines of other writers are counted");
    /* Another process drives the run later: it counts the whole file once. */
    memset(&other, 0, sizeof(other));
    other.dir = dir;
    CHECK(!wdr_event(&other, NULL, "run.resumed", ""), "event of a second driver");
    CHECK(!wdr_event(&run, NULL, "run.again", ""), "event of the first driver after the second");
    expect_trace(dir, "1 run.created 2 step.ready 3 step.running 4 run.note 5 run.resumed 6 run.again ", "two drivers continue one numbering");
    /* awk 'END { print NR + 1 }': a last line without a newline counts too. */
    append(dir, "events.jsonl", "partial");
    CHECK(!wdr_event(&run, NULL, "run.after_partial", ""), "event after a partial line");
    expect_trace(dir, "1 run.created 2 step.ready 3 step.running 4 run.note 5 run.resumed 6 run.again 8 run.after_partial ",
        "a partial last line counts as a line");
    /* A shorter or replaced file is counted afresh. */
    put(dir, "events.jsonl", "{\"sequence\":1,\"type\":\"kept\"}\n");
    CHECK(!wdr_event(&run, NULL, "run.shorter", ""), "event after the file shrank");
    expect_trace(dir, "1 kept 2 run.shorter ", "a shorter file is counted afresh");
    /* A replaced file is longer than what was counted and may even get the
     * old file's inode: where the counted part ended it has no line end. */
    CHECK(!f_path(path, sizeof(path), dir, "events.jsonl") && !unlink(path), "remove events");
    memset(padding, 'x', sizeof(padding) - 1);
    padding[sizeof(padding) - 1] = '\0';
    put(dir, "events.jsonl", "{\"pad\":\"");
    append(dir, "events.jsonl", padding);
    append(dir, "events.jsonl", "\",\"sequence\":1,\"type\":\"a\"}\n{\"sequence\":2,\"type\":\"b\"}\n{\"sequence\":3,\"type\":\"c\"}\n");
    CHECK(!wdr_event(&run, NULL, "run.replaced", ""), "event after the file was replaced");
    expect_trace(dir, "1 a 2 b 3 c 4 run.replaced ", "a replaced file is counted afresh");
    expect_released(dir, "sequences", NULL);
}

/* A writer killed while it held the lock: broken at once. */
static void dead_owner(void) {
    struct wdr_run run; char dir[F_PATH], owner[32]; double started = seconds();
    new_run(&run, dir, "dead-owner");
    snprintf(owner, sizeof(owner), "%ld\n", dead_pid());
    hold_lock(dir, true, owner);
    CHECK(!wdr_event(&run, NULL, "run.after_dead_owner", ""), "event after a dead owner");
    CHECK(seconds() - started < 4.0, "a dead owner's lock is broken at once (%.1fs)", seconds() - started);
    expect_trace(dir, "1 run.after_dead_owner ", "dead owner");
    expect_released(dir, "dead owner", NULL);
}

/* A live writer's lock is waited for, never broken. */
static void live_owner(void) {
    struct wdr_run run; char dir[F_PATH], owner[32], lock[F_PATH]; double started = seconds(), waited; pid_t helper;
    new_run(&run, dir, "live-owner");
    snprintf(owner, sizeof(owner), "%ld\n", (long)getpid());
    hold_lock(dir, true, owner);
    CHECK(!f_path(lock, sizeof(lock), dir, ".events.lock"), "lock path");
    CHECK((helper = fork()) >= 0, "fork");
    if (helper == 0) {
        pause_ms(2500);
        append(dir, "events.jsonl", "{\"schema_version\":1,\"sequence\":1,\"type\":\"holder.event\"}\n");
        _exit(f_remove_tree(lock) ? 1 : 0);
    }
    CHECK(!wdr_event(&run, NULL, "run.after_live_owner", ""), "event after a live owner released");
    waited = seconds() - started;
    CHECK(finished(helper), "the holder released its lock itself");
    CHECK(waited >= 2.0, "a live owner's lock is waited for, not broken (%.1fs)", waited);
    expect_trace(dir, "1 holder.event 2 run.after_live_owner ", "live owner");
    expect_released(dir, "live owner", NULL);
}

/* A writer that died between creating the lock and recording itself as its
 * owner: the lock has no owner and no process will ever release it. */
static void ownerless(const char *name, bool claim, const char *owner) {
    struct wdr_run run; char dir[F_PATH]; double started = seconds(), waited;
    new_run(&run, dir, name);
    hold_lock(dir, claim, owner);
    CHECK(!wdr_event(&run, NULL, "run.after_abandoned", ""), "%s: event after an abandoned lock", name);
    waited = seconds() - started;
    CHECK(waited >= 4.0, "%s: an ownerless lock is given time to record its owner (%.1fs)", name, waited);
    CHECK(waited < 25.0, "%s: an abandoned ownerless lock is broken, not waited out (%.1fs)", name, waited);
    expect_trace(dir, "1 run.after_abandoned ", name);
    expect_released(dir, name, NULL);
    CHECK(!wdr_event(&run, NULL, "run.next", "") && seconds() - started - waited < 2.0, "%s: the next event does not wait", name);
}

/* A live writer that records itself late keeps its lock. */
static void late_owner(void) {
    struct wdr_run run; char dir[F_PATH], owner[32], lock[F_PATH]; double started = seconds(), waited; pid_t helper;
    new_run(&run, dir, "late-owner");
    hold_lock(dir, true, NULL);
    CHECK(!f_path(lock, sizeof(lock), dir, ".events.lock"), "lock path");
    snprintf(owner, sizeof(owner), "%ld\n", (long)getpid());
    CHECK((helper = fork()) >= 0, "fork");
    if (helper == 0) {
        pause_ms(2200);
        put(lock, "owner-pid", owner);
        pause_ms(6300);
        append(dir, "events.jsonl", "{\"schema_version\":1,\"sequence\":1,\"type\":\"holder.event\"}\n");
        _exit(f_remove_tree(lock) ? 1 : 0);
    }
    CHECK(!wdr_event(&run, NULL, "run.after_late_owner", ""), "event after a late owner released");
    waited = seconds() - started;
    CHECK(finished(helper), "the late owner released its lock itself");
    CHECK(waited >= 8.0, "a lock whose live owner recorded itself late is not broken (%.1fs)", waited);
    expect_trace(dir, "1 holder.event 2 run.after_late_owner ", "late owner");
    expect_released(dir, "late owner", NULL);
}

/* Another live process has announced that it is breaking the stale lock: it
 * may be about to rename it away and take the lock, so this writer must not
 * delete what it finds. The announcement of a dead process does not count. */
static void announced_breaker(void) {
    struct wdr_run run; char dir[F_PATH], owner[32], claim[64]; double started = seconds(), waited; pid_t breaker, helper; int status;
    new_run(&run, dir, "announced-breaker");
    snprintf(owner, sizeof(owner), "%ld\n", dead_pid());
    hold_lock(dir, true, owner);
    CHECK((breaker = fork()) >= 0, "fork");
    if (breaker == 0) { pause_ms(60000); _exit(0); }
    snprintf(claim, sizeof(claim), ".events.lock.breaking.%ld", (long)breaker);
    put(dir, claim, "");
    CHECK((helper = fork()) >= 0, "fork");
    if (helper == 0) { pause_ms(2500); _exit(kill(breaker, SIGKILL) ? 1 : 0); }
    /* The breaker is this process's child: it stays a zombie, which counts
     * as gone, until the wait below. */
    CHECK(!wdr_event(&run, NULL, "run.after_breaker", ""), "event after the other breaker died");
    waited = seconds() - started;
    CHECK(waitpid(helper, &status, 0) == helper && waitpid(breaker, &status, 0) == breaker, "helpers finished");
    CHECK(waited >= 2.0, "a stale lock another process is breaking is left to it (%.1fs)", waited);
    CHECK(waited < 25.0, "a dead breaker's announcement does not block the lock (%.1fs)", waited);
    expect_trace(dir, "1 run.after_breaker ", "announced breaker");
    CHECK(present(dir, claim), "a dead breaker's announcement is not deleted by others");
    expect_released(dir, "announced breaker", claim);
}

/* Of two writers that both found the owner dead, the slower one acts after
 * the faster one broke the lock and took it. The slower writer here is held
 * between looking at the lock and reading its owner record, which is a FIFO:
 * what it then reads is the dead owner of a lock that is no longer the one at
 * that name. It must not delete the lock that is: both writers would append
 * with the same sequence number. */
static void stale_view(void) {
    struct wdr_run run; char dir[F_PATH], lock[F_PATH], old[F_PATH], fifo[F_PATH], owner[32], found[256];
    pid_t slower; int feed; double started = seconds(), waited;
    new_run(&run, dir, "stale-view");
    hold_lock(dir, true, NULL);
    CHECK(!f_path(lock, sizeof(lock), dir, ".events.lock") && !f_path(fifo, sizeof(fifo), lock, "owner-pid"), "lock paths");
    CHECK(!f_path(old, sizeof(old), dir, "first-lock"), "old lock path");
    CHECK(!mkfifo(fifo, 0600), "mkfifo %s", fifo);
    CHECK((slower = fork()) >= 0, "fork");
    if (slower == 0) _exit(wdr_event(&run, NULL, "slower.event", "") ? 1 : 0);
    /* Returns once the slower writer is reading the owner record. */
    CHECK((feed = open(fifo, O_WRONLY)) >= 0, "open %s", fifo);
    /* The faster writer breaks the stale lock and takes the name. */
    CHECK(!rename(lock, old), "rename the stale lock away");
    snprintf(owner, sizeof(owner), "%ld\n", (long)getpid());
    hold_lock(dir, true, owner);
    snprintf(owner, sizeof(owner), "%ld\n", dead_pid());
    CHECK(write(feed, owner, strlen(owner)) == (ssize_t)strlen(owner) && !close(feed), "feed the dead owner");
    pause_ms(2500);
    trace(dir, found, sizeof(found));
    CHECK(!found[0], "a writer does not delete the lock another writer took after breaking the stale one: '%s'", found);
    CHECK(present(lock, "owner-pid"), "the faster writer still holds its lock");
    append(dir, "events.jsonl", "{\"schema_version\":1,\"sequence\":1,\"type\":\"faster.event\"}\n");
    CHECK(!f_remove_tree(lock) && !f_remove_tree(old), "the faster writer releases its lock");
    CHECK(finished(slower), "the slower writer publishes after it");
    waited = seconds() - started;
    CHECK(waited < 25.0, "the slower writer publishes once the lock is released (%.1fs)", waited);
    expect_trace(dir, "1 faster.event 2 slower.event ", "each of the two events has its own sequence number");
    expect_released(dir, "stale view", NULL);
}

#define WRITERS 8
#define EVENTS_EACH 6

/* One contending writer: it starts when the gate closes. */
static void writer(struct wdr_run *run, int gate[2]) {
    char byte; int event;
    close(gate[1]);
    (void)!read(gate[0], &byte, 1);
    for (event = 0; event < EVENTS_EACH; event++)
        if (wdr_event(run, NULL, "w", "")) _exit(1);
    _exit(0);
}

/* Many writers find one stale lock at the same moment. Each event keeps its
 * own sequence number: no writer deletes the lock another has just taken. */
static void contention(const char *name, bool claim, bool owned) {
    struct wdr_run run; char dir[F_PATH], owner[32], found[8192], expected[8192]; pid_t writers[WRITERS];
    int gate[2], i; size_t used = 0;
    new_run(&run, dir, name);
    snprintf(owner, sizeof(owner), "%ld\n", dead_pid());
    hold_lock(dir, claim, owned ? owner : NULL);
    CHECK(!pipe(gate), "pipe");
    for (i = 0; i < WRITERS; i++) {
        CHECK((writers[i] = fork()) >= 0, "fork");
        if (writers[i] == 0) writer(&run, gate);
    }
    close(gate[0]); close(gate[1]);
    for (i = 0; i < WRITERS; i++) CHECK(finished(writers[i]), "%s: writer %d published its events", name, i);
    trace(dir, found, sizeof(found));
    for (i = 1; i <= WRITERS * EVENTS_EACH; i++) used += (size_t)snprintf(expected + used, sizeof(expected) - used, "%d w ", i);
    CHECK(!strcmp(found, expected), "%s: every event has its own sequence number, 1..%d in file order: '%s'", name, WRITERS * EVENTS_EACH, found);
    expect_released(dir, name, NULL);
}

/* An event that cannot be appended whole: the file size limit sits ten
 * bytes past the log, so the first bytes of the line are written and the
 * rest are refused (EFBIG, with SIGXFSZ ignored), as on a disk that fills
 * mid-line. The event is reported as not recorded, the log is exactly what
 * it was, and the next event takes the number the lost one would have had.
 * Nothing is checked while the limit is in force: the test's own output
 * may be a regular file. */
static void torn_append(void) {
    struct wdr_run run; char dir[F_PATH], path[F_PATH], *before, *after;
    struct rlimit saved, limited; struct stat st; int status;
    new_run(&run, dir, "torn-append");
    CHECK(!wdr_event(&run, NULL, "run.created", ""), "first event");
    CHECK(!f_path(path, sizeof(path), dir, "events.jsonl") && !stat(path, &st) && st.st_size > 0, "the log exists");
    CHECK((before = f_read(path, 65536)) != NULL, "read the log");
    CHECK(!getrlimit(RLIMIT_FSIZE, &saved) && signal(SIGXFSZ, SIG_IGN) != SIG_ERR, "file size limit");
    limited = saved; limited.rlim_cur = (rlim_t)st.st_size + 10;
    CHECK(!setrlimit(RLIMIT_FSIZE, &limited), "set the file size limit");
    status = wdr_event(&run, "a", "step.ready", "does not fit");
    if (setrlimit(RLIMIT_FSIZE, &saved)) _exit(2);
    CHECK(status != 0, "an event that could not be appended is reported");
    after = f_read(path, 65536);
    CHECK(after && !strcmp(before, after), "the log is unchanged: '%s'", after ? after : "(unreadable)");
    expect_released(dir, "torn-append", NULL);
    CHECK(!wdr_event(&run, "a", "step.ready", "fits"), "the event is recorded once writes work");
    expect_trace(dir, "1 run.created 2 step.ready ", "the lost event's number is used by the next one");
    free(before); free(after);
}

static void run_ownerless_claimed(void) { ownerless("ownerless-claimed", true, NULL); }
static void run_ownerless_unclaimed(void) { ownerless("ownerless-unclaimed", false, NULL); }
static void run_ownerless_empty(void) { ownerless("ownerless-empty-record", true, ""); }
static void run_contention_dead(void) { contention("contention-dead-owner", true, true); }
static void run_contention_ownerless(void) { contention("contention-ownerless", true, false); }

static bool selected(int argc, char **argv, const char *name) {
    int i;
    for (i = 1; i < argc; i++) if (!strcmp(argv[i], name)) return true;
    return argc == 1;
}

/* With arguments, only the named cases run. */
int main(int argc, char **argv) {
    static const struct { const char *name; void (*run)(void); } cases[] = {
        {"sequences", sequences}, {"torn-append", torn_append}, {"dead-owner", dead_owner}, {"live-owner", live_owner},
        {"ownerless-claimed", run_ownerless_claimed}, {"ownerless-unclaimed", run_ownerless_unclaimed},
        {"ownerless-empty-record", run_ownerless_empty}, {"late-owner", late_owner}, {"announced-breaker", announced_breaker}, {"stale-view", stale_view},
        {"contention-dead-owner", run_contention_dead}, {"contention-ownerless", run_contention_ownerless}};
    enum { COUNT = sizeof(cases) / sizeof(cases[0]) };
    pid_t pids[COUNT]; size_t i; int failed = 0;
    const char *tmp = getenv("TMPDIR");
    CHECK(!f_format(root, sizeof(root), "%s/hydra-drive-test.XXXXXX", tmp && tmp[0] ? tmp : "/tmp") && mkdtemp(root), "temporary directory");
    for (i = 0; i < COUNT; i++) {
        pids[i] = -1;
        if (!selected(argc, argv, cases[i].name)) continue;
        CHECK((pids[i] = fork()) >= 0, "fork");
        if (pids[i] == 0) { cases[i].run(); _exit(0); }
    }
    for (i = 0; i < COUNT; i++) {
        if (pids[i] < 0 || finished(pids[i])) continue;
        fprintf(stderr, "FAIL case %s\n", cases[i].name);
        failed = 1;
    }
    (void)f_remove_tree(root);
    if (failed) return 1;
    puts("workflow drive events: sequence numbering, stale and abandoned locks, late owners, announced breakers and contention passed");
    return 0;
}
