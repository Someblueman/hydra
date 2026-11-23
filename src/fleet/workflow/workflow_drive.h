#ifndef HYDRA_FLEET_WORKFLOW_DRIVE_H
#define HYDRA_FLEET_WORKFLOW_DRIVE_H
#include "fleet/fleet.h"
#include <sys/types.h>

/* The native workflow run driver (`hydra-fleet workflow-drive <run-dir>`).
 * It is the only run driver. It drives a run whose drive lock the shell
 * (workflow_drive in lib/workflow_runtime.sh) already holds: files written by
 * atomic rename with private modes, and events under the .events.lock
 * protocol that shell writers share. It also drives runs that the shell driver
 * of earlier releases started, which left the same files. Step workers and
 * the rarer parts of a drive stay shell processes ("hooks"). */

#define WDR_VALUE 4096
#define WDR_MAX_DEPS 128

struct wdr_step {
    char *id, *kind, *needs, *retry, *idem;
    bool task;
    int deps[WDR_MAX_DEPS];
    size_t dep_count;
    char state[64];
    char scratch[64];
};

struct wdr_policy { char *id, *classes, *delay; };

struct wdr_child { pid_t pid; bool exited; };

/* How much of events.jsonl this driver has counted: see next_sequence. */
struct wdr_events { bool counted; dev_t dev; ino_t ino; off_t size; long lines; char last; };

struct wdr_run {
    const char *dir;
    char *graph;
    struct wdr_step *steps;
    size_t count;
    struct wdr_policy *policies;
    size_t policy_count;
    bool has_tasks;
    char **task_ids;
    size_t task_count;
    long parallelism;
    struct wdr_child *children;
    size_t child_count, child_capacity;
    bool woken;
    char root[F_PATH];
    struct wdr_events events;
};

/* workflow_drive_io.c: run files, scalars and events. */
int wdr_join(char *out, const char *dir, const char *name);
int wdr_step_path(char *out, const struct wdr_run *run, size_t step, const char *name);
int wdr_first_line(const char *path, char *out, size_t size);
bool wdr_exists(const char *path);
bool wdr_is_dir(const char *path);
int wdr_private_mkdirs(const char *dir);
int wdr_private_touch(const char *path);
int wdr_scalar(const char *path, const char *value);
int wdr_scalar_long(const char *path, long value);
int wdr_run_scalar(const struct wdr_run *run, const char *name, const char *value);
int wdr_step_scalar(const struct wdr_run *run, size_t step, const char *name, const char *value);
void wdr_step_line(const struct wdr_run *run, size_t step, const char *name, char *out, size_t size);
void wdr_run_line(const struct wdr_run *run, const char *name, char *out, size_t size);
bool wdr_run_has(const struct wdr_run *run, const char *name);
int wdr_event(struct wdr_run *run, const char *step, const char *type, const char *detail);
bool wdr_number(const char *text, long *value);
long wdr_shell_number(const char *text);
int wdr_remove_tree(const char *path);

/* workflow_drive_proc.c: signals, children, hooks and process liveness. */
int wdr_signals_init(void);
void wdr_cancel_traps(void);
bool wdr_take_cancel_signal(void);
pid_t wdr_spawn(struct wdr_run *run, char *const argv[]);
int wdr_hook(struct wdr_run *run, char *out, size_t size, bool quiet, const char *operation, const char *a, const char *b);
int wdr_capture(char *const argv[], char *out, size_t size);
int wdr_fork_next(struct wdr_run *run, char *out, size_t size);
void wdr_reap(struct wdr_run *run);
bool wdr_pid_alive(struct wdr_run *run, const char *pid_text);
void wdr_signal_tree(pid_t pid, int signal_number);
bool wdr_zombie(pid_t pid);
void wdr_sleep(struct wdr_run *run);

/* workflow_drive_graph.c: the static graph and step-state scans. */
int wdr_load(struct wdr_run *run, const char *dir);
void wdr_free(struct wdr_run *run);
void wdr_read_states(struct wdr_run *run);
long wdr_count(struct wdr_run *run, const char *states, const char *kind);
const struct wdr_policy *wdr_policy(const struct wdr_run *run, const char *id);
long wdr_find(const struct wdr_run *run, const char *id);

/* workflow_drive_steps.c: transitions of steps, as the shell records them. */
int wdr_attempt_result(struct wdr_run *run, size_t step, const char *code);
int wdr_recover(struct wdr_run *run);
int wdr_retry_ready(struct wdr_run *run);
void wdr_refresh(struct wdr_run *run);
int wdr_start_step(struct wdr_run *run, size_t step);
int wdr_task_start(struct wdr_run *run, size_t step);
int wdr_task_resume(struct wdr_run *run);
int wdr_cancel_steps(struct wdr_run *run);
bool wdr_waiting_state(struct wdr_run *run);
bool wdr_disk_available(struct wdr_run *run);
bool wdr_plan_expired(const struct wdr_run *run);
bool wdr_cancel_requested(struct wdr_run *run);

/* workflow_drive.c */
int wdr_main(int argc, char **argv);
#endif
