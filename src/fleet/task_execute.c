#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec; }
static int phase(char *const argv[], int64_t deadline, struct f_capture *cap, struct f_control *control) {
    int64_t left = deadline - monotonic();
    if (left <= 0) { cap->timeout = true; cap->status = 124; return -1; }
    return f_run_controlled(argv, (unsigned)left, cap, control) || cap->status ? -1 : 0;
}
static int step(char *const argv[], int64_t deadline, struct f_control *control) {
    struct f_capture cap = {0}; int status = phase(argv, deadline, &cap, control); f_capture_free(&cap); return status;
}
static int inputs(const char *directory, json_object *package) {
    char root[F_PATH]; json_object *manifest = f_field(f_field(package, "spec"), "inputs"), *payload = f_field(package, "input_hex"); size_t i;
    if (f_path(root, sizeof(root), directory, "inputs") || mkdir(root, 0700) || setenv("HYDRA_TASK_INPUT_DIR", root, 1)) return -1;
    for (i = 0; i < json_object_array_length(manifest); i++) {
        const char *relative = f_string(json_object_array_get_idx(manifest, i), "path"); char path[F_PATH], parent[F_PATH];
        if (!task_path(relative) || f_path(path, sizeof(path), root, relative) || f_copy(parent, sizeof(parent), path)) return -1;
        *strrchr(parent, '/') = '\0';
        if (f_mkdirs(parent) || f_hex_write(path, task_text(json_object_array_get_idx(payload, i)), 0600)) return -1;
    }
    return 0;
}
static int prepare(const char *directory, json_object *package, json_object *state, int64_t deadline, struct f_control *control) {
    char bundle[F_PATH], workspace[F_PATH], heads[F_PATH]; const char *commit = f_string(f_field(f_field(package, "spec"), "source"), "commit");
    char *clone[] = {"git", "-c", "core.hooksPath=/dev/null", "clone", "--no-checkout", "--template=", "--", bundle, workspace, NULL};
    char *checkout[] = {"git", "-c", "core.hooksPath=/dev/null", "checkout", "--detach", (char *)commit, NULL};
    char *init[] = {(char *)f_hydra, "init", "--no-agent", "--trust", "--worktree-root", heads, "--json", NULL};
    if (f_path(bundle, sizeof(bundle), directory, "source.bundle") || f_path(workspace, sizeof(workspace), directory, "workspace") ||
        f_path(heads, sizeof(heads), directory, "heads") || f_hex_write(bundle, f_string(package, "bundle_hex"), 0600) ||
        inputs(directory, package) || step(clone, deadline, control) || chdir(workspace) || step(checkout, deadline, control) || step(init, deadline, control)) return -1;
    f_string_add(state, "workspace", workspace);
    {
        char *project = f_read(".git/hydra/project-id", 128);
        if (!project) return -1;
        project[strcspn(project, "\r\n")] = '\0';
        if (!f_name(project) || strncmp(project, "project_", 8)) { free(project); return -1; }
        f_string_add(state, "execution_project_id", project); free(project);
    }
    return task_write_json(directory, "state.json", state, true);
}
void task_execute(const char *directory, json_object *package, json_object *state) {
    json_object *spec = f_field(package, "spec"), *work = f_field(spec, "work"), *limits = f_field(spec, "limits"), *evidence = NULL;
    struct f_capture cap = {0}; char bytes[32], home[F_PATH]; char *argv[140]; size_t n = 0, i;
    struct task_control control;
    const char *clear[] = {"HYDRA_PROJECT_ID", "HYDRA_HEAD_ID", "HYDRA_INSTANCE_ID", "HYDRA_STATE_DIR", "HYDRA_BRANCH", "HYDRA_WORKTREE", NULL};
    int64_t startup = monotonic() + json_object_get_int64(f_field(limits, "startup_seconds"));
    bool complete_response = false;
    bool command = !strcmp(f_string(work, "kind"), "exec");
    char *global = getenv("GIT_CONFIG_GLOBAL") ? strdup(getenv("GIT_CONFIG_GLOBAL")) : NULL;
    char *system = getenv("GIT_CONFIG_NOSYSTEM") ? strdup(getenv("GIT_CONFIG_NOSYSTEM")) : NULL;
    f_string_add(state, "work_kind", f_string(work, "kind"));
    if (task_control_open(&control, directory, f_string(package, "spec_sha256"), state, limits)) { f_string_add(state, "failure", "log_io_failed"); goto failed; }
    if (f_copy(home, sizeof(home), f_home)) goto failed;
    f_home = home;
    for (i = 0; clear[i]; i++) unsetenv(clear[i]);
    setenv("HYDRA_HOME", f_home, 1); setenv("HYDRA_NONINTERACTIVE", "1", 1);
    setenv("GIT_CONFIG_NOSYSTEM", "1", 1); setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1); setenv("GIT_TERMINAL_PROMPT", "0", 1);
    snprintf(bytes, sizeof(bytes), "%d", json_object_get_int(f_field(limits, "log_bytes"))); setenv("HYDRA_EXEC_MAX_BYTES", bytes, 1);
    json_object_object_add(state, "owner_pid", json_object_new_int64((int64_t)getpid()));
    f_string_add(state, "launch_intent", "started");
    if (task_write_json(directory, "state.json", state, true)) goto done;
    if (prepare(directory, package, state, startup, &control.process)) { f_string_add(state, "failure", monotonic() >= startup ? "startup_deadline" : "startup_failed"); goto failed; }
    if (global) setenv("GIT_CONFIG_GLOBAL", global, 1); else unsetenv("GIT_CONFIG_GLOBAL");
    if (system) setenv("GIT_CONFIG_NOSYSTEM", system, 1); else unsetenv("GIT_CONFIG_NOSYSTEM");
    if (command) {
        char *spawn[] = {(char *)f_hydra, "spawn", "task", "--no-agent", NULL};
        char *provenance[] = {(char *)f_hydra, "provenance", "task", "--json", NULL};
        if (step(spawn, startup, &control.process) || phase(provenance, startup, &cap, &control.process)) { f_string_add(state, "failure", monotonic() >= startup ? "startup_deadline" : "startup_failed"); goto failed; }
        evidence = f_parse(cap.out);
        if (!evidence || !json_object_get_boolean(f_field(evidence, "ok")) || task_write_json(directory, "provenance.json", evidence, false)) goto failed;
        if (!f_name(f_string(f_field(evidence, "data"), "head_id"))) goto failed;
        f_string_add(state, "execution_head_id", f_string(f_field(evidence, "data"), "head_id"));
        json_object_put(evidence); evidence = NULL; f_capture_free(&cap);
    }
    f_string_add(state, "state", "running"); json_object_object_add(state, "started_at", json_object_new_int64((int64_t)time(NULL)));
    if (task_write_json(directory, "state.json", state, true)) goto done;
    argv[n++] = (char *)f_hydra;
    if (command) {
        json_object *args = f_field(work, "argv");
        argv[n++] = "exec"; argv[n++] = "--branch"; argv[n++] = "task"; argv[n++] = "--json";
        argv[n++] = "--timeout"; argv[n++] = "0"; argv[n++] = "--";
        for (i = 0; i < json_object_array_length(args); i++) argv[n++] = (char *)task_text(json_object_array_get_idx(args, i));
    } else { argv[n++] = "workflow"; argv[n++] = "run"; argv[n++] = (char *)f_string(work, "path"); }
    argv[n] = NULL;
    (void)phase(argv, monotonic() + json_object_get_int64(f_field(limits, "execution_seconds")), &cap, &control.process);
    json_object_object_add(state, "exit_status", json_object_new_int(cap.status));
    if (command && cap.out) {
        evidence = f_parse(cap.out);
        if (f_number_is(evidence, "schema_version", 1) && json_object_get_boolean(f_field(evidence, "ok")) &&
            f_name(f_string(f_field(evidence, "data"), "run_id")) && json_object_is_type(f_field(f_field(evidence, "data"), "results"), json_type_array)) {
            complete_response = true;
            f_string_add(state, "run_id", f_string(f_field(evidence, "data"), "run_id"));
            if (task_write_json(directory, "attempt.json", evidence, false)) goto failed;
        }
    } else if (cap.out) {
        char run[128]; size_t length = strcspn(cap.out, "\r\n");
        if (length < sizeof(run)) {
            memcpy(run, cap.out, length); run[length] = '\0';
            if (!strncmp(run, "run_", 4) && f_name(run)) f_string_add(state, "run_id", run);
        }
    }
    if (cap.status == 125 || (!cap.status && (!f_string(state, "run_id") || (command && !complete_response)))) {
        f_string_add(state, "state", "outcome_unknown"); f_string_add(state, "failure", "execution_evidence_unavailable"); goto done;
    }
    if (cap.status) { f_string_add(state, "failure", cap.timeout ? "execution_deadline" : "execution_failed"); goto failed; }
    f_string_add(state, "state", "succeeded"); goto done;
failed:
    f_string_add(state, "state", "failed");
    if (!f_string(state, "failure")) f_string_add(state, "failure", "evidence_unavailable");
done:
    task_control_close(&control);
    json_object_object_add(state, "finished_at", json_object_new_int64((int64_t)time(NULL)));
    (void)task_write_json(directory, "state.json", state, true);
    free(global); free(system); json_object_put(evidence); f_capture_free(&cap);
}
