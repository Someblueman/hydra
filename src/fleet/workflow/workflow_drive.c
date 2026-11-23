#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_drive.h"
#include "fleet/support/files.h"
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The loop of the run driver after the shell (workflow_drive in
 * lib/workflow_runtime.sh) has taken the run's drive lock and recorded the
 * statistics start. It is the only run driver; the shell driver of earlier
 * releases was removed. Each poll sleeps until a worker exits or a second
 * passes. */

#define CONTINUE (-1)

static long now(void) { return (long)time(NULL); }

static int heartbeat(const struct wdr_run *run) {
    char path[F_PATH];
    return wdr_join(path, run->dir, "heartbeat-at") || wdr_scalar_long(path, now()) ? 1 : 0;
}

static void release(const struct wdr_run *run) {
    char path[F_PATH];
    if (!wdr_join(path, run->dir, ".drive.lock")) (void)wdr_remove_tree(path);
}

/* workflow_plan_repair: runs that may not repair never call it. */
static int repair(struct wdr_run *run, const char *mode, char *out, size_t size) {
    char budget[64];
    wdr_run_line(run, "plan-repair-budget", budget, sizeof(budget));
    if (out) f_copy(out, size, "0");
    if (!wdr_run_has(run, "compiled.json") || !strcmp(budget, "0")) return 0;
    return wdr_hook(run, out, size, out == NULL, "repair", mode, NULL) ? 1 : 0;
}

/* workflow_approval_resume changes nothing unless a step waits for approval. */
static bool approval_waiting(struct wdr_run *run) {
    size_t i;
    wdr_read_states(run);
    for (i = 0; i < run->count; i++)
        if (!strcmp(run->steps[i].kind, "approval-wait") && !strcmp(run->steps[i].state, "waiting-approval")) return true;
    return false;
}

static int prologue(struct wdr_run *run) {
    char pid[32];
    snprintf(pid, sizeof(pid), "%ld", (long)getpid());
    if (wdr_run_scalar(run, "owner-pid", pid) || heartbeat(run) || wdr_run_scalar(run, "state", "running")) return 1;
    /* Each drive records which driver it is; the removed shell driver recorded driver=shell. */
    (void)wdr_event(run, NULL, "run.running", "driver=native");
    if (!wdr_cancel_requested(run) && approval_waiting(run) &&
        wdr_hook(run, NULL, 0, false, "approval-resume", NULL, NULL)) {
        (void)wdr_run_scalar(run, "state", "recovery-required");
        release(run);
        return 1;
    }
    if (repair(run, "repair-resume", NULL, 0) || wdr_recover(run) || wdr_task_resume(run)) return 1;
    return 0;
}

static void fail_step(struct wdr_run *run, const char *id, const char *reason) {
    char path[F_PATH];
    if (snprintf(path, sizeof(path), "%s/steps/%s/state", run->dir, id) < (int)sizeof(path)) (void)wdr_scalar(path, "failed");
    (void)wdr_event(run, id, "step.failed", reason);
}

/* workflow_next_step: the first ready step in graph order, or the bound
 * coordinator's decision for a run of remote tasks. */
static int next_step(struct wdr_run *run, char *id, size_t size) {
    size_t i;
    id[0] = '\0';
    if (wdr_run_has(run, "tasks.json")) return wdr_fork_next(run, id, size);
    wdr_read_states(run);
    for (i = 0; i < run->count; i++)
        if (!strcmp(run->steps[i].state, "ready")) return f_copy(id, size, run->steps[i].id) ? 1 : 0;
    return 0;
}

/* One step start, or CONTINUE with *stop when no further step may start. */
static int start_next(struct wdr_run *run, bool *stop) {
    char id[256]; long step;
    *stop = true;
    if (next_step(run, id, sizeof(id))) {
        (void)wdr_run_scalar(run, "state", "recovery-required");
        release(run);
        return 1;
    }
    if (!id[0]) return CONTINUE;
    step = wdr_find(run, id);
    if (step >= 0 && !strcmp(run->steps[step].kind, "spawn") && wdr_count(run, "running", "spawn") != 0) return CONTINUE;
    if (!wdr_disk_available(run)) { fail_step(run, id, "disk_safeguard"); return CONTINUE; }
    if (step < 0 || wdr_start_step(run, (size_t)step)) { fail_step(run, id, "launch_error"); return CONTINUE; }
    *stop = false;
    return CONTINUE;
}

static int schedule(struct wdr_run *run, long active) {
    long slots = run->parallelism - active;
    while (slots > 0) {
        bool stop; int status = start_next(run, &stop);
        if (status != CONTINUE || stop) return status;
        slots--;
    }
    return CONTINUE;
}

static const char *final_state(struct wdr_run *run) {
    if (wdr_count(run, "recovery-required", NULL) > 0) return "recovery-required";
    if (wdr_count(run, "failed", NULL) > 0) return "failed";
    return wdr_cancel_requested(run) ? "cancelled" : "succeeded";
}

/* The terminal state: published first, then its event; retiring verifier
 * heads is reported in the run record and never changes it. */
static int finish(struct wdr_run *run) {
    char repaired[64], event[96]; const char *state; bool ended;
    if (repair(run, "repair", repaired, sizeof(repaired))) return 1;
    if (!strcmp(repaired, "1")) { (void)wdr_event(run, NULL, "run.repair", "new_candidate_round"); return CONTINUE; }
    if (wdr_cancel_requested(run)) (void)wdr_cancel_steps(run);
    state = final_state(run);
    if (!strcmp(state, "succeeded") && wdr_run_has(run, "compiled.json") && wdr_hook(run, NULL, 0, false, "finish", NULL, NULL)) {
        state = "failed";
        (void)wdr_event(run, NULL, "run.delivery_rejected", "missing_or_negative_verification");
    }
    ended = strcmp(state, "recovery-required") != 0;
    if (ended) {
        char path[F_PATH];
        if (!wdr_join(path, run->dir, "completed-at")) (void)wdr_scalar_long(path, now());
    }
    (void)wdr_run_scalar(run, "state", state);
    snprintf(event, sizeof(event), "run.%s", state);
    (void)wdr_event(run, NULL, event, "");
    if (ended && wdr_run_has(run, "compiled.json")) (void)wdr_hook(run, NULL, 0, false, "retire", NULL, NULL);
    release(run);
    return strcmp(state, "succeeded") ? 1 : 0;
}

/* A cancellation whose workers outlive it for five seconds needs recovery. */
static bool residual_expired(struct wdr_run *run) {
    char path[F_PATH], started[64]; struct stat st;
    if (!wdr_cancel_requested(run) || wdr_join(path, run->dir, "residual-children.tsv") || stat(path, &st) || st.st_size == 0)
        return false;
    wdr_run_line(run, "cancel-started-at", started, sizeof(started));
    return now() - wdr_shell_number(started) >= 5;
}

static void request_cancel_on_deadline(struct wdr_run *run) {
    char path[F_PATH];
    if (!wdr_plan_expired(run) || wdr_cancel_requested(run) || wdr_join(path, run->dir, "cancel-requested")) return;
    (void)wdr_scalar_long(path, now());
    (void)wdr_event(run, NULL, "run.budget_exhausted", "plan_deadline");
}

static void advance(struct wdr_run *run) {
    (void)wdr_recover(run);
    if (wdr_cancel_requested(run)) {
        char path[F_PATH];
        if (!wdr_join(path, run->dir, "cancel-started-at") && !wdr_exists(path)) (void)wdr_scalar_long(path, now());
        (void)wdr_cancel_steps(run);
    } else {
        (void)wdr_retry_ready(run);
        wdr_refresh(run);
    }
}

static int poll_once(struct wdr_run *run) {
    long active, nonterminal; int status;
    run->woken = false;
    (void)heartbeat(run);
    request_cancel_on_deadline(run);
    advance(run);
    active = wdr_count(run, "running", NULL);
    if (!wdr_cancel_requested(run) && (status = schedule(run, active)) != CONTINUE) return status;
    /* Count active work first so a concurrent waiting-remote publication cannot look terminal. */
    nonterminal = wdr_count(run, "queued ready running retrying", NULL);
    if (wdr_waiting_state(run)) { release(run); return 3; }
    if (nonterminal == 0) return finish(run);
    if (residual_expired(run)) {
        (void)wdr_run_scalar(run, "state", "recovery-required");
        (void)wdr_event(run, NULL, "run.recovery_required", "residual_children");
        release(run);
        return 1;
    }
    if (!run->woken) wdr_sleep(run);
    return CONTINUE;
}

static int drive(struct wdr_run *run) {
    char parallelism[64]; int status;
    if (prologue(run)) return 1;
    wdr_cancel_traps();
    wdr_run_line(run, "parallelism", parallelism, sizeof(parallelism));
    run->parallelism = wdr_shell_number(parallelism);
    while ((status = poll_once(run)) == CONTINUE) {}
    return status;
}

/* hydra-fleet workflow-drive <run-dir> | --capability */
int wdr_main(int argc, char **argv) {
    struct wdr_run run; int status;
    if (argc == 1 && !strcmp(argv[0], "--capability")) { puts("workflow-drive 1"); return 0; }
    if (argc != 1 || !argv[0][0]) { fputs("usage: hydra-fleet workflow-drive <run-dir>\n", stderr); return 2; }
    if (wdr_signals_init()) return 1;
    if (wdr_load(&run, argv[0])) {
        wdr_free(&run);
        return 1;
    }
    status = drive(&run);
    wdr_free(&run);
    return status;
}
