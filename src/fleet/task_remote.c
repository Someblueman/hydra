#include "task.h"
#include <stdlib.h>
#include <string.h>

static bool supported(json_object *handshake, const char *capability) {
    json_object *data = f_field(handshake, "data"), *caps = f_field(data, "capabilities"); size_t i;
    if (!f_number_is(data, "task_protocol", 1) || !json_object_is_type(caps, json_type_array)) return false;
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *name = task_text(json_object_array_get_idx(caps, i));
        if (name && !strcmp(name, capability)) return true;
    }
    return false;
}
static bool uncertain(json_object *response) {
    const char *code = f_string(f_field(response, "error"), "code");
    const char *codes[] = {"timeout", "offline", "cancelled", "invalid_response", "remote_failed", "output_limit", NULL}; size_t i;
    for (i = 0; code && codes[i]; i++) if (!strcmp(code, codes[i])) return true;
    return false;
}
json_object *task_remote_cli(int argc, char **argv) {
    const char *host, *input = NULL, *key = NULL, *id = NULL, *trust = NULL; bool submit = !strcmp(argv[0], "submit"), start = !strcmp(argv[0], "start");
    const char *stream = NULL, *offset = NULL, *limit = NULL, *source = NULL, *step = NULL, *attempt = NULL; bool cancel = !strcmp(argv[0], "cancel"), logs = !strcmp(argv[0], "logs");
    char capability[32]; unsigned log_offset = 0, log_limit = 4096;
    struct f_remote remote; json_object *package = NULL, *checked = NULL, *request = NULL, *response = NULL;
    unsigned seconds = 5; int i;
    if (argc < 2 || !f_name(argv[1])) return f_error("fleet-task", "invalid_input", "select a registered host alias");
    host = argv[1];
    for (i = 2; i < argc; i++) {
        const char **destination;
        if (!strcmp(argv[i], "--input") && submit) destination = &input;
        else if (!strcmp(argv[i], "--key") && submit) destination = &key;
        else if (!strcmp(argv[i], "--id") && !submit) destination = &id;
        else if (!strcmp(argv[i], "--trust-spec") && (start || submit)) destination = &trust;
        else if (!strcmp(argv[i], "--source") && logs) destination = &source;
        else if (!strcmp(argv[i], "--step") && logs) destination = &step;
        else if (!strcmp(argv[i], "--attempt") && logs) destination = &attempt;
        else if (!strcmp(argv[i], "--stream") && logs) destination = &stream;
        else if (!strcmp(argv[i], "--offset") && logs) destination = &offset;
        else if (!strcmp(argv[i], "--limit") && logs) destination = &limit;
        else return f_error("fleet-task", "invalid_input", "use hydra fleet task help");
        if (*destination || ++i == argc || !*argv[i]) return f_error("fleet-task", "invalid_input", "each option requires one value");
        *destination = argv[i];
    }
    if ((submit ? !input || !key : !id) || (start && !trust)) return f_error("fleet-task", "invalid_input", "required task options are missing");
    if (logs) {
        unsigned long value;
        if (attempt && (strspn(attempt, "0123456789") != strlen(attempt) || strtoul(attempt, NULL, 10) < 1 || strtoul(attempt, NULL, 10) > 10000)) return f_error("fleet-task-logs", "invalid_input", "attempt must be 1-10000");
        if (offset) { value = strtoul(offset, NULL, 10); if (strspn(offset, "0123456789") != strlen(offset) || value > TASK_FILE_LIMIT) return f_error("fleet-task-logs", "invalid_input", "offset must be 0-524288"); log_offset = (unsigned)value; }
        if (limit) { value = strtoul(limit, NULL, 10); if (strspn(limit, "0123456789") != strlen(limit) || !value || value > 65536) return f_error("fleet-task-logs", "invalid_input", "limit must be 1-65536"); log_limit = (unsigned)value; }
    }
    if (f_remote_load(host, &remote)) return f_error("fleet-task", "invalid_alias", "register the remote before submitting a task");
    if (submit) {
        json_object *spec;
        package = f_read_json(input, TASK_PACKAGE_LIMIT); checked = task_inspect(package);
        if (!json_object_get_boolean(f_field(checked, "ok"))) { response = checked; checked = NULL; goto done; }
        spec = f_field(checked, "data");
        if (strcmp(host, f_string(spec, "host"))) { response = f_error("fleet-task-submit", "placement_mismatch", "the selected alias differs from the immutable task destination"); goto done; }
        seconds = (unsigned)json_object_get_int(f_field(f_field(spec, "limits"), "transport_seconds"));
    }
    response = f_observe(&remote, "handshake", seconds);
    if (!json_object_get_boolean(f_field(response, "ok"))) goto done;
    snprintf(capability, sizeof(capability), "task-%s", submit ? "accept" : argv[0]);
    if (!supported(response, capability) || (submit && trust && !supported(response, "task-start"))) {
        json_object_put(response); response = f_error("fleet-task", "capability_unavailable", "the receiver lacks the requested task protocol 1 capability"); goto done;
    }
    json_object_put(response); request = json_object_new_object();
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(request, "action", "task");
    f_string_add(request, "operation", argv[0]);
    if (trust) f_string_add(request, "trust_spec", trust);
    if (logs) {
        if (source) f_string_add(request, "source", source);
        if (step) f_string_add(request, "step", step);
        if (attempt) json_object_object_add(request, "attempt", json_object_new_int64(strtol(attempt, NULL, 10)));
        f_string_add(request, "stream", stream ? stream : "stdout");
        json_object_object_add(request, "offset", json_object_new_int64(log_offset));
        json_object_object_add(request, "limit", json_object_new_int64(log_limit));
    }
    if (submit) { json_object_object_add(request, "package", json_object_get(package)); f_string_add(request, "submission_key", key); }
    else f_string_add(request, "task_id", id);
    response = f_request(&remote, request, seconds);
    if (json_object_get_boolean(f_field(response, "ok"))) {
        json_object *data = f_field(response, "data"); const char *received = f_string(data, "task_id"), *digest = f_string(data, "spec_sha256"), *received_key = f_string(data, "submission_key");
        bool valid = received && !strncmp(received, "task_", 5) && task_hex(received + 5, 64) && task_hex(digest, 64);
        if (valid) valid = submit ? received_key && !strcmp(received_key, key) && !strcmp(digest, f_string(package, "spec_sha256")) : !strcmp(received, id);
        if (!valid) { json_object_put(response); response = f_error("fleet-task", "invalid_response", "the receiver returned a task handle with inconsistent bindings"); }
    }
    if ((submit || start || cancel) && uncertain(response)) {
        json_object *wrapped = f_error("fleet-task", "outcome_unknown", submit ? "the acceptance response was lost or invalid; retry this same package and key to reconcile, never invent a new key" : "the mutation response was lost or invalid; inspect this task's status; execution is never replayed");
        json_object_object_add(f_field(wrapped, "error"), "cause", json_object_get(f_field(response, "error")));
        json_object_put(response); response = wrapped;
    }
done:
    if (response) f_string_add(response, "host", host);
    json_object_put(checked); json_object_put(package); json_object_put(request); return response;
}
