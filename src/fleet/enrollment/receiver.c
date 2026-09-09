#include "fleet/enrollment/receiver.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/server.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static bool operation_name(const char *operation, char name[256]) {
    size_t i;
    if (!operation || strlen(operation) < 66 || strlen(operation) >= 256 ||
        operation[64] != ':' || !f_name(operation + 65)) return false;
    for (i = 0; i < 64; i++) if (!strchr("0123456789abcdef", operation[i])) return false;
    if (f_copy(name, 256, operation)) return false;
    name[64] = '_';
    return true;
}

/* Store the exact effect-bearing request, independently of JSON key order.
 * A reused operation ID cannot authorize another project or init argument. */
static json_object *request_binding(json_object *request) {
    const char *const keys[] = {"protocol", "action", "project", "args", NULL};
    json_object *binding = json_object_new_object(); size_t i;
    for (i = 0; keys[i]; i++)
        json_object_object_add(binding, keys[i], json_object_get(f_field(request, keys[i])));
    return binding;
}

static int save_record(const char *path, const char *directory, json_object *record, bool replace) {
    const char *text = json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN);
    int fd, status;
    if (f_write(path, text, strlen(text), replace)) return -1;
    fd = open(directory, O_RDONLY);
    if (fd < 0) return -1;
    status = fsync(fd); close(fd);
    return status;
}

static json_object *reconcile(const char *path, json_object *binding) {
    json_object *record = f_read_json(path, F_LIMIT), *result = NULL;
    const char *state = f_string(record, "state");
    if (!record || !f_number_is(record, "schema_version", 1))
        result = f_error("fleet-enrollment", "outcome_unknown", "receiver operation record is unavailable or unreadable");
    else if (!json_object_equal(f_field(record, "request"), binding))
        result = f_error("fleet-enrollment", "intent_changed", "operation ID is already bound to another request; renewed review is required");
    else if (state && !strcmp(state, "completed") &&
             json_object_is_type(f_field(f_field(record, "result"), "ok"), json_type_boolean))
        result = json_object_get(f_field(record, "result"));
    else
        result = f_error("fleet-enrollment", "outcome_unknown", "receiver operation remains pending; its outcome cannot be inferred or replayed");
    json_object_put(record); return result;
}

json_object *enrollment_apply_request(json_object *request, char **argv, unsigned seconds) {
    char directory[F_PATH], path[F_PATH], name[256];
    const char *operation = f_string(request, "enrollment_operation_id");
    json_object *binding, *record, *result;
    if (!operation_name(operation, name))
        return f_error("fleet-enrollment", "invalid_input", "enrollment operation ID must bind a review digest and alias");
    if (f_path(directory, sizeof(directory), f_home, "fleet/enrollment-ops") ||
        f_mkdirs(directory) || f_path(path, sizeof(path), directory, name))
        return f_error("fleet-enrollment", "state_unavailable", "cannot establish durable receiver operation state");
    binding = request_binding(request);
    record = json_object_new_object();
    json_object_object_add(record, "schema_version", json_object_new_int(1));
    f_string_add(record, "operation_id", operation); f_string_add(record, "state", "pending");
    json_object_object_add(record, "request", json_object_get(binding));
    /* Atomic create is the sole claim. Once present, even owner death never
     * permits another caller to repeat a possibly completed mutation. */
    if (save_record(path, directory, record, false)) {
        result = reconcile(path, binding);
    } else {
        result = f_run_hydra(argv, seconds);
        f_string_add(record, "state", "completed");
        json_object_object_add(record, "result", json_object_get(result));
        if (save_record(path, directory, record, true)) {
            json_object_put(result);
            result = f_error("fleet-enrollment", "outcome_unknown", "mutation returned but its result could not be recorded durably");
        }
    }
    json_object_put(record); json_object_put(binding); return result;
}
