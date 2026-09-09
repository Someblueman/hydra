#include "fleet/enrollment/enrollment.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include "fleet/support/process.h"
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static bool intent_valid(json_object *intent, const char *confirm) {
    char actual[65]; const char *digest = f_string(intent, "intent_sha256");
    json_object *hosts = f_field(intent, "hosts"), *copy = NULL; bool valid = false;
    if (!enrollment_digest(digest) || !confirm || strcmp(confirm, digest) ||
        !f_number_is(intent, "schema_version", 2) || !json_object_is_type(hosts, json_type_array) ||
        !json_object_array_length(hosts) || json_object_array_length(hosts) > ENROLL_HOSTS) return false;
    if (json_object_deep_copy(intent, &copy, NULL)) return false;
    json_object_object_del(copy, "intent_sha256");
    valid = !enrollment_hash(copy, actual) && !strcmp(actual, digest);
    json_object_put(copy); return valid;
}
static json_object *new_progress(json_object *intent) {
    json_object *progress = json_object_new_object(), *rows = json_object_new_array();
    json_object *hosts = f_field(intent, "hosts"); const char *digest = f_string(intent, "intent_sha256"); size_t i;
    json_object_object_add(progress, "schema_version", json_object_new_int(1));
    f_string_add(progress, "intent_sha256", digest); json_object_object_add(progress, "hosts", rows);
    for (i = 0; i < json_object_array_length(hosts); i++) {
        json_object *host = json_object_array_get_idx(hosts, i), *row = json_object_new_object(); char operation[256];
        const char *alias = f_string(host, "alias");
        if (!f_name(alias) || snprintf(operation, sizeof(operation), "%s:%s", digest, alias) >= (int)sizeof(operation)) {
            json_object_put(row); json_object_put(progress); return NULL;
        }
        f_string_add(row, "alias", alias); f_string_add(row, "operation_id", operation);
        f_string_add(row, "status", "not_started"); f_string_add(row, "phase", "preflight");
        json_object_array_add(rows, row);
    }
    return progress;
}
static bool progress_valid(json_object *progress, json_object *intent) {
    json_object *expected = new_progress(intent), *rows = f_field(progress, "hosts"), *expected_rows = f_field(expected, "hosts");
    const char *digest = f_string(progress, "intent_sha256"); size_t i; bool valid = false;
    if (!expected || !f_number_is(progress, "schema_version", 1) || !digest ||
        strcmp(digest, f_string(intent, "intent_sha256")) || !json_object_is_type(rows, json_type_array) ||
        json_object_array_length(rows) != json_object_array_length(expected_rows)) goto done;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i), *ref = json_object_array_get_idx(expected_rows, i);
        const char *phase = f_string(row, "phase");
        if (!json_object_equal(f_field(row, "alias"), f_field(ref, "alias")) ||
            !json_object_equal(f_field(row, "operation_id"), f_field(ref, "operation_id")) ||
            !f_string(row, "status") || !phase ||
            (strcmp(phase, "preflight") && strcmp(phase, "install") && strcmp(phase, "init") && strcmp(phase, "alias") && strcmp(phase, "complete"))) goto done;
    }
    valid = true;
done:
    json_object_put(expected); return valid;
}
static int progress_lock(const char *digest, char path[F_PATH]) {
    char directory[F_PATH], lock[F_PATH]; int fd;
    if (f_path(directory, sizeof(directory), f_home, "fleet/enrollment") || f_mkdirs(directory) ||
        snprintf(path, F_PATH, "%s/%s.json", directory, digest) >= F_PATH ||
        snprintf(lock, sizeof(lock), "%s.lock", path) >= (int)sizeof(lock)) return -1;
    fd = open(lock, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) || flock(fd, LOCK_EX | LOCK_NB)) { close(fd); return -1; }
    return fd;
}
static json_object *apply_hosts(json_object *intent, json_object *progress, const char *path, unsigned seconds) {
    json_object *hosts = f_field(intent, "hosts"), *rows = f_field(progress, "hosts"); size_t i; bool partial = false;
    for (i = 0; i < json_object_array_length(hosts); i++) {
        json_object *row = json_object_array_get_idx(rows, i), *failure;
        if (!strcmp(f_string(row, "status"), "enrolled")) continue;
        if (f_stopped) { partial = true; break; }
        failure = enrollment_host_apply(json_object_array_get_idx(hosts, i), row, progress, path, seconds);
        if (failure) return failure;
        if (strcmp(f_string(row, "status"), "enrolled")) partial = true;
    }
    json_object_object_add(progress, "partial_failure", json_object_new_boolean(partial));
    if (enrollment_write(path, progress)) return f_error("fleet-enrollment", "state_unavailable", "cannot durably save enrollment results");
    return f_success("fleet-enrollment", json_object_get(progress));
}
json_object *enrollment_apply(const struct enrollment_options *options) {
    json_object *intent = NULL, *progress = NULL, *result; char path[F_PATH]; int lock;
    if (!options->input || options->output || options->count || options->project || options->alias || options->package || options->sha256 || options->prefix)
        return f_error("fleet-enrollment", "invalid_input", "apply requires only a reviewed input and its confirmation digest");
    intent = f_read_json(options->input, F_LIMIT);
    if (!intent_valid(intent, options->confirm)) { json_object_put(intent); return f_error("fleet-enrollment", "invalid_intent", "review or confirmation changed; create and confirm a fresh review"); }
    lock = progress_lock(options->confirm, path);
    if (lock < 0) { json_object_put(intent); return f_error("fleet-enrollment", "apply_in_progress", "cannot acquire exclusive enrollment progress state"); }
    if (!access(path, F_OK)) progress = f_read_json(path, F_LIMIT);
    else progress = new_progress(intent);
    if (!progress_valid(progress, intent) || enrollment_write(path, progress))
        result = f_error("fleet-enrollment", "state_unavailable", "enrollment progress is missing, invalid, or unwritable");
    else result = apply_hosts(intent, progress, path, options->seconds);
    json_object_put(progress); json_object_put(intent); close(lock); return result;
}
