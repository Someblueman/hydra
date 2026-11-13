#include "fleet/enrollment/enrollment.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <string.h>
#include <unistd.h>

/* `fleet enroll status --input INTENT`: the reviewed hosts of an intent with
 * the per-host apply progress Hydra recorded for its digest, read without
 * the apply lock. Nothing here connects, retries or changes a row: a host
 * that was never attempted is `not_started`, an interrupted or lost attempt
 * keeps the status apply recorded (`outcome_unknown` is never resolved by
 * reading), and a binding revoked since review shows as `revoked`, as apply
 * would mark it before any connection. */
static bool intent_shape(json_object *intent) {
    char actual[65]; const char *digest = f_string(intent, "intent_sha256");
    json_object *hosts = f_field(intent, "hosts"), *copy = NULL; bool valid;
    if (!enrollment_digest(digest) || !f_number_is(intent, "schema_version", 2) || !json_object_is_type(hosts, json_type_array) ||
        !json_object_array_length(hosts) || json_object_array_length(hosts) > ENROLL_HOSTS) return false;
    if (json_object_deep_copy(intent, &copy, NULL)) return false;
    json_object_object_del(copy, "intent_sha256");
    valid = !enrollment_hash(copy, actual) && !strcmp(actual, digest);
    json_object_put(copy); return valid;
}
static json_object *progress_row(json_object *progress, size_t index, const char *alias) {
    json_object *rows = f_field(progress, "hosts"), *row;
    if (!json_object_is_type(rows, json_type_array) || index >= json_object_array_length(rows)) return NULL;
    row = json_object_array_get_idx(rows, index);
    return f_string(row, "alias") && !strcmp(f_string(row, "alias"), alias) ? row : NULL;
}
static void copy_field(json_object *into, json_object *from, const char *key) {
    if (f_field(from, key)) json_object_object_add(into, key, json_object_get(f_field(from, key)));
}
static json_object *status_row(json_object *host, json_object *progress, size_t index, const char *digest) {
    static const char *const intent_keys[] = {"alias", "candidate_id", "target", "principal", "project", "fingerprint",
                                              "required_capability", "package_sha256", "prefix", "ssh_config", NULL};
    static const char *const progress_keys[] = {"operation_id", "status", "phase", "host_key", "revocation", NULL};
    json_object *out = json_object_new_object(), *row = progress_row(progress, index, f_string(host, "alias")), *revoked;
    const char *status; size_t i; char operation[256];
    for (i = 0; intent_keys[i]; i++) copy_field(out, host, intent_keys[i]);
    for (i = 0; progress_keys[i]; i++) copy_field(out, row, progress_keys[i]);
    if (!row) {
        snprintf(operation, sizeof(operation), "%s:%.120s", digest, f_string(host, "alias"));
        f_string_add(out, "operation_id", operation); f_string_add(out, "status", "not_started"); f_string_add(out, "phase", "preflight");
    }
    status = f_string(out, "status");
    if ((!status || strcmp(status, "enrolled")) && (revoked = enrollment_host_revocation(host))) {
        f_string_add(out, "status", "revoked"); json_object_object_add(out, "revocation", revoked);
    }
    json_object_object_add(out, "attempted", json_object_new_boolean(row != NULL));
    return out;
}
json_object *enrollment_status(const struct enrollment_options *options) {
    json_object *intent, *progress = NULL, *data, *hosts, *rows; char path[F_PATH], directory[F_PATH]; size_t i;
    const char *digest;
    if (!options->input || options->output || options->count || options->project || options->alias || options->package ||
        options->sha256 || options->prefix || options->confirm)
        return f_error("fleet-enrollment-status", "invalid_input", "status reads only --input INTENT");
    intent = f_read_json(options->input, F_LIMIT);
    if (!intent_shape(intent)) { json_object_put(intent); return f_error("fleet-enrollment-status", "invalid_intent", "the input is not a complete reviewed enrollment intent; create a fresh review"); }
    digest = f_string(intent, "intent_sha256");
    if (f_path(directory, sizeof(directory), f_home, "fleet/enrollment") || snprintf(path, sizeof(path), "%s/%s.json", directory, digest) >= (int)sizeof(path)) {
        json_object_put(intent); return f_error("fleet-enrollment-status", "state_unavailable", "enrollment progress path exceeds limits");
    }
    if (!access(path, F_OK)) {
        progress = f_read_json(path, F_LIMIT);
        if (!f_number_is(progress, "schema_version", 1) || !f_string(progress, "intent_sha256") || strcmp(f_string(progress, "intent_sha256"), digest)) {
            json_object_put(progress); json_object_put(intent);
            return f_error("fleet-enrollment-status", "state_unavailable", "recorded enrollment progress is unreadable or belongs to another intent; preserve it for inspection");
        }
    }
    data = json_object_new_object(); rows = json_object_new_array(); hosts = f_field(intent, "hosts");
    json_object_object_add(data, "enrollment_status_schema", json_object_new_int(1));
    f_string_add(data, "intent_sha256", digest); f_string_add(data, "intent", options->input);
    json_object_object_add(data, "reviewed_at", json_object_get(f_field(intent, "reviewed_at")));
    json_object_object_add(data, "progress_recorded", json_object_new_boolean(progress != NULL));
    if (progress) f_string_add(data, "progress", path); else json_object_object_add(data, "progress", NULL);
    json_object_object_add(data, "partial_failure", progress && f_field(progress, "partial_failure") ? json_object_get(f_field(progress, "partial_failure")) : NULL);
    for (i = 0; i < json_object_array_length(hosts); i++)
        json_object_array_add(rows, status_row(json_object_array_get_idx(hosts, i), progress, i, digest));
    json_object_object_add(data, "hosts", rows);
    json_object_put(progress); json_object_put(intent);
    return f_success("fleet-enrollment-status", data);
}
