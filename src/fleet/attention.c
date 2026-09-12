#include "fleet/fleet.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include <json-c/json.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ATTENTION_LIMIT 512U
static bool semantic_failed;

static const char *text(json_object *o, const char *key) { return f_string(o, key); }
static const char *or_empty(const char *value) { return value ? value : ""; }
static void nullable(json_object *o, const char *key, const char *value) {
    if (value && *value) f_string_add(o, key, value);
    else json_object_object_add(o, key, json_object_new_null());
}
static void copy_value(json_object *dst, const char *key, json_object *value) {
    json_object_object_add(dst, key, value ? json_object_get(value) : json_object_new_null());
}
static int key_order(const void *left, const void *right) { return strcmp(*(const char *const *)left, *(const char *const *)right); }
static json_object *canonical_value(json_object *value);
static json_object *canonical_array(json_object *value) {
    json_object *copy = json_object_new_array(); size_t i;
    for (i = 0; i < json_object_array_length(value); i++) json_object_array_add(copy, canonical_value(json_object_array_get_idx(value, i)));
    return copy;
}
static json_object *canonical_object(json_object *value) {
    json_object *copy; size_t i, count = json_object_object_length(value), n = 0;
    char **keys = calloc(count ? count : 1, sizeof(*keys));
    if (!keys) return NULL;
    json_object_object_foreach(value, key, child) { if (n < count) keys[n++] = key; }
    qsort(keys, n, sizeof(keys[0]), key_order); copy = json_object_new_object();
    for (i = 0; i < n; i++) { child = f_field(value, keys[i]); json_object_object_add(copy, keys[i], canonical_value(child)); }
    free(keys); return copy;
}
static json_object *canonical_value(json_object *value) {
    if (!value) return json_object_new_null();
    if (json_object_is_type(value, json_type_array)) return canonical_array(value);
    if (json_object_is_type(value, json_type_object)) return canonical_object(value);
    return json_object_get(value);
}
static void copy_semantic(json_object *dst, const char *key, json_object *value) {
    json_object *copy;
    if (!value) { json_object_object_add(dst, key, json_object_new_null()); return; }
    copy = canonical_value(value);
    if (!copy) { semantic_failed = true; copy = json_object_new_null(); }
    json_object_object_add(dst, key, copy);
}
static bool valid_name(const char *value, const char *prefix) {
    return value && f_name(value) && (!prefix || !strncmp(value, prefix, strlen(prefix)));
}
static bool valid_task_id(const char *id) {
    return valid_name(id, "task_") && strlen(id) == 69 && task_hex(id + 5, 64);
}
static bool valid_task_identity(json_object *task) {
    const char *id = text(task, "task_id"), *digest = text(task, "spec_sha256");
    return valid_task_id(id) &&
        valid_name(text(task, "run_id"), "run_") && task_hex(digest, 64);
}
static bool valid_step_identity(json_object *step) {
    return valid_name(text(step, "step_id"), NULL) && valid_name(text(step, "attempt_id"), NULL);
}
static bool valid_request_identity(json_object *request) {
    return valid_name(text(request, "request_id"), NULL) && valid_name(text(request, "step_id"), NULL);
}
static bool valid_waiting_reason(json_object *task) {
    static const char *const reasons[] = {"none", "admission", "approval", "reconciliation", "authentication", "dependency", NULL};
    const char *reason = text(f_field(task, "waiting"), "reason"); size_t i;
    if (!reason) return false;
    for (i = 0; reasons[i]; i++) if (!strcmp(reason, reasons[i])) return true;
    return false;
}
static bool valid_task_state(json_object *task) {
    static const char *const states[] = {"accepted", "starting", "running", "waiting_approval", "succeeded", "failed", "expired", "cancelled", "outcome_unknown", NULL};
    const char *state = text(task, "execution_state"); size_t i;
    if (!state || !valid_waiting_reason(task)) return false;
    for (i = 0; states[i]; i++) if (!strcmp(state, states[i])) return true;
    return false;
}
static bool timestamp_value(json_object *value, int64_t *result) {
    const char *string; char *end; intmax_t parsed; size_t i;
    if (json_object_is_type(value, json_type_int)) {
        parsed = json_object_get_int64(value);
        if (parsed < 0) return false;
        *result = (int64_t)parsed; return true;
    }
    string = f_text(value);
    if (!string || !*string) return false;
    for (i = 0; string[i]; i++) if (string[i] < '0' || string[i] > '9') return false;
    errno = 0; parsed = strtoimax(string, &end, 10);
    if (*end || parsed < 0 || errno == ERANGE) return false;
    *result = (int64_t)parsed; return true;
}
/* 0 means no expiry, 1 expired, and 2 malformed or missing. */
static int expiry_status(json_object *request) {
    int64_t expiry; json_object *value = f_field(request, "expires_at");
    if (!value || json_object_is_type(value, json_type_null) || !timestamp_value(value, &expiry)) return 2;
    if (expiry == 0) return 0;
    return expiry <= (int64_t)time(NULL) ? 1 : 0;
}
static void add_identity(json_object *o, const char *host, json_object *task, const char *step,
                         const char *attempt, json_object *request) {
    nullable(o, "host", host); nullable(o, "task_id", text(task, "task_id"));
    nullable(o, "run_id", text(task, "run_id")); nullable(o, "step_id", step);
    nullable(o, "attempt_id", attempt); nullable(o, "spec_sha256", text(task, "spec_sha256"));
    nullable(o, "request_id", request ? text(request, "request_id") : NULL);
}
static void add_observation_fields(json_object *item, json_object *host_data, json_object *task) {
    json_object *freshness = f_field(host_data, "freshness");
    json_object *times = f_field(task, "observation_timestamps");
    const char *state = text(freshness, "state");
    f_string_add(item, "freshness", state ? state : "unknown");
    copy_value(item, "receiver_observed_at", f_field(host_data, "receiver_observed_at") ? f_field(host_data, "receiver_observed_at") : f_field(times, "receiver_observed_at"));
    copy_value(item, "last_confirmed_at", f_field(host_data, "last_confirmed_at"));
}
static void add_semantics(json_object *revision, json_object *task, json_object *request) {
    static const char *const fields[] = {"execution_state", "cancellation", "cancellation_scope", "cancel_requested_at", "result_collection", "verification", "artifact_inventory", "process_exit", NULL};
    size_t i;
    for (i = 0; fields[i]; i++) copy_semantic(revision, fields[i], f_field(task, fields[i]));
    if (request) {
        copy_semantic(revision, "request_state", f_field(request, "state"));
        copy_semantic(revision, "expires_at", f_field(request, "expires_at"));
        copy_semantic(revision, "request_message", f_field(request, "message"));
    }
}
static json_object *make_item(const char *kind, const char *host, json_object *task, json_object *request,
                              const char *step, const char *attempt, const char *reason, const char *next,
                              json_object *host_data) {
    json_object *item = json_object_new_object(), *revision = json_object_new_object(), *route = json_object_new_object();
    const char *route_kind = !strcmp(kind, "result") ? "task-result" : "task-observe";
    semantic_failed = false;
    f_string_add(item, "kind", kind); f_string_add(item, "source", "fleet overview");
    f_string_add(item, "reason", reason); f_string_add(item, "next_action", next);
    add_identity(item, host, task, step, attempt, request); add_observation_fields(item, host_data, task);
    json_object_object_add(item, "accepted", json_object_new_boolean(false));
    f_string_add(revision, "kind", kind); add_identity(revision, host, task, step, attempt, request);
    add_semantics(revision, task, request);
    if (semantic_failed) { f_string_add(item, "kind", "unknown"); f_string_add(item, "reason", "semantic_projection_unavailable"); f_string_add(item, "next_action", "inspect the recorded task observation"); }
    f_string_add(item, "revision", json_object_to_json_string_ext(revision, JSON_C_TO_STRING_PLAIN)); json_object_put(revision);
    f_string_add(route, "kind", route_kind); add_identity(route, host, task, step, attempt, request);
    json_object_object_add(route, "navigable", json_object_new_boolean(valid_name(host, NULL) && valid_task_id(text(task, "task_id"))));
    json_object_object_add(route, "fresh_action", json_object_new_boolean(false));
    f_string_add(route, "freshness", text(f_field(host_data, "freshness"), "state") ? text(f_field(host_data, "freshness"), "state") : "unknown");
    json_object_object_add(item, "route", route); return item;
}
static bool same_item(json_object *left, json_object *right) {
    static const char *const fields[] = {"kind", "host", "task_id", "run_id", "step_id", "attempt_id", "spec_sha256", "request_id", "revision", NULL};
    size_t i;
    for (i = 0; fields[i]; i++) {
        json_object *a = f_field(left, fields[i]), *b = f_field(right, fields[i]);
        if (!a || !b) { if (a != b) return false; continue; }
        if (strcmp(json_object_to_json_string_ext(a, JSON_C_TO_STRING_PLAIN), json_object_to_json_string_ext(b, JSON_C_TO_STRING_PLAIN))) return false;
    }
    return true;
}
static void append_item(json_object *items, json_object *item, bool *truncated) {
    size_t i;
    for (i = 0; i < json_object_array_length(items); i++) if (same_item(json_object_array_get_idx(items, i), item)) { json_object_put(item); return; }
    if (json_object_array_length(items) >= ATTENTION_LIMIT) { *truncated = true; json_object_put(item); return; }
    json_object_array_add(items, item);
}
static json_object *unknown_item(const char *host, json_object *task, json_object *request, const char *reason, json_object *host_data) {
    const char *id = text(task, "task_id");
    return make_item("unknown", host, task, request, text(request, "step_id"), NULL, reason,
                     id ? "inspect the recorded task observation" : "inspect fleet overview and reconcile the host", host_data);
}
static json_object *find_step(json_object *steps, const char *step_id, size_t *matches) {
    json_object *found = NULL; size_t i; *matches = 0;
    if (!json_object_is_type(steps, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (!strcmp(or_empty(text(step, "step_id")), or_empty(step_id))) { found = step; (*matches)++; }
    }
    return found;
}
static void add_request_item(json_object *items, const char *host, json_object *host_data, json_object *task, json_object *steps, json_object *request, bool *truncated) {
    json_object *step; size_t matches; int expiry; const char *reason;
    if (!json_object_is_type(request, json_type_object) || !valid_request_identity(request)) { append_item(items, unknown_item(host, task, request, "malformed_request", host_data), truncated); return; }
    if (strcmp(or_empty(text(request, "state")), "pending")) {
        append_item(items, unknown_item(host, task, request, "malformed_request_state", host_data), truncated);
        return;
    }
    step = find_step(steps, text(request, "step_id"), &matches);
    if (matches != 1 || !valid_step_identity(step)) { append_item(items, unknown_item(host, task, request, "approval_binding_unknown", host_data), truncated); return; }
    expiry = expiry_status(request);
    if (expiry == 2) { append_item(items, unknown_item(host, task, request, "malformed_expiry", host_data), truncated); return; }
    reason = expiry == 1 ? "approval_expired" : "approval";
    append_item(items, make_item(expiry == 1 ? "approval_expired" : "approval", host, task, request, text(step, "step_id"), text(step, "attempt_id"), reason, expiry == 1 ? "inspect the expired approval record" : "inspect the pending approval", host_data), truncated);
}
static void add_result_item(json_object *items, const char *host, json_object *host_data, json_object *task, bool *truncated) {
    if (strcmp(or_empty(text(f_field(task, "result_collection"), "state")), "ready")) return;
    if (!valid_name(text(task, "step_id"), NULL) || !valid_name(text(task, "attempt_id"), NULL))
        append_item(items, unknown_item(host, task, NULL, "result_binding_unknown", host_data), truncated);
    else append_item(items, make_item("result", host, task, NULL, text(task, "step_id"), text(task, "attempt_id"), "result_ready", "inspect the retained result snapshot", host_data), truncated);
}
static void add_task_items(json_object *items, const char *host, json_object *host_data, json_object *task, bool *truncated) {
    json_object *requests = f_field(task, "pending_requests"), *steps = f_field(task, "steps"); size_t i;
    if (!valid_task_identity(task) || !valid_task_state(task)) {
        append_item(items, unknown_item(host, task, NULL, valid_task_identity(task) ? "unsupported_observation" : "missing_identity", host_data), truncated); return;
    }
    if (!json_object_is_type(requests, json_type_array)) { append_item(items, unknown_item(host, task, NULL, "malformed_observation", host_data), truncated); return; }
    for (i = 0; i < json_object_array_length(requests); i++) add_request_item(items, host, host_data, task, steps, json_object_array_get_idx(requests, i), truncated);
    add_result_item(items, host, host_data, task, truncated);
}
static int item_order(const void *left, const void *right) {
    const char *a = text(*(json_object *const *)left, "revision"), *b = text(*(json_object *const *)right, "revision"); return strcmp(or_empty(a), or_empty(b));
}
json_object *f_attention_aggregate(json_object *aggregate) {
    json_object *data = f_field(aggregate, "data"), *hosts = f_field(data, "hosts"), *items = json_object_new_array(); bool partial = false, truncated = false; size_t i, j;
    if (!json_object_is_type(hosts, json_type_array)) { json_object_put(items); return f_error("fleet-attention", "invalid_response", "fleet overview did not return host observations"); }
    for (i = 0; i < json_object_array_length(hosts); i++) {
        json_object *host = json_object_array_get_idx(hosts, i), *host_data = f_field(host, "data"), *tasks = f_field(host_data, "tasks"); const char *host_name = text(host, "host"), *error = text(f_field(host, "error"), "code");
        if (!json_object_get_boolean(f_field(host, "ok")) || !json_object_is_type(tasks, json_type_array)) {
            json_object *empty = json_object_new_object();
            partial = true; append_item(items, unknown_item(host_name, empty, NULL, error ? error : "observation_unavailable", host_data), &truncated);
            json_object_put(empty); continue;
        }
        if (json_object_get_boolean(f_field(host_data, "cached"))) partial = true;
        for (j = 0; j < json_object_array_length(tasks); j++) add_task_items(items, host_name, host_data, json_object_array_get_idx(tasks, j), &truncated);
    }
    json_object_array_sort(items, item_order); data = json_object_new_object();
    json_object_object_add(data, "snapshot_schema_version", json_object_new_int(1)); json_object_object_add(data, "items", items);
    json_object_object_add(data, "partial", json_object_new_boolean(partial || truncated)); json_object_object_add(data, "truncated", json_object_new_boolean(truncated));
    return f_success("fleet-attention", data);
}
