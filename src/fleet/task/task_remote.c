#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/transport/remote.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>

static bool supported(json_object *handshake, const char *capability) {
    json_object *data = f_field(handshake, "data"), *caps = f_field(data, "capabilities"); size_t i;
    if (!f_number_is(data, "task_protocol", 1) || !json_object_is_type(caps, json_type_array)) return false;
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *name = f_text(json_object_array_get_idx(caps, i));
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
struct remote_options {
    const char *host, *output, *timeout, *input, *key, *id, *trust;
    const char *stream, *offset, *limit, *source, *step, *attempt;
    const char *request_id, *decision, *actor;
    bool submit, start, cancel, logs, resume, decide, result_read;
    unsigned log_offset, log_limit, seconds;
};

static bool number(const char *text, unsigned minimum, unsigned maximum, unsigned *result) {
    unsigned long value = strtoul(text, NULL, 10);
    if (!*text || strspn(text, "0123456789") != strlen(text) || value < minimum || value > maximum) return false;
    *result = (unsigned)value;
    return true;
}
static json_object *parse_limits(struct remote_options *options) {
    unsigned attempt;
    if (options->timeout && !number(options->timeout, 1, 300, &options->seconds))
        return f_error(options->cancel ? "fleet-task-cancel" : "fleet-task-result", "invalid_input", "timeout must be 1-300 seconds");
    if (!options->logs) return NULL;
    if (options->attempt && !number(options->attempt, 1, 10000, &attempt))
        return f_error("fleet-task-logs", "invalid_input", "attempt must be 1-10000");
    if (options->offset && !number(options->offset, 0, TASK_FILE_LIMIT, &options->log_offset))
        return f_error("fleet-task-logs", "invalid_input", "offset must be 0-524288");
    if (options->limit && !number(options->limit, 1, 65536, &options->log_limit))
        return f_error("fleet-task-logs", "invalid_input", "limit must be 1-65536");
    return NULL;
}
static json_object *parse_options(int argc, char **argv, struct remote_options *options) {
    int i;
    *options = (struct remote_options){0};
    options->submit = !strcmp(argv[0], "submit");
    options->start = !strcmp(argv[0], "start");
    options->cancel = !strcmp(argv[0], "cancel");
    options->logs = !strcmp(argv[0], "logs");
    options->resume = !strcmp(argv[0], "resume");
    options->decide = !strcmp(argv[0], "decide");
    options->result_read = !strcmp(argv[0], "result");
    options->seconds = (options->result_read || options->cancel) ? 30 : 5;
    options->log_limit = 4096;
    if (argc < 2 || !f_name(argv[1])) return f_error("fleet-task", "invalid_input", "select a registered host alias");
    options->host = argv[1];
    const struct {
        const char *name;
        bool allowed;
        const char **destination;
    } fields[] = {
        {"--output", options->result_read, &options->output},
        {"--timeout", options->result_read || options->cancel, &options->timeout},
        {"--input", options->submit, &options->input},
        {"--key", options->submit, &options->key},
        {"--id", !options->submit, &options->id},
        {"--trust-spec", options->start || options->submit || options->resume || options->decide, &options->trust},
        {"--request", options->decide, &options->request_id},
        {"--decision", options->decide, &options->decision},
        {"--by", options->decide, &options->actor},
        {"--source", options->logs, &options->source},
        {"--step", options->logs, &options->step},
        {"--attempt", options->logs, &options->attempt},
        {"--stream", options->logs, &options->stream},
        {"--offset", options->logs, &options->offset},
        {"--limit", options->logs, &options->limit}
    };
    for (i = 2; i < argc; i++) {
        const char **destination = NULL;
        size_t field;
        for (field = 0; field < sizeof(fields) / sizeof(fields[0]); field++) {
            if (fields[field].allowed && !strcmp(argv[i], fields[field].name)) {
                destination = fields[field].destination;
                break;
            }
        }
        if (!destination) return f_error("fleet-task", "invalid_input", "use hydra fleet task help");
        if (*destination || ++i == argc || !*argv[i]) return f_error("fleet-task", "invalid_input", "each option requires one value");
        *destination = argv[i];
    }
    if ((options->submit ? !options->input || !options->key : !options->id) || ((options->start || options->resume || options->decide) && !options->trust) || (options->decide && (!options->request_id || !options->decision))) return f_error("fleet-task", "invalid_input", "required task options are missing");
    return parse_limits(options);
}
/* Returns a new request; package is borrowed. */
static json_object *make_request(const struct remote_options *options, const char *operation, json_object *package) {
    json_object *request = json_object_new_object();
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(request, "action", "task");
    f_string_add(request, "operation", operation);
    if (options->request_id) f_string_add(request, "request_id", options->request_id);
    if (options->decision) f_string_add(request, "decision", options->decision);
    if (options->actor) f_string_add(request, "by", options->actor);
    if (options->trust) f_string_add(request, "trust_spec", options->trust);
    if (options->logs) {
        if (options->source) f_string_add(request, "source", options->source);
        if (options->step) f_string_add(request, "step", options->step);
        if (options->attempt) json_object_object_add(request, "attempt", json_object_new_int64(strtol(options->attempt, NULL, 10)));
        f_string_add(request, "stream", options->stream ? options->stream : "stdout");
        json_object_object_add(request, "offset", json_object_new_int64(options->log_offset));
        json_object_object_add(request, "limit", json_object_new_int64(options->log_limit));
    }
    if (options->submit) { json_object_object_add(request, "package", json_object_get(package)); f_string_add(request, "submission_key", options->key); }
    else f_string_add(request, "task_id", options->id);
    return request;
}
/* Consumes response and returns its verified/replaced envelope. */
static json_object *result_output(const struct remote_options *options, json_object *response) {
    json_object *data = f_field(response, "data"), *envelope = f_field(data, "collection"), *checked = task_result_verify(envelope);
    const char *bound = f_string(f_field(f_field(envelope, "result"), "receipt"), "task_id");
    const char *digest = f_string(f_field(f_field(envelope, "result"), "receipt"), "spec_sha256");
    if (!json_object_get_boolean(f_field(checked, "ok")) || !bound || strcmp(bound, options->id) || !digest || strcmp(digest, f_string(data, "spec_sha256"))) {
        json_object_put(response); response = f_error("fleet-task-result", "invalid_result", "the received snapshot failed independent validation or task binding");
    } else if (options->output) {
        const char *text = json_object_to_json_string_ext(envelope, JSON_C_TO_STRING_PLAIN);
        if (f_write(options->output, text, strlen(text), false)) { json_object_put(response); response = f_error("fleet-task-result", "io_failed", "cannot create a new private result file; existing files are never replaced"); }
        else { f_string_add(data, "file", options->output); f_string_add(data, "result_sha256", f_string(envelope, "result_sha256")); json_object_object_del(data, "collection"); }
    }
    json_object_put(checked);
    return response;
}
json_object *task_remote_cli(int argc, char **argv) {
    struct remote_options parsed, *options = &parsed;
    struct f_remote remote; char capability[32];
    json_object *package = NULL, *checked = NULL, *request = NULL;
    json_object *response = parse_options(argc, argv, options);
    if (response) return response;
    if (f_remote_load(options->host, &remote)) return f_error("fleet-task", "invalid_alias", "register the remote before submitting a task");
    if (options->submit) {
        json_object *spec;
        package = f_read_json(options->input, TASK_PACKAGE_LIMIT); checked = task_inspect(package);
        if (!json_object_get_boolean(f_field(checked, "ok"))) { response = checked; checked = NULL; goto done; }
        spec = f_field(checked, "data");
        if (strcmp(options->host, f_string(spec, "host"))) { response = f_error("fleet-task-submit", "placement_mismatch", "the selected alias differs from the immutable task destination"); goto done; }
        options->seconds = (unsigned)json_object_get_int(f_field(f_field(spec, "limits"), "transport_seconds"));
    }
    response = f_observe(&remote, "handshake", options->seconds);
    if (!json_object_get_boolean(f_field(response, "ok"))) goto done;
    snprintf(capability, sizeof(capability), "task-%s", options->submit ? "accept" : argv[0]);
    if (!supported(response, capability) || (options->submit && options->trust && !supported(response, "task-start"))) {
        json_object_put(response); response = f_error("fleet-task", "capability_unavailable", "the receiver lacks the requested task protocol 1 capability"); goto done;
    }
    json_object_put(response);
    request = make_request(options, argv[0], package);
    response = f_request(&remote, request, options->seconds);
    if (json_object_get_boolean(f_field(response, "ok"))) {
        json_object *data = f_field(response, "data"); const char *received = f_string(data, "task_id"), *digest = f_string(data, "spec_sha256"), *received_key = f_string(data, "submission_key");
        bool valid = received && !strncmp(received, "task_", 5) && task_hex(received + 5, 64) && task_hex(digest, 64);
        if (valid) valid = options->submit ? received_key && !strcmp(received_key, options->key) && !strcmp(digest, f_string(package, "spec_sha256")) : !strcmp(received, options->id);
        if (!valid) { json_object_put(response); response = f_error("fleet-task", "invalid_response", "the receiver returned a task handle with inconsistent bindings"); }
    }
    if (options->result_read && json_object_get_boolean(f_field(response, "ok"))) response = result_output(options, response);
    if ((options->submit || options->start || options->cancel || options->resume || options->decide) && uncertain(response)) {
        json_object *wrapped = f_error("fleet-task", "outcome_unknown", options->submit ? "the acceptance response was lost or invalid; retry this same package and key to reconcile, never invent a new key" : "the mutation response was lost or invalid; inspect this task's status; execution is never replayed");
        json_object_object_add(f_field(wrapped, "error"), "cause", json_object_get(f_field(response, "error")));
        json_object_put(response); response = wrapped;
    }
done:
    if (response) f_string_add(response, "host", options->host);
    json_object_put(checked); json_object_put(package); json_object_put(request); return response;
}
