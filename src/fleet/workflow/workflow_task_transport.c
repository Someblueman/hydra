#include "fleet/workflow/workflow_task.h"
#include "fleet/transport/remote.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/task/task.h"
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static bool nonnegative(json_object *object, const char *key, int64_t *out) {
    json_object *value = f_field(object, key);
    if (!json_object_is_type(value, json_type_int) || json_object_get_int64(value) < 0 ||
        json_object_get_uint64(value) > INT64_MAX) return false;
    *out = json_object_get_int64(value); return true;
}
static bool transfer_read(const char *directory, struct wt_transfer *transfer) {
    const char *const keys[] = {"schema_version", "calls", "request_bytes", "response_bytes", "complete", NULL};
    json_object *record = wt_metric_record(directory, "transport-metrics.json", 16384);
    bool valid = task_keys(record, keys) && f_number_is(record, "schema_version", 1) &&
        nonnegative(record, "calls", &transfer->calls) && transfer->calls > 0 &&
        nonnegative(record, "request_bytes", &transfer->requests) && nonnegative(record, "response_bytes", &transfer->responses) &&
        json_object_is_type(f_field(record, "complete"), json_type_boolean);
    if (valid) transfer->complete = json_object_get_boolean(f_field(record, "complete"));
    json_object_put(record); return valid;
}
bool wt_transfer_read(const char *directory, struct wt_transfer *transfer) {
    char path[F_PATH]; struct stat st;
    if (f_path(path, sizeof(path), directory, "transport-incomplete") || !lstat(path, &st) || errno != ENOENT) return false;
    return transfer_read(directory, transfer);
}
static bool mark_transport(const char *directory, bool *historical_gap) {
    char path[F_PATH]; struct stat st; *historical_gap = false;
    if (!directory || f_path(path, sizeof(path), directory, "transport-incomplete")) return false;
    if (!lstat(path, &st)) { *historical_gap = true; return true; }
    if (errno != ENOENT || f_write(path, "incomplete\n", 11, false)) return false;
    return !task_sync_dir(directory);
}
static bool record_transport(const char *directory, size_t request_bytes, size_t response_bytes, bool complete) {
    char path[F_PATH]; struct stat st; struct wt_transfer total = {.complete = true};
    if (f_path(path, sizeof(path), directory, "transport-metrics.json")) return false;
    if (!lstat(path, &st)) { if (!transfer_read(directory, &total)) return false; }
    else if (errno != ENOENT) return false;
    if (total.calls == INT64_MAX || request_bytes > (uint64_t)(INT64_MAX - total.requests) ||
        response_bytes > (uint64_t)(INT64_MAX - total.responses)) return false;
    json_object *record = json_object_new_object();
    json_object_object_add(record, "schema_version", json_object_new_int(1));
    json_object_object_add(record, "calls", json_object_new_int64(total.calls + 1));
    json_object_object_add(record, "request_bytes", json_object_new_int64(total.requests + (int64_t)request_bytes));
    json_object_object_add(record, "response_bytes", json_object_new_int64(total.responses + (int64_t)response_bytes));
    json_object_object_add(record, "complete", json_object_new_boolean(total.complete && complete));
    int status = task_write_json(directory, "transport-metrics.json", record, true);
    json_object_put(record); return !status;
}

static void finish_transport(const char *metrics_dir, size_t request_bytes, size_t response_bytes,
                              bool request_complete, bool historical_gap) {
    if (record_transport(metrics_dir, request_bytes, response_bytes, request_complete) && !historical_gap) {
        char marker[F_PATH];
        if (!f_path(marker, sizeof(marker), metrics_dir, "transport-incomplete") && !unlink(marker)) (void)task_sync_dir(metrics_dir);
    }
}

json_object *wt_request(json_object *destination, json_object *request, unsigned seconds,
                        const char *metrics_dir) {
    const char *kind = f_string(destination, "kind"), *alias = f_string(destination, "alias");
    json_object *current = wt_destination(alias ? alias : "local"), *response = NULL;
    bool matches = current && json_object_equal(current, destination);
    json_object_put(current);
    if (!matches || !kind) return f_error("workflow task", "placement_changed", "the recorded destination no longer matches its registered transport");
    size_t request_bytes = 0, response_bytes = 0; bool request_complete = false, historical_gap = false;
    bool metrics_guard = mark_transport(metrics_dir, &historical_gap);
    if (!metrics_guard) return f_error("workflow task", "metrics_record_failed", "cannot durably record transport intent; no request was sent");
    if (!strcmp(kind, "local")) {
        struct f_capture cap = {0}; char *argv[] = {(char *)f_hydra, "fleet", "serve", NULL};
        const char *text = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
        if (!f_run(argv, text, strlen(text), seconds, &cap)) response = f_parse(cap.out);
        request_bytes = cap.in_bytes; response_bytes = cap.out_bytes; request_complete = cap.measurement_complete;
        f_capture_free(&cap);
    } else {
        struct f_remote remote = {0};
        if (!f_copy(remote.name, sizeof(remote.name), alias) &&
            !f_copy(remote.target, sizeof(remote.target), f_string(destination, "target")) &&
            !f_copy(remote.home, sizeof(remote.home), f_string(destination, "home")) &&
            !f_copy(remote.hydra, sizeof(remote.hydra), f_string(destination, "hydra"))) {
            remote.multiplex = json_object_get_boolean(f_field(destination, "multiplex"));
            response = f_request_measured(&remote, request, seconds, &request_bytes, &response_bytes, &request_complete);
        }
    }
    finish_transport(metrics_dir, request_bytes, response_bytes, request_complete, historical_gap);
    return response ? response : f_error("workflow task", "outcome_unknown", "transport did not return a valid response; retain the original dispatch identity");
}
