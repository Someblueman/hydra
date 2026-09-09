#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
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

enum waiting_action {
    WAIT_OVERVIEW, WAIT_RECONCILE, WAIT_START, WAIT_ADMISSION, WAIT_APPROVAL,
    WAIT_AUTH, WAIT_DEPENDENCY, WAIT_LOGS, WAIT_RESULT
};

struct waiting_values {
    const char *reason;
    const char *detail;
    enum waiting_action action;
};

static bool contains_any(const char *value, const char *first, const char *second) {
    return value && (strstr(value, first) || strstr(value, second));
}

static bool failure_waiting_values(const char *failure, struct waiting_values *result) {
    if (contains_any(failure, "reconcil", "owner_unavailable")) {
        *result = (struct waiting_values){"reconciliation", "the receiver retained an uncertain outcome", WAIT_RECONCILE}; return true;
    }
    if (contains_any(failure, "auth", "credential")) {
        *result = (struct waiting_values){"authentication", "receiver authentication or provider credentials need attention", WAIT_AUTH}; return true;
    }
    if (contains_any(failure, "dependency", "startup")) {
        *result = (struct waiting_values){"dependency", "a required receiver dependency or startup step is unavailable", WAIT_DEPENDENCY}; return true;
    }
    return false;
}

static struct waiting_values waiting_values(const char *name, const char *admission, const char *failure) {
    struct waiting_values result = {"none", "execution state is recorded; no receiver wait is active", WAIT_OVERVIEW};
    if (!name) return (struct waiting_values){"reconciliation", "receiver state is missing or malformed", WAIT_RECONCILE};
    if (!strcmp(name, "accepted")) return (struct waiting_values){"admission", "task is accepted but has no launch claim", WAIT_START};
    if (admission && !strcmp(admission, "queued")) return (struct waiting_values){"admission", "receiver admission is queued for capacity", WAIT_ADMISSION};
    if (!strcmp(name, "waiting_approval")) return (struct waiting_values){"approval", "workflow execution is suspended for an approval decision", WAIT_APPROVAL};
    if (!strcmp(name, "outcome_unknown")) return (struct waiting_values){"reconciliation", "the receiver retained an uncertain outcome", WAIT_RECONCILE};
    if (failure_waiting_values(failure, &result)) return result;
    if (!strcmp(name, "starting") || !strcmp(name, "running"))
        return (struct waiting_values){"none", "receiver owner has recorded active execution", WAIT_LOGS};
    if (!strcmp(name, "failed") || !strcmp(name, "expired"))
        return (struct waiting_values){"none", failure ? failure : "receiver recorded a terminal failure", WAIT_LOGS};
    if (!strcmp(name, "succeeded") || !strcmp(name, "cancelled"))
        return (struct waiting_values){"none", "receiver recorded a terminal execution state", WAIT_RESULT};
    return result;
}

static void waiting_action(char action[256], enum waiting_action kind, const char *task_id) {
    const char *format = "inspect hydra fleet overview --json for %s";
    switch (kind) {
    case WAIT_RECONCILE: format = "reconcile task %s before any retry"; break;
    case WAIT_START: format = "review the specification, then start task %s with its exact trust digest"; break;
    case WAIT_ADMISSION: format = "wait for the receiver admission claim for %s"; break;
    case WAIT_APPROVAL: format = "inspect and decide the pending approval for %s"; break;
    case WAIT_AUTH: format = "inspect receiver authentication before resuming %s"; break;
    case WAIT_DEPENDENCY: format = "inspect receiver capabilities and dependency diagnostics for %s"; break;
    case WAIT_LOGS: format = "inspect bounded logs for %s"; break;
    case WAIT_RESULT: format = "inspect the retained result snapshot for %s"; break;
    default: break;
    }
    (void)snprintf(action, 256, format, task_id);
}

static void waiting(json_object *object, json_object *state, const char *task_id) {
    struct waiting_values values = waiting_values(f_string(state, "state"), f_string(f_field(state, "admission"), "state"), f_string(state, "failure"));
    char action[256]; json_object *value = json_object_new_object();
    waiting_action(action, values.action, task_id);
    f_string_add(value, "reason", values.reason); f_string_add(value, "detail", values.detail); f_string_add(value, "next_action", action);
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

static bool step_fields_valid(char **fields, size_t count, size_t total) {
    return count >= 10 && total < OBSERVATION_STEPS && f_name(fields[1]) &&
        safe_text(fields[2], 64) && safe_text(fields[9], 128);
}

static void step_attempt(json_object *step, char attempt_id[128], const char *authoritative, const char *attempts) {
    if (authoritative && f_name(authoritative) && snprintf(attempt_id, 128, "attempt-%s", authoritative) < 128)
        f_string_add(step, "attempt_id", attempt_id);
    else if (attempts && f_name(attempts) && strcmp(attempts, "0") && snprintf(attempt_id, 128, "attempt-%s", attempts) < 128)
        f_string_add(step, "attempt_id", attempt_id);
    else json_object_object_add(step, "attempt_id", json_object_new_null());
}

static const char *step_waiting_reason(const char *state) {
    if (state && !strcmp(state, "waiting-approval")) return "approval";
    if (state && (!strcmp(state, "waiting") || !strcmp(state, "waiting_remote"))) return "dependency";
    return "none";
}

static int add_step(json_object *steps, const char *run_dir, char **fields, size_t count, size_t *total) {
    char steps_dir[F_PATH], path[F_PATH], *state = NULL, *attempts = NULL, *authoritative = NULL, attempt_id[128];
    json_object *step; const char *profile;
    if (!step_fields_valid(fields, count, *total)) return -1;
    if (f_path(steps_dir, sizeof(steps_dir), run_dir, "steps") || f_path(path, sizeof(path), steps_dir, fields[1])) return -1;
    state = scalar(path, "state"); attempts = scalar(path, "attempts"); authoritative = scalar(path, "authoritative-attempt");
    step = json_object_new_object(); f_string_add(step, "step_id", fields[1]);
    f_string_add(step, "kind", fields[2]);
    profile = fields[9]; f_string_add(step, "agent_profile", strcmp(profile, "-") ? profile : "unavailable");
    nullable(step, "state", state ? state : "unavailable");
    step_attempt(step, attempt_id, authoritative, attempts);
    f_string_add(step, "waiting_reason", step_waiting_reason(state));
    json_object_array_add(steps, step); (*total)++;
    free(state); free(attempts); free(authoritative); return 0;
}

static bool graph_paths(json_object *state, char run_dir[F_PATH], char graph_path[F_PATH]) {
    const char *kind = f_string(state, "work_kind"), *project = f_string(state, "execution_project_id"), *run = f_string(state, "run_id");
    if (!kind || strcmp(kind, "workflow") || !project || !run || !f_name(project) || !f_name(run)) return false;
    if (snprintf(run_dir, F_PATH, "%s/state/v2/projects/%s/workflows/runs/%s", f_home, project, run) >= F_PATH) return false;
    return !f_path(graph_path, F_PATH, run_dir, "graph.tsv") && private_regular(graph_path);
}

static int parse_steps(json_object *steps, const char *run_dir, char *graph) {
    char *line, *save = NULL; size_t total = 0;
    save = NULL;
    for (line = strtok_r(graph, "\n", &save); line && total < OBSERVATION_STEPS; line = strtok_r(NULL, "\n", &save)) {
        char *fields[32], *cursor = line; size_t count = 0;
        while (count < sizeof(fields) / sizeof(fields[0]) && (fields[count] = strtok_r(cursor, "\t", &cursor))) count++;
        if (count && !strcmp(fields[0], "step") && add_step(steps, run_dir, fields, count, &total)) return -1;
    }
    return json_object_array_length(steps) ? 0 : -1;
}

static void add_unavailable_step(json_object *steps, json_object *state, const char *kind) {
    json_object *step = json_object_new_object();
    f_string_add(step, "step_id", "unavailable"); json_object_object_add(step, "attempt_id", json_object_new_null());
    f_string_add(step, "kind", kind && !strcmp(kind, "workflow") ? "workflow" : "exec");
    f_string_add(step, "agent_profile", "unavailable"); f_string_add(step, "state", f_string(state, "state") ? f_string(state, "state") : "unavailable");
    f_string_add(step, "waiting_reason", "reconciliation"); json_object_array_add(steps, step);
}

static json_object *steps_for(json_object *state, const char *directory) {
    const char *kind = f_string(state, "work_kind"); json_object *steps = json_object_new_array();
    char run_dir[F_PATH], graph_path[F_PATH], *graph = NULL;
    if (graph_paths(state, run_dir, graph_path) && (graph = f_read(graph_path, OBSERVATION_GRAPH_LIMIT)) && !parse_steps(steps, run_dir, graph)) {
        free(graph); return steps;
    }
    free(graph); add_unavailable_step(steps, state, kind); (void)directory; return steps;
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

/* V2 observations are deliberately read-only and bounded.  A cursor is the
 * last sequence the client has confirmed; the receiver returns sequence+1.
 * Retention and malformed streams are reported explicitly instead of being
 * silently treated as an empty stream. */
static void event_observation(json_object *data, json_object *state, json_object *request) {
    json_object *stream = json_object_new_object(), *events = json_object_new_array();
    const char *project = f_string(state, "execution_project_id"), *run = f_string(state, "run_id");
    char path[F_PATH]; FILE *input = NULL; char line[8192]; unsigned cursor = 0, wanted = 128, first = 0, last = 0;
    bool gap = false, reset = false, available = false, scan_truncated = false; size_t returned = 0, bytes = 0; unsigned delivered = 0; off_t byte_offset = 0;
    char generation[128] = ""; struct stat stream_stat; off_t next_byte_offset = 0, safe_offset = 0, reset_offset = 0, line_start;
    if (f_field(request, "cursor") && json_object_is_type(f_field(request, "cursor"), json_type_int) && json_object_get_int64(f_field(request, "cursor")) >= 0)
        cursor = (unsigned)json_object_get_int64(f_field(request, "cursor"));
    if (f_field(request, "event_limit") && json_object_is_type(f_field(request, "event_limit"), json_type_int) && json_object_get_int64(f_field(request, "event_limit")) >= 1 && json_object_get_int64(f_field(request, "event_limit")) <= 128)
        wanted = (unsigned)json_object_get_int64(f_field(request, "event_limit"));
    if (f_field(request, "byte_offset") && json_object_is_type(f_field(request, "byte_offset"), json_type_int) && json_object_get_int64(f_field(request, "byte_offset")) >= 0)
        byte_offset = (off_t)json_object_get_int64(f_field(request, "byte_offset"));
    if (!project || !run || !f_name(project) || !f_name(run) || snprintf(path, sizeof(path), "%s/state/v2/projects/%s/workflows/runs/%s/events.jsonl", f_home, project, run) >= (int)sizeof(path) || !(input = fopen(path, "r")) || fstat(fileno(input), &stream_stat)) {
        json_object_object_add(stream, "schema_version", json_object_new_int(1)); json_object_object_add(stream, "events", events);
        json_object_object_add(stream, "oldest_cursor", json_object_new_int64(cursor));
        json_object_object_add(stream, "available", json_object_new_boolean(false)); f_string_add(stream, "unavailable_reason", "event-history-missing");
        json_object_object_add(stream, "retention_gap", json_object_new_boolean(true)); json_object_object_add(stream, "stream_reset", json_object_new_boolean(false));
        json_object_object_add(stream, "next_cursor", json_object_new_int64(cursor)); json_object_object_add(stream, "head_cursor", json_object_new_int64(cursor)); f_string_add(stream, "duplicate_policy", "sequence-cursor");
        json_object_object_add(stream, "next_byte_offset", json_object_new_int64(0));
        json_object_object_add(data, "event_observation", stream); return;
    }
    available = true;
    (void)snprintf(generation, sizeof(generation), "%llu:%llu", (unsigned long long)stream_stat.st_dev, (unsigned long long)stream_stat.st_ino);
    safe_offset = byte_offset; reset_offset = byte_offset;
    if (f_field(request, "stream_id") && (!f_text(f_field(request, "stream_id")) || strcmp(f_text(f_field(request, "stream_id")), generation))) {
        /* A different inode is a new stream.  Re-read from its beginning and
         * retain the caller's cursor while returning an explicit reset. */
        reset = true; safe_offset = 0; reset_offset = 0;
    }
    if (byte_offset > stream_stat.st_size || (!reset && fseeko(input, byte_offset, SEEK_SET))) { reset = true; safe_offset = 0; reset_offset = 0; }
    if (reset) (void)fseeko(input, safe_offset, SEEK_SET);
    while (fgets(line, sizeof(line), input)) {
        json_object *event; json_object *sequence;
        line_start = ftello(input) - (off_t)strlen(line);
        if (!strchr(line, '\n') && !feof(input)) { reset = true; break; }
        bytes += strlen(line); if (bytes > 262144U) { scan_truncated = true; safe_offset = line_start; break; }
        event = f_parse(line); sequence = f_field(event, "sequence");
        if (!event || !json_object_is_type(event, json_type_object) || !f_number_is(event, "schema_version", 1) || !json_object_is_type(sequence, json_type_int) || json_object_get_int64(sequence) < 1 || json_object_get_int64(sequence) > 4294967295U) { json_object_put(event); reset = true; break; }
        { unsigned value = (unsigned)json_object_get_int64(sequence);
          if (!first) first = value;
          if ((last && value != last + 1U) || (byte_offset > 0 && !last && cursor < 4294967295U && value != cursor + 1U)) { reset = true; json_object_put(event); break; }
          last = value;
          if (value > cursor && returned < wanted) { json_object_array_add(events, event); returned++; delivered = value; } else json_object_put(event); }
        if (returned >= wanted) break;
    }
    /* A bounded scan stops after fgets has consumed the next line.  Resume
     * from that line's start so the caller cannot skip an event. */
    next_byte_offset = scan_truncated ? safe_offset : ftello(input);
    if (next_byte_offset < 0) next_byte_offset = safe_offset;
    fclose(input);
    if (first && cursor + 1U < first) gap = true;
    if (reset || (f_field(request, "stream_id") && strcmp(f_text(f_field(request, "stream_id")), generation))) { json_object_put(events); events = json_object_new_array(); delivered = cursor; next_byte_offset = reset_offset; }
    json_object_object_add(stream, "schema_version", json_object_new_int(1)); json_object_object_add(stream, "events", events);
    json_object_object_add(stream, "available", json_object_new_boolean(available)); f_string_add(stream, "stream_id", generation);
    json_object_object_add(stream, "scan_truncated", json_object_new_boolean(scan_truncated));
    json_object_object_add(stream, "next_byte_offset", json_object_new_int64((int64_t)next_byte_offset));
    json_object_object_add(stream, "oldest_cursor", json_object_new_int64(first ? first - 1U : cursor));
    if (!delivered) delivered = cursor;
    json_object_object_add(stream, "next_cursor", json_object_new_int64(delivered));
    json_object_object_add(stream, "head_cursor", json_object_new_int64(last));
    json_object_object_add(stream, "retention_gap", json_object_new_boolean(gap)); json_object_object_add(stream, "stream_reset", json_object_new_boolean(reset));
    f_string_add(stream, "duplicate_policy", "sequence-cursor");
    json_object_object_add(data, "event_observation", stream);
}

static void v2_evidence(json_object *task, json_object *state, const char *directory, json_object *steps) {
    json_object *attempts = json_object_new_array(), *artifacts = json_object_new_array(), *provider = json_object_new_object(), *verification = json_object_new_object(), *result = NULL;
    char path[F_PATH]; size_t i;
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i); const char *step_id = f_string(step, "step_id");
        char step_root[F_PATH] = "", *count_text = NULL; unsigned count = 1, n;
        if (f_string(state, "work_kind") && !strcmp(f_string(state, "work_kind"), "workflow") && f_name(f_string(state, "execution_project_id")) && f_name(f_string(state, "run_id")) && step_id && f_name(step_id) && snprintf(step_root, sizeof(step_root), "%s/state/v2/projects/%s/workflows/runs/%s/steps/%s", f_home, f_string(state, "execution_project_id"), f_string(state, "run_id"), step_id) < (int)sizeof(step_root)) {
            count_text = scalar(step_root, "attempts");
            if (count_text && f_name(count_text) && strspn(count_text, "0123456789") == strlen(count_text)) { unsigned long parsed = strtoul(count_text, NULL, 10); if (parsed > 0 && parsed <= 10000) count = (unsigned)parsed; }
        }
        free(count_text);
        for (n = 1; n <= count; n++) {
            json_object *attempt = json_object_new_object(); char attempt_root[F_PATH] = "", *attempt_state = NULL, *exit_status = NULL;
            f_string_add(attempt, "step_id", step_id ? step_id : "unavailable"); (void)snprintf(attempt_root, sizeof(attempt_root), "%s/attempt-%u", step_root, n);
            if (step_root[0]) { struct stat attempt_stat; if (lstat(attempt_root, &attempt_stat) || !S_ISDIR(attempt_stat.st_mode)) { json_object_put(attempt); continue; } }
            { char id[32]; (void)snprintf(id, sizeof(id), "attempt-%u", n); f_string_add(attempt, "attempt_id", id); }
            if (step_root[0]) { attempt_state = scalar(attempt_root, "state"); exit_status = scalar(attempt_root, "exit-code"); }
            nullable(attempt, "state", attempt_state); nullable(attempt, "process_exit", exit_status);
            f_string_add(attempt, "result_collection", "unavailable"); f_string_add(attempt, "verification", "unavailable");
            json_object_array_add(attempts, attempt); free(attempt_state); free(exit_status);
        }
    }
    if (!f_path(path, sizeof(path), directory, "result.json")) result = f_read_json(path, TASK_PACKAGE_LIMIT + 1024);
    if (result && json_object_is_type(f_field(f_field(result, "result"), "artifacts"), json_type_array)) {
        json_object *source = f_parse(json_object_to_json_string_ext(f_field(f_field(result, "result"), "artifacts"), JSON_C_TO_STRING_PLAIN));
        for (i = 0; source && i < json_object_array_length(source); i++) { json_object *item = json_object_array_get_idx(source, i); json_object_object_del(item, "hex"); json_object_array_add(artifacts, json_object_get(item)); }
        json_object_put(source);
        { json_object *checked = task_result_verify(result); f_string_add(verification, "kind", "integrity"); f_string_add(verification, "state", json_object_get_boolean(f_field(checked, "ok")) ? "integrity_verified" : "unavailable"); json_object_put(checked); }
    } else f_string_add(verification, "state", "unavailable");
    if (!f_path(path, sizeof(path), directory, "provenance.json")) {
        json_object *observed = f_read_json(path, 131072);
        if (observed) { json_object_object_add(provider, "observation", observed); f_string_add(provider, "state", "available"); }
    }
    if (!f_string(provider, "state")) f_string_add(provider, "state", "unavailable");
    json_object_object_add(task, "attempt_history", attempts); json_object_object_add(task, "artifact_inventory", artifacts);
    json_object_object_add(task, "provider_observations", provider); json_object_object_add(task, "verification", verification);
    { json_object *process = json_object_new_object(); nullable(process, "state", f_string(state, "state"));
      if (f_field(state, "exit_status")) json_object_object_add(process, "exit_status", json_object_get(f_field(state, "exit_status"))); else json_object_object_add(process, "exit_status", json_object_new_null());
      json_object_object_add(task, "process_exit", process); }
    { json_object *collection = json_object_new_object(); const char *result_state = f_string(state, "result_state");
      f_string_add(collection, "state", result_state && !strcmp(result_state, "ready") ? "ready" : "unavailable"); nullable(collection, "error", f_string(state, "result_error")); json_object_object_add(task, "result_collection", collection); }
    json_object_object_add(task, "approval_requests", json_object_get(f_field(task, "pending_requests")));
    json_object_put(result);
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
    v2_evidence(task, state, directory, steps);
    json_object_put(accepted); json_object_put(state); json_object_put(package); json_object_put(spec); return task;
bad:
    json_object_put(accepted); json_object_put(state); json_object_put(package); json_object_put(spec); json_object_put(task);
    return NULL;
}

json_object *task_observation(const char *id, json_object *request) {
    char root[F_PATH], directory[F_PATH]; struct stat st; json_object *task, *state = NULL; int64_t now = (int64_t)time(NULL);
    if (!task_id_valid(id) || task_store_root(root) || f_path(directory, sizeof(directory), root, id) || lstat(directory, &st) || !S_ISDIR(st.st_mode) ||
        !(task = build_observation(id, directory, now))) return f_error("fleet-observation", "recovery_required", "receiver task records are missing, malformed, or no longer safe to inspect");
    state = task_read_record(directory, "state.json");
    { json_object *data = json_object_new_object(); json_object_object_add(data, "snapshot_schema_version", json_object_new_int(1)); nullable_now(data, "receiver_observed_at", now); event_observation(data, state, request); v2_evidence(task, state, directory, f_field(task, "steps")); json_object_object_add(data, "task", task); json_object_put(state); return f_success("fleet-observation", data); }
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
