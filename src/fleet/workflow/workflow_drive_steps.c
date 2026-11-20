#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_drive.h"
#include "fleet/support/files.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* Each function records what the shell function named beside it records, in
 * the same order: workflow_retry.sh, workflow_task.sh and the scans of
 * workflow_runtime.sh. */

static long now(void) { return (long)time(NULL); }

/* The shell driver's trap records a cancelling signal as cancel-requested
 * between commands; the driver checks the request at the same points. */
bool wdr_cancel_requested(struct wdr_run *run) {
    char path[F_PATH];
    if (wdr_join(path, run->dir, "cancel-requested")) return false;
    if (wdr_take_cancel_signal()) (void)wdr_scalar_long(path, now());
    return wdr_exists(path);
}

static bool same(const char *a, const char *b) { return strcmp(a, b) == 0; }

static const char *failure_class(const char *code) {
    long value;
    if (same(code, "unknown")) return "interrupted";
    if (same(code, "0")) return "success";
    if (same(code, "124")) return "timeout";
    if (same(code, "126") || same(code, "127")) return "configuration";
    return wdr_number(code, &value) && value <= 128 ? "failure" : "interrupted";
}

static bool retry_allowed(const struct wdr_policy *policy, const char *class) {
    char classes[WDR_VALUE], wanted[128];
    const char *listed = policy && policy->classes[0] ? policy->classes : "failure,timeout,interrupted,configuration";
    snprintf(classes, sizeof(classes), ",%s,", listed);
    snprintf(wanted, sizeof(wanted), ",%s,", class);
    return strstr(classes, wanted) != NULL;
}

static long backoff(const struct wdr_policy *policy, long attempt) {
    const char *text = policy && policy->delay[0] ? policy->delay : "0";
    long delay, power = 1;
    if (!wdr_number(text, &delay)) return 86400;
    while (power < attempt && delay < 86400) { delay *= 2; power++; }
    return delay > 86400 ? 86400 : delay;
}

/* workflow_attempt_result's retry branch: the retry time is persisted once,
 * so a restart does not reset the backoff clock. */
static int schedule_retry(struct wdr_run *run, size_t step, const char *attempt_dir, long attempt) {
    char dir[F_PATH], path[F_PATH], completed[F_PATH], finished[64];
    long delay = backoff(wdr_policy(run, run->steps[step].id), attempt), at;
    if (wdr_step_path(dir, run, step, attempt_dir) || wdr_join(path, dir, "retry-at") || wdr_join(completed, dir, "completed-at"))
        return -1;
    if (wdr_exists(path)) return 0;
    (void)wdr_first_line(completed, finished, sizeof(finished));
    at = (finished[0] ? wdr_shell_number(finished) : now()) + delay;
    return wdr_scalar_long(path, at);
}

static bool retryable(const struct wdr_run *run, size_t step, const char *class, long *attempt, const char *text) {
    long limit;
    const struct wdr_step *s = &run->steps[step];
    return same(s->idem, "true") && wdr_number(text, attempt) && wdr_number(s->retry, &limit) && *attempt <= limit &&
        retry_allowed(wdr_policy(run, s->id), class);
}

/* workflow_attempt_result: one durable decision for a finished attempt. */
int wdr_attempt_result(struct wdr_run *run, size_t step, const char *code) {
    char attempt[64], attempt_dir[96], path[F_PATH], detail[256];
    const char *class = failure_class(code), *state;
    long number = 0;
    wdr_step_line(run, step, "attempts", attempt, sizeof(attempt));
    (void)f_format(attempt_dir, sizeof(attempt_dir), "attempt-%s", attempt);
    (void)f_format(path, sizeof(path), "%s/failure-class", attempt_dir);
    if (wdr_step_scalar(run, step, path, class)) return 1;
    if (same(class, "success")) state = "succeeded";
    else if (retryable(run, step, class, &number, attempt)) {
        if (schedule_retry(run, step, attempt_dir, number)) return 1;
        state = "retrying";
    } else state = same(class, "interrupted") ? "recovery-required" : "failed";
    if ((same(state, "succeeded") || same(state, "failed")) && wdr_step_scalar(run, step, "authoritative-attempt", attempt)) return 1;
    if (wdr_step_scalar(run, step, "state", state)) return 1;
    (void)f_format(path, sizeof(path), "step.%s", state);
    (void)f_format(detail, sizeof(detail), "attempt=%s class=%s", attempt, class);
    return wdr_event(run, run->steps[step].id, path, detail);
}

/* Records each step in the state with one of its files read in the same scan
 * ("-" when it has none), as the shell's one-pass scans list them. */
static void scan(struct wdr_run *run, const char *state, const char *attempt_file) {
    size_t i;
    wdr_read_states(run);
    for (i = 0; i < run->count; i++) {
        struct wdr_step *step = &run->steps[i];
        char name[160], attempt[64];
        step->scratch[0] = '\0';
        if (!same(step->state, state)) continue;
        if (attempt_file) {
            wdr_step_line(run, i, "attempts", attempt, sizeof(attempt));
            snprintf(name, sizeof(name), "attempt-%s/%s", attempt, attempt_file);
        } else f_copy(name, sizeof(name), "worker-pid");
        wdr_step_line(run, i, name, step->scratch, sizeof(step->scratch));
        if (!step->scratch[0]) f_copy(step->scratch, sizeof(step->scratch), "-");
    }
}

static void recover_step(struct wdr_run *run, size_t step) {
    char state[64], attempt[64], name[96], code[64];
    wdr_step_line(run, step, "state", state, sizeof(state));
    if (!same(state, "running")) return;
    wdr_step_line(run, step, "attempts", attempt, sizeof(attempt));
    snprintf(name, sizeof(name), "attempt-%s/exit-code", attempt);
    wdr_step_line(run, step, name, code, sizeof(code));
    if (same(run->steps[step].kind, "task")) { (void)wdr_step_scalar(run, step, "state", "waiting-remote"); return; }
    (void)wdr_attempt_result(run, step, code[0] ? code : "unknown");
}

/* workflow_recover_running_steps: a running step whose worker is gone is read
 * again, then waits for its remote task or gets the result it recorded. */
int wdr_recover(struct wdr_run *run) {
    size_t i;
    scan(run, "running", NULL);
    for (i = 0; i < run->count; i++)
        if (run->steps[i].scratch[0] && !wdr_pid_alive(run, run->steps[i].scratch)) recover_step(run, i);
    return 0;
}

/* workflow_retry_ready: a retrying step whose recorded time has come is ready. */
int wdr_retry_ready(struct wdr_run *run) {
    size_t i;
    scan(run, "retrying", "retry-at");
    for (i = 0; i < run->count; i++) {
        long when;
        if (!run->steps[i].scratch[0]) continue;
        if (!wdr_number(run->steps[i].scratch, &when)) { (void)wdr_step_scalar(run, i, "state", "recovery-required"); continue; }
        if (now() >= when && wdr_step_scalar(run, i, "state", "ready")) return 1;
    }
    return 0;
}

static int dependency_verdict(const struct wdr_run *run, const struct wdr_step *step) {
    size_t j; int ready = 1;
    for (j = 0; j < step->dep_count; j++) {
        const char *state;
        if (step->deps[j] == -1) continue;
        state = step->deps[j] < 0 ? "" : run->steps[step->deps[j]].state;
        if (same(state, "succeeded")) continue;
        if (same(state, "failed") || same(state, "cancelled") || same(state, "recovery-required")) return -1;
        ready = 0;
    }
    return ready;
}

/* One pass in graph order; each decision is visible to the steps after it.
 * Decisions are step indexes, negated and offset by one for cancellations. */
static bool decide_pass(struct wdr_run *run, long *decisions, size_t *count) {
    size_t i; bool changed = false;
    for (i = 0; i < run->count; i++) {
        int verdict;
        if (!same(run->steps[i].state, "queued")) continue;
        verdict = dependency_verdict(run, &run->steps[i]);
        if (!verdict || *count >= run->count) continue;
        f_copy(run->steps[i].state, sizeof(run->steps[i].state), verdict < 0 ? "cancelled" : "ready");
        decisions[(*count)++] = verdict < 0 ? -(long)i - 1 : (long)i;
        changed = true;
    }
    return changed;
}

/* workflow_queued_decisions: passes until nothing changes. */
static size_t queued_decisions(struct wdr_run *run, long *decisions) {
    size_t count = 0;
    wdr_read_states(run);
    while (decide_pass(run, decisions, &count)) {}
    return count;
}

static void record_ready(struct wdr_run *run, size_t step) {
    char attempts[64];
    wdr_step_line(run, step, "attempts", attempts, sizeof(attempts));
    if (same(attempts, "0")) {
        char path[F_PATH];
        if (!wdr_step_path(path, run, step, "initial-ready-at")) (void)wdr_scalar_long(path, now());
    }
    (void)wdr_step_scalar(run, step, "state", "ready");
    (void)wdr_event(run, run->steps[step].id, "step.ready", "");
}

/* workflow_refresh_states */
void wdr_refresh(struct wdr_run *run) {
    long *decisions = calloc(run->count + 1, sizeof(*decisions));
    size_t count, i;
    if (!decisions) return;
    while ((count = queued_decisions(run, decisions)) > 0) {
        for (i = 0; i < count; i++) {
            if (decisions[i] >= 0) { record_ready(run, (size_t)decisions[i]); continue; }
            (void)wdr_step_scalar(run, (size_t)(-decisions[i] - 1), "state", "cancelled");
            (void)wdr_event(run, run->steps[-decisions[i] - 1].id, "step.cancelled", "dependency_failed");
        }
    }
    free(decisions);
}

static int start_worker(struct wdr_run *run, size_t step, const char *attempt) {
    char *argv[] = {(char *)f_hydra, (char *)"workflow", (char *)"--driver-hook", (char *)"step-worker",
        (char *)run->dir, run->steps[step].id, (char *)attempt, NULL};
    char pid[32]; pid_t worker = wdr_spawn(run, argv);
    if (worker < 0) return 1;
    snprintf(pid, sizeof(pid), "%ld", (long)worker);
    return wdr_step_scalar(run, step, "worker-pid", pid) ? 1 : 0;
}

static int attempt_mkdir(const struct wdr_run *run, size_t step, const char *attempt, char *dir) {
    char name[96];
    snprintf(name, sizeof(name), "attempt-%s", attempt);
    return wdr_step_path(dir, run, step, name) || wdr_private_mkdirs(dir) ? -1 : 0;
}

/* workflow_start_step: the driver's half; the worker does the rest. */
int wdr_start_step(struct wdr_run *run, size_t step) {
    char attempts[64], attempt[32], dir[F_PATH], path[F_PATH], detail[64], started[32];
    struct wdr_step *s = &run->steps[step];
    if (same(s->kind, "approval-wait")) return wdr_hook(run, NULL, 0, false, "approval-create", s->id, NULL);
    if (same(s->kind, "task")) return wdr_task_start(run, step);
    wdr_step_line(run, step, "attempts", attempts, sizeof(attempts));
    snprintf(attempt, sizeof(attempt), "%ld", wdr_shell_number(attempts) + 1);
    if (attempt_mkdir(run, step, attempt, dir)) return 1;
    (void)wdr_step_scalar(run, step, "attempts", attempt);
    (void)wdr_step_scalar(run, step, "state", "running");
    snprintf(started, sizeof(started), "%ld", now());
    (void)wdr_step_scalar(run, step, "started-at", started);
    if (same(attempt, "1")) (void)wdr_step_scalar(run, step, "initial-started-at", started);
    if (same(s->kind, "approve") && (wdr_join(path, dir, "decision-source") || wdr_scalar(path, "workflow-policy"))) return 1;
    snprintf(detail, sizeof(detail), "attempt=%s", attempt);
    (void)wdr_event(run, s->id, "step.running", detail);
    return start_worker(run, step, attempt);
}

static int task_started_at(struct wdr_run *run, size_t step, const char *dir, const char *number) {
    char path[F_PATH], remote[F_PATH], started[32];
    if (wdr_join(remote, dir, "remote") || wdr_join(path, dir, "started-at")) return -1;
    if (wdr_is_dir(remote) || wdr_exists(path)) return 0;
    snprintf(started, sizeof(started), "%ld", now());
    if (wdr_scalar(path, started) || wdr_step_scalar(run, step, "started-at", started)) return -1;
    return same(number, "1") ? wdr_step_scalar(run, step, "initial-started-at", started) : 0;
}

/* workflow_task_start: the driver's half for a remote task step. */
int wdr_task_start(struct wdr_run *run, size_t step) {
    char number[64], dir[F_PATH]; long value;
    wdr_step_line(run, step, "attempts", number, sizeof(number));
    if (!wdr_number(number, &value) || value <= 0) f_copy(number, sizeof(number), "1");
    if (attempt_mkdir(run, step, number, dir)) return 1;
    (void)wdr_step_scalar(run, step, "attempts", number);
    if (task_started_at(run, step, dir, number)) return 1;
    (void)wdr_step_scalar(run, step, "state", "running");
    (void)wdr_event(run, run->steps[step].id, "step.running", "task_reconciliation");
    return start_worker(run, step, number);
}

/* workflow_task_resume */
int wdr_task_resume(struct wdr_run *run) {
    size_t i;
    for (i = 0; i < run->task_count; i++) {
        char path[F_PATH], state[64]; long step;
        if (snprintf(path, sizeof(path), "%s/steps/%s/cancel-reconciled", run->dir, run->task_ids[i]) >= (int)sizeof(path)) return 1;
        (void)unlink(path);
        if (wdr_cancel_requested(run) || wdr_plan_expired(run)) continue;
        step = wdr_find(run, run->task_ids[i]);
        if (step < 0) continue;
        wdr_step_line(run, (size_t)step, "state", state, sizeof(state));
        if (same(state, "waiting-remote") && wdr_task_start(run, (size_t)step)) return 1;
    }
    return 0;
}

static bool task_step(const struct wdr_run *run, const char *id) {
    size_t i;
    for (i = 0; i < run->task_count; i++) if (same(run->task_ids[i], id)) return true;
    return false;
}

/* workflow_task_cancel: true when the task step needs nothing else. */
static bool task_cancel(struct wdr_run *run, const char *id, const char *sd, const char *state) {
    char path[F_PATH]; long step;
    if (!task_step(run, id)) return false;
    if (same(state, "running")) return true;
    if (!same(state, "waiting-remote")) return false;
    if (wdr_join(path, sd, "cancel-reconciled") || wdr_exists(path)) return true;
    (void)wdr_scalar(path, "1");
    step = wdr_find(run, id);
    return step >= 0 && !wdr_task_start(run, (size_t)step);
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

struct names { char **items; size_t count, capacity; };

static int add_name(struct names *names, const char *name) {
    if (names->count == names->capacity) {
        size_t capacity = names->capacity ? names->capacity * 2 : 64;
        char **grown = realloc(names->items, capacity * sizeof(char *));
        if (!grown) return -1;
        names->items = grown; names->capacity = capacity;
    }
    if (!(names->items[names->count] = strdup(name))) return -1;
    names->count++;
    return 0;
}

static bool child_dir(const char *parent, const char *name) {
    char child[F_PATH]; struct stat st;
    if (same(name, ".") || same(name, "..")) return false;
    return !wdr_join(child, parent, name) && !lstat(child, &st) && S_ISDIR(st.st_mode);
}

/* `find steps -mindepth 1 -maxdepth 1 -type d | LC_ALL=C sort` */
static void step_dirs(const struct wdr_run *run, struct names *names) {
    char path[F_PATH]; DIR *dir; struct dirent *entry;
    memset(names, 0, sizeof(*names));
    if (wdr_join(path, run->dir, "steps") || !(dir = opendir(path))) return;
    while ((entry = readdir(dir)))
        if (child_dir(path, entry->d_name) && add_name(names, entry->d_name)) break;
    closedir(dir);
    if (names->count) qsort(names->items, names->count, sizeof(char *), by_name);
}

static int cancel_running(struct wdr_run *run, const char *id, const char *sd, int residual) {
    char path[F_PATH], worker[64], command[64], line[256]; long pid;
    if (!wdr_join(path, sd, "worker-pid")) (void)wdr_first_line(path, worker, sizeof(worker));
    if (!wdr_join(path, sd, "command-pid")) (void)wdr_first_line(path, command, sizeof(command));
    if (wdr_pid_alive(run, command) && wdr_number(command, &pid)) wdr_signal_tree((pid_t)pid, SIGTERM);
    wdr_reap(run);
    if (!wdr_pid_alive(run, worker)) return 0;
    snprintf(line, sizeof(line), "%s\t%s\t%s\n", id, worker, command);
    /* A line that cannot be written whole is cut off again: the file lists
     * whole lines or fewer, and the caller is told it is short. */
    return f_append(residual, line, strlen(line), false);
}

static int cancel_one(struct wdr_run *run, const char *id, int residual) {
    char sd[F_PATH], path[F_PATH], state[64];
    if (snprintf(sd, sizeof(sd), "%s/steps/%s", run->dir, id) >= (int)sizeof(sd) || wdr_join(path, sd, "state")) return 0;
    (void)wdr_first_line(path, state, sizeof(state));
    if (task_cancel(run, id, sd, state)) return 0;
    if (same(state, "queued") || same(state, "ready") || same(state, "retrying") || same(state, "waiting-approval")) {
        (void)wdr_scalar(path, "cancelled");
        (void)wdr_event(run, id, "step.cancelled", "request");
    } else if (same(state, "running")) return cancel_running(run, id, sd, residual);
    return 0;
}

/* workflow_cancel_steps: every step directory in name order; running steps
 * whose worker outlives the request are listed in residual-children.tsv.
 * 1 when the list could not be opened or a line of it could not be written;
 * the driver cancels again on its next poll and rewrites the list. */
int wdr_cancel_steps(struct wdr_run *run) {
    char path[F_PATH]; struct names names; size_t i; int residual, status = 0;
    if (wdr_join(path, run->dir, "residual-children.tsv") || wdr_private_touch(path)) return 1;
    if ((residual = open(path, O_WRONLY | O_TRUNC | O_APPEND)) < 0) return 1;
    step_dirs(run, &names);
    for (i = 0; i < names.count; i++) { if (cancel_one(run, names.items[i], residual)) status = 1; free(names.items[i]); }
    free(names.items);
    if (close(residual)) status = 1;
    return status;
}

/* workflow_waiting_state: the run waits for approval or remote work when
 * nothing else can progress. */
bool wdr_waiting_state(struct wdr_run *run) {
    long approval = 0, remote = 0, active = 0, ready = 0, parallelism; size_t i;
    char text[64]; const char *state;
    wdr_read_states(run);
    for (i = 0; i < run->count; i++) {
        const char *s = run->steps[i].state;
        if (same(s, "waiting-approval")) approval++;
        else if (same(s, "waiting-remote")) remote++;
        else if (same(s, "running") || same(s, "retrying")) active++;
        else if (same(s, "ready")) ready++;
    }
    if (approval + remote == 0 || active) return false;
    if (ready > 0) {
        wdr_run_line(run, "parallelism", text, sizeof(text));
        if (!wdr_number(text, &parallelism) || remote < parallelism) return false;
    }
    state = remote ? "waiting-remote" : "waiting-approval";
    (void)wdr_run_scalar(run, "state", state);
    return wdr_event(run, NULL, remote ? "run.waiting_remote" : "run.waiting_approval", "") == 0;
}

static bool repository_root(struct wdr_run *run) {
    char *argv[] = {(char *)"git", (char *)"rev-parse", (char *)"--show-toplevel", NULL};
    if (run->root[0]) return true;
    if (wdr_capture(argv, run->root, sizeof(run->root))) run->root[0] = '\0';
    return run->root[0] != '\0';
}

/* workflow_disk_available: free space in the repository, as df -Pk reports. */
bool wdr_disk_available(struct wdr_run *run) {
    char required[64], detail[96]; struct statvfs fs; unsigned long long kb;
    wdr_run_line(run, "disk-mb", required, sizeof(required));
    if (!repository_root(run) || statvfs(run->root, &fs)) return false;
    kb = ((unsigned long long)fs.f_bavail * (unsigned long long)fs.f_frsize + 1023ULL) / 1024ULL;
    if (kb >= (unsigned long long)wdr_shell_number(required) * 1024ULL) return true;
    snprintf(detail, sizeof(detail), "disk_mb=%s", required);
    (void)wdr_event(run, NULL, "run.resource_refused", detail);
    return false;
}

/* workflow_plan_expired */
bool wdr_plan_expired(const struct wdr_run *run) {
    char deadline[64]; long value;
    if (!wdr_run_has(run, "compiled.json")) return false;
    wdr_run_line(run, "plan-deadline", deadline, sizeof(deadline));
    return !wdr_number(deadline, &value) || now() >= value;
}
