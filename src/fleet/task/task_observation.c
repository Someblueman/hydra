#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * This module is deliberately a projection of receiver-owned records.  It
 * never probes a coordinator, treats a transport as execution evidence, or
 * exposes the detached owner's PID as an authority.  The projection is
 * bounded so a hostile task directory cannot turn a read request into an
 * unbounded response.
 */
#define OBSERVATION_TASKS 512U
#define OBSERVATION_STEPS 512U
#define OBSERVATION_REQUESTS 32U
#define OBSERVATION_GRAPH_LIMIT (256U * 1024U)

static int object_id_order(const void *left, const void *right) {
    const char *a = f_string(*(json_object *const *)left, "request_id");
    const char *b = f_string(*(json_object *const *)right, "request_id");
    return strcmp(a ? a : "", b ? b : "");
}

static int task_order(const void *left, const void *right) {
    const char *a = f_string(*(json_object *const *)left, "task_id");
    const char *b = f_string(*(json_object *const *)right, "task_id");
    return strcmp(a ? a : "", b ? b : "");
}

static void nullable(json_object *object, const char *key, const char *value) {
    if (value) f_string_add(object, key, value);
    else json_object_object_add(object, key, json_object_new_null());
}

static bool task_id_valid(const char *id) {
    return id && !strncmp(id, "task_", 5) && task_hex(id + 5, 64);
}

static bool safe_text(const char *value, size_t limit) {
    const unsigned char *p;
    if (!value || strlen(value) >= limit) return false;
    for (p = (const unsigned char *)value; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}

static char *scalar(const char *directory, const char *name) {
    char path[F_PATH], *value;
    if (f_path(path, sizeof(path), directory, name) || !(value = f_read(path, 256))) return NULL;
    value[strcspn(value, "\r\n")] = '\0';
    if (!safe_text(value, 128) || !value[0]) { free(value); return NULL; }
    return value;
}

static bool private_regular(const char *path) {
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022);
}

static void nullable_timestamp(json_object *object, const char *key, json_object *value) {
    if (json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0)
        json_object_object_add(object, key, json_object_get(value));
    else
        json_object_object_add(object, key, json_object_new_null());
}

static void nullable_now(json_object *object, const char *key, int64_t now) {
    if (now >= 0) json_object_object_add(object, key, json_object_new_int64(now));
    else json_object_object_add(object, key, json_object_new_null());
}

static json_object *observation_timestamps(json_object *accepted, json_object *state, int64_t now) {
    json_object *times = json_object_new_object();
    nullable_timestamp(times, "accepted_at", f_field(accepted, "accepted_at"));
    nullable_timestamp(times, "started_at", f_field(state, "started_at"));
    nullable_timestamp(times, "resumed_at", f_field(state, "resumed_at"));
    nullable_timestamp(times, "finished_at", f_field(state, "finished_at"));
    nullable_now(times, "receiver_observed_at", now);
    return times;
}

static void waiting(json_object *object, json_object *state, const char *task_id) {
    const char *name = f_string(state, "state"), *failure = f_string(state, "failure"), *admission_state = f_string(f_field(state, "admission"), "state");
    const char *reason = "none", *detail = "execution state is recorded; no receiver wait is active";
    char action[256]; json_object *value = json_object_new_object();
    (void)snprintf(action, sizeof(action), "inspect hydra fleet overview --json for %s", task_id);
    if (!name) {
        reason = "reconciliation"; detail = "receiver state is missing or malformed";
        (void)snprintf(action, sizeof(action), "reconcile task %s before any retry", task_id);
    } else if (!strcmp(name, "accepted")) {
        reason = "admission"; detail = "task is accepted but has no launch claim";
        (void)snprintf(action, sizeof(action), "review the specification, then start task %s with its exact trust digest", task_id);
    } else if (admission_state && !strcmp(admission_state, "queued")) {
        reason = "admission"; detail = "receiver admission is queued for capacity";
        (void)snprintf(action, sizeof(action), "wait for the receiver admission claim for %s", task_id);
    } else if (!strcmp(name, "waiting_approval")) {
        reason = "approval"; detail = "workflow execution is suspended for an approval decision";
        (void)snprintf(action, sizeof(action), "inspect and decide the pending approval for %s", task_id);
    } else if (!strcmp(name, "outcome_unknown") || (failure &&
               (strstr(failure, "reconcil") || strstr(failure, "owner_unavailable")))) {
        reason = "reconciliation"; detail = "the receiver retained an uncertain outcome";
        (void)snprintf(action, sizeof(action), "reconnect and reconcile %s; do not replay it", task_id);
    } else if (failure && (strstr(failure, "auth") || strstr(failure, "credential"))) {
        reason = "authentication"; detail = "receiver authentication or provider credentials need attention";
        (void)snprintf(action, sizeof(action), "inspect receiver authentication before resuming %s", task_id);
    } else if (failure && (strstr(failure, "dependency") || strstr(failure, "startup"))) {
        reason = "dependency"; detail = "a required receiver dependency or startup step is unavailable";
        (void)snprintf(action, sizeof(action), "inspect receiver capabilities and dependency diagnostics for %s", task_id);
    } else if (name && (!strcmp(name, "starting") || !strcmp(name, "running"))) {
        detail = "receiver owner has recorded active execution";
        (void)snprintf(action, sizeof(action), "inspect bounded logs for %s", task_id);
    } else if (name && (!strcmp(name, "failed") || !strcmp(name, "expired"))) {
        detail = failure ? failure : "receiver recorded a terminal failure";
        (void)snprintf(action, sizeof(action), "inspect logs and retained evidence for %s", task_id);
    } else if (name && (!strcmp(name, "succeeded") || !strcmp(name, "cancelled"))) {
        detail = "receiver recorded a terminal execution state";
        (void)snprintf(action, sizeof(action), "inspect the retained result snapshot for %s", task_id);
    }
    f_string_add(value, "reason", reason); f_string_add(value, "detail", detail); f_string_add(value, "next_action", action);
    json_object_object_add(object, "waiting", value);
}

static json_object *effective_configuration(json_object *spec) {
    const char *const fields[] = {"host", "project", "source", "work", "completion", "capabilities", "limits", NULL};
    json_object *configuration = json_object_new_object(); size_t i;
    for (i = 0; fields[i]; i++) {
        json_object *value = f_field(spec, fields[i]);
        if (value) json_object_object_add(configuration, fields[i], json_object_get(value));
    }
    return configuration;
}

static json_object *owner(json_object *state) {
    json_object *result = json_object_new_object(); const char *name = f_string(state, "state");
    const char *recorded = f_string(state, "recorded_state"), *failure = f_string(state, "failure");
    const char *owner_state = "none";
    if (name && (!strcmp(name, "starting") || !strcmp(name, "running") || !strcmp(name, "waiting_approval")))
        owner_state = "recorded";
    else if (recorded && failure && !strcmp(failure, "owner_unavailable")) owner_state = "unavailable";
    f_string_add(result, "kind", "detached-owner"); f_string_add(result, "state", owner_state);
    nullable(result, "recorded_state", recorded); nullable(result, "failure", failure);
    return result;
}

static int add_pending(json_object *task, const char *project, const char *run) {
    char root[F_PATH], approvals[F_PATH]; DIR *dir = NULL; struct dirent *entry; size_t count = 0;
    json_object *requests = json_object_new_array();
    if (!project || !run || !f_name(project) || !f_name(run) ||
        snprintf(root, sizeof(root), "%s/state/v2/projects/%s/workflows/runs/%s", f_home, project, run) >= (int)sizeof(root) ||
        f_path(approvals, sizeof(approvals), root, "approvals") || !(dir = opendir(approvals))) {
        json_object_object_add(task, "pending_requests", requests); return 0;
    }
    while ((entry = readdir(dir)) && count < OBSERVATION_REQUESTS) {
        char request_dir[F_PATH], *request_state, *step, *message, *expires; json_object *request;
        if (entry->d_name[0] == '.' || !f_name(entry->d_name) || f_path(request_dir, sizeof(request_dir), approvals, entry->d_name)) continue;
        request_state = scalar(request_dir, "state"); step = scalar(request_dir, "step-id");
        if (!request_state || !step) { free(request_state); free(step); continue; }
        if (strcmp(request_state, "pending")) { free(request_state); free(step); continue; }
        request = json_object_new_object(); f_string_add(request, "request_id", entry->d_name); f_string_add(request, "step_id", step); f_string_add(request, "state", request_state);
        message = scalar(request_dir, "message"); expires = scalar(request_dir, "expires-at");
        nullable(request, "message", message); nullable(request, "expires_at", expires);
        json_object_array_add(requests, request); count++; free(request_state); free(step); free(message); free(expires);
    }
    closedir(dir); json_object_array_sort(requests, object_id_order);
    json_object_object_add(task, "pending_requests", requests); return 0;
}

static int add_step(json_object *steps, const char *run_dir, char **fields, size_t count, size_t *total) {
    char steps_dir[F_PATH], path[F_PATH], *state = NULL, *attempts = NULL, *authoritative = NULL, attempt_id[128];
    json_object *step; const char *profile;
    if (count < 10 || *total >= OBSERVATION_STEPS || !f_name(fields[1]) ||
        !safe_text(fields[2], 64) || !safe_text(fields[9], 128)) return -1;
    if (f_path(steps_dir, sizeof(steps_dir), run_dir, "steps") || f_path(path, sizeof(path), steps_dir, fields[1])) return -1;
    state = scalar(path, "state"); attempts = scalar(path, "attempts"); authoritative = scalar(path, "authoritative-attempt");
    step = json_object_new_object(); f_string_add(step, "step_id", fields[1]);
    f_string_add(step, "kind", fields[2]);
    profile = fields[9]; f_string_add(step, "agent_profile", strcmp(profile, "-") ? profile : "unavailable");
    nullable(step, "state", state ? state : "unavailable");
    if (authoritative && f_name(authoritative) && snprintf(attempt_id, sizeof(attempt_id), "attempt-%s", authoritative) < (int)sizeof(attempt_id))
        f_string_add(step, "attempt_id", attempt_id);
    else if (attempts && f_name(attempts) && strcmp(attempts, "0") && snprintf(attempt_id, sizeof(attempt_id), "attempt-%s", attempts) < (int)sizeof(attempt_id))
        f_string_add(step, "attempt_id", attempt_id);
    else json_object_object_add(step, "attempt_id", json_object_new_null());
    if (state && (!strcmp(state, "waiting") || !strcmp(state, "waiting-approval") || !strcmp(state, "waiting_remote"))) {
        f_string_add(step, "waiting_reason", !strcmp(state, "waiting-approval") ? "approval" : "dependency");
    } else f_string_add(step, "waiting_reason", "none");
    json_object_array_add(steps, step); (*total)++;
    free(state); free(attempts); free(authoritative); return 0;
}

static json_object *steps_for(json_object *state, const char *directory) {
    const char *kind = f_string(state, "work_kind"), *project = f_string(state, "execution_project_id"), *run = f_string(state, "run_id");
    json_object *steps = json_object_new_array(); size_t total = 0;
    char run_dir[F_PATH], graph_path[F_PATH], *graph, *line, *save;
    if (!kind || strcmp(kind, "workflow") || !project || !run || !f_name(project) || !f_name(run) ||
        snprintf(run_dir, sizeof(run_dir), "%s/state/v2/projects/%s/workflows/runs/%s", f_home, project, run) >= (int)sizeof(run_dir) ||
        f_path(graph_path, sizeof(graph_path), run_dir, "graph.tsv") || !private_regular(graph_path) ||
        !(graph = f_read(graph_path, OBSERVATION_GRAPH_LIMIT))) goto fallback;
    save = NULL;
    for (line = strtok_r(graph, "\n", &save); line && total < OBSERVATION_STEPS; line = strtok_r(NULL, "\n", &save)) {
        char *fields[32], *cursor = line; size_t count = 0;
        while (count < sizeof(fields) / sizeof(fields[0]) && (fields[count] = strtok_r(cursor, "\t", &cursor))) count++;
        if (count && !strcmp(fields[0], "step") && add_step(steps, run_dir, fields, count, &total)) { free(graph); goto fallback; }
    }
    free(graph); if (json_object_array_length(steps)) return steps;
fallback:
    { json_object *step = json_object_new_object(); f_string_add(step, "step_id", "unavailable"); json_object_object_add(step, "attempt_id", json_object_new_null()); f_string_add(step, "kind", kind && !strcmp(kind, "workflow") ? "workflow" : "exec"); f_string_add(step, "agent_profile", "unavailable"); f_string_add(step, "state", f_string(state, "state") ? f_string(state, "state") : "unavailable"); f_string_add(step, "waiting_reason", "reconciliation"); json_object_array_add(steps, step); }
    (void)directory; return steps;
}

static void current_step_identity(json_object *task, json_object *steps) {
    json_object *selected = NULL; size_t i;
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i); const char *state = f_string(step, "state");
        if (state && (!strcmp(state, "running") || !strcmp(state, "waiting") ||
                      !strcmp(state, "waiting-approval") || !strcmp(state, "waiting_remote") ||
                      !strcmp(state, "recovery-required"))) { selected = step; break; }
    }
    if (!selected && json_object_array_length(steps)) selected = json_object_array_get_idx(steps, json_object_array_length(steps) - 1);
    nullable(task, "step_id", f_string(selected, "step_id"));
    nullable(task, "attempt_id", f_string(selected, "attempt_id"));
    if (f_string(selected, "agent_profile") && strcmp(f_string(selected, "agent_profile"), "unavailable"))
        f_string_add(task, "agent_profile", f_string(selected, "agent_profile"));
}

static json_object *build_observation(const char *id, const char *directory, int64_t now) {
    json_object *accepted = task_read_record(directory, "acceptance.json"), *state = task_read_record(directory, "state.json");
    char path[F_PATH]; json_object *package = NULL, *spec = NULL, *task = NULL, *steps;
    const char *host, *project, *workspace, *kind, *digest;
    if (!accepted || !f_number_is(accepted, "schema_version", 1) ||
        !f_string(accepted, "task_id") || strcmp(f_string(accepted, "task_id"), id) || !f_string(accepted, "project") ||
        !f_string(accepted, "project_id") || !f_string(accepted, "submission_key") ||
        !json_object_is_type(f_field(accepted, "accepted_at"), json_type_int) ||
        !(digest = f_string(accepted, "spec_sha256")) || !task_hex(digest, 64) ||
        !task_runtime_valid(state) || f_path(path, sizeof(path), directory, "package.json") || !private_regular(path) ||
        !(package = f_read_json(path, TASK_PACKAGE_LIMIT)) ||
        !f_string(package, "spec_sha256") || strcmp(f_string(package, "spec_sha256"), digest)) goto bad;
    if ((f_string(state, "state") && (!strcmp(f_string(state, "state"), "starting") || !strcmp(f_string(state, "state"), "running"))) &&
        !task_owner_active(directory)) {
        f_string_add(state, "recorded_state", f_string(state, "state")); f_string_add(state, "state", "outcome_unknown");
        f_string_add(state, "failure", "owner_unavailable");
    }
    task_cancel_view(directory, digest, state);
    spec = task_spec(f_field(package, "spec"), true);
    if (!spec) goto bad;
    task = json_object_new_object(); f_string_add(task, "task_id", id);
    nullable(task, "run_id", f_string(state, "run_id"));
    json_object_object_add(task, "step_id", json_object_new_null());
    json_object_object_add(task, "attempt_id", json_object_new_null());
    host = f_string(spec, "host"); project = f_string(state, "execution_project_id"); workspace = f_string(state, "workspace"); kind = f_string(f_field(spec, "work"), "kind");
    nullable(task, "assigned_host", host); nullable(task, "workspace", workspace);
    f_string_add(task, "agent_profile", kind && !strcmp(kind, "exec") ? "none" : "unavailable");
    json_object_object_add(task, "execution_owner", owner(state));
    f_string_add(task, "execution_state", f_string(state, "state") ? f_string(state, "state") : "unavailable");
    json_object_object_add(task, "observation_timestamps", observation_timestamps(accepted, state, now));
    json_object_object_add(task, "effective_configuration", effective_configuration(spec));
    waiting(task, state, id);
    steps = steps_for(state, directory); current_step_identity(task, steps); json_object_object_add(task, "steps", steps);
    add_pending(task, project, f_string(state, "run_id"));
    {
        json_object *contract = json_object_new_object(), *missing = json_object_new_array();
        f_string_add(contract, "availability", "unavailable"); f_string_add(contract, "reason", "no receiver obligation record is attached to task protocol 1");
        json_object_object_add(contract, "missing_evidence", missing); json_object_object_add(task, "contract", contract);
    }
    json_object_put(accepted); json_object_put(state); json_object_put(package); json_object_put(spec); return task;
bad:
    json_object_put(accepted); json_object_put(state); json_object_put(package); json_object_put(spec); json_object_put(task);
    return NULL;
}

json_object *task_observation(const char *id) {
    char root[F_PATH], directory[F_PATH]; struct stat st; json_object *task; int64_t now = (int64_t)time(NULL);
    if (!task_id_valid(id) || task_store_root(root) || f_path(directory, sizeof(directory), root, id) || lstat(directory, &st) || !S_ISDIR(st.st_mode) ||
        !(task = build_observation(id, directory, now))) return f_error("fleet-observation", "recovery_required", "receiver task records are missing, malformed, or no longer safe to inspect");
    { json_object *data = json_object_new_object(); json_object_object_add(data, "snapshot_schema_version", json_object_new_int(1)); nullable_now(data, "receiver_observed_at", now); json_object_object_add(data, "task", task); return f_success("fleet-observation", data); }
}

json_object *task_overview(void) {
    char root[F_PATH]; DIR *dir = NULL; struct dirent *entry; size_t count = 0; int64_t now = (int64_t)time(NULL);
    json_object *data = json_object_new_object(), *tasks = json_object_new_array();
    json_object_object_add(data, "snapshot_schema_version", json_object_new_int(1));
    nullable_now(data, "receiver_observed_at", now);
    json_object_object_add(data, "tasks", tasks);
    if (task_store_root(root) || !(dir = opendir(root))) { json_object_put(data); return f_error("fleet-overview", "io_failed", "receiver task storage is unavailable for observation"); }
    while ((entry = readdir(dir))) {
        char directory[F_PATH]; json_object *task;
        if (entry->d_name[0] == '.' || !task_id_valid(entry->d_name)) continue;
        if (count >= OBSERVATION_TASKS || f_path(directory, sizeof(directory), root, entry->d_name)) { closedir(dir); json_object_put(data); return f_error("fleet-overview", "limit", "receiver observation exceeds the 512-task bound"); }
        task = build_observation(entry->d_name, directory, now);
        if (task) { json_object_array_add(tasks, task); count++; }
    }
    closedir(dir); json_object_array_sort(tasks, task_order); return f_success("fleet-overview", data);
}
