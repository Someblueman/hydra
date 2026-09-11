#include "fleet/discovery/discovery.h"
#include "fleet/enrollment/enrollment.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <limits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool resolve_rows(json_object *rows, const struct hd_options *options, const char *config) {
    bool failed = false;
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        json_object *resolved = f_stopped ? f_error("host-resolve", "cancelled", "selection interrupted before resolution") :
            hd_resolve(f_string(row, "target"), config, options->seconds);
        bool ok = json_object_get_boolean(f_field(resolved, "ok"));
        json_object_object_add(row, "resolution", resolved);
        f_string_add(row, "status", options->probe ? "not_qualified" : (ok ? "discovered" : "resolution_failed"));
        if (options->config) f_string_add(row, "ssh_config", options->config);
        if (!ok) failed = true;
    }
    return failed;
}
static int next_row(json_object *rows, const bool visited[HD_HOSTS]) {
    int best = -1; int64_t attempts = INT64_MAX;
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        int64_t count = json_object_get_int64(f_field(row, "qualification_attempts"));
        if (visited[i] || !strcmp(f_string(row, "status"), "compatible")) continue;
        if (count < attempts) { best = (int)i; attempts = count; }
    }
    return best;
}
static void retain_failure(json_object *row) {
    json_object *previous = f_field(row, "qualification"), *history = f_field(row, "qualification_history");
    if (!previous) return;
    if (!history) { history = json_object_new_array(); json_object_object_add(row, "qualification_history", history); }
    if (json_object_array_length(history) == 4) {
        json_object_array_del_idx(history, 0, 1);
        json_object_object_add(row, "qualification_history_truncated", json_object_new_boolean(true));
    }
    json_object *entry = json_object_new_object();
    json_object_object_add(entry, "qualified_at", json_object_get(f_field(row, "qualified_at")));
    json_object_object_add(entry, "result", json_object_get(previous));
    json_object_array_add(history, entry);
}
static void qualify_row(json_object *row, const struct hd_options *options, const char *config) {
    retain_failure(row);
    json_object *probe = json_object_get_boolean(f_field(f_field(row, "resolution"), "ok")) ?
        hd_probe(row, config, options) : f_error("fleet-qualify", "not_checked", "effective SSH policy is unavailable");
    int64_t attempts = json_object_get_int64(f_field(row, "qualification_attempts"));
    json_object_object_add(row, "qualification", probe);
    json_object_object_add(row, "qualification_attempts", json_object_new_int64(attempts + 1));
    json_object_object_add(row, "qualified_at", json_object_new_int64((int64_t)time(NULL)));
    f_string_add(row, "qualification_freshness", "observed_now");
    f_string_add(row, "status", json_object_get_boolean(f_field(probe, "ok")) ? "compatible" : "qualification_failed");
}
static json_object *summary(const struct hd_options *options, json_object *rows, size_t processed, bool failed) {
    json_object *data = json_object_new_object(), *result; size_t remaining = 0, pending = 0;
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        const char *status = f_string(json_object_array_get_idx(rows, i), "status");
        if (!strcmp(status, "not_qualified")) pending++;
        if (options->probe && strcmp(status, "compatible")) remaining++;
        if (!strcmp(status, "qualification_failed")) failed = true;
    }
    json_object_object_add(data, "candidate_schema_version", json_object_new_int(1));
    json_object_object_add(data, "observed_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(data, "candidates", json_object_get(rows));
    json_object_object_add(data, "qualification_batch_size", json_object_new_int(HD_QUALIFY_BATCH));
    json_object_object_add(data, "processed_this_batch", json_object_new_int64((int64_t)processed));
    json_object_object_add(data, "pending_count", json_object_new_int64((int64_t)pending));
    json_object_object_add(data, "remaining_count", json_object_new_int64((int64_t)remaining));
    json_object_object_add(data, "complete", json_object_new_boolean(!remaining && !failed));
    json_object_object_add(data, "partial_failure", json_object_new_boolean(failed));
    f_string_add(data, "required_capability", options->probe ? options->capability : "");
    result = failed ? f_error("fleet-discovery", f_stopped ? "cancelled" : "partial_failure", "one or more selected candidates could not be resolved or qualified") : f_success("fleet-discovery", NULL);
    json_object_object_add(result, "data", data); return result;
}
static void cancelled_pending(json_object *rows) {
    if (!f_stopped) return;
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (strcmp(f_string(row, "status"), "not_qualified")) continue;
        json_object_object_add(row, "qualification", f_error("fleet-qualify", "cancelled", "selection interrupted before qualification"));
    }
}
static int qualify_batch(struct hd_progress *progress, const struct hd_options *options,
                         json_object *rows, const char *config, size_t *processed) {
    bool visited[HD_HOSTS] = {false};
    if (hd_progress_save(progress, rows)) return -1;
    for (; *processed < HD_QUALIFY_BATCH && !f_stopped; (*processed)++) {
        int next = next_row(rows, visited);
        if (next < 0) break;
        visited[next] = true;
        qualify_row(json_object_array_get_idx(rows, (size_t)next), options, config);
        if (hd_progress_save(progress, rows)) return -1;
    }
    cancelled_pending(rows);
    return hd_progress_save(progress, rows);
}
static json_object *prepare_batch(const struct hd_options *options, json_object *rows, char config[F_PATH]) {
    if (options->probe && json_object_array_length(rows) > HD_QUALIFY_BATCH && !options->progress)
        return f_error("fleet-discovery", "progress_required", "more than 16 selected candidates require --progress /absolute/export.json");
    if (hd_config(config, options->config)) return f_error("fleet-discovery", "ssh_config_failed", "cannot prepare private strict SSH config");
    return NULL;
}
json_object *hd_batch(const struct hd_options *options, json_object *rows) {
    struct hd_progress progress = {.lock = -1}; char config[F_PATH] = "";
    json_object *result = NULL; bool failed; size_t processed = 0;
    result = prepare_batch(options, rows, config);
    if (result) goto done;
    failed = resolve_rows(rows, options, config);
    result = hd_progress_open(&progress, options, rows);
    if (result) goto done;
    if (options->probe && qualify_batch(&progress, options, rows, config, &processed)) goto io_error;
    result = summary(options, rows, processed, failed || f_stopped);
    if (options->progress && enrollment_write(options->progress, result)) {
        json_object_put(result); result = NULL; goto io_error;
    }
    goto done;
io_error:
    result = f_error("fleet-discovery", "progress_io_failed", "cannot durably save qualification progress; completed private records are retained");
done:
    hd_progress_close(&progress);
    if (config[0]) unlink(config);
    json_object_put(rows); return result;
}
