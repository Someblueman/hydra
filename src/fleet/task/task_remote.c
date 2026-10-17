#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/transport/remote.h"
#include "fleet/task/task.h"
#include "fleet/placement/placement.h"
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
    const char *request_id, *decision, *actor, *placement;
    const char *cursor, *event_limit, *byte_offset, *stream_id;
    const char *separate[PLACEMENT_SEPARATIONS];
    bool submit, start, cancel, logs, resume, decide, result_read, observe, withdraw;
    unsigned log_offset, log_limit, seconds, generation, separations;
    unsigned event_cursor, event_count;
};
static bool observation_text(json_object *object, const char *key, size_t limit, bool nullable_value) {
    json_object *value = f_field(object, key); const char *text = f_text(value);
    const unsigned char *p;
    if (nullable_value && json_object_is_type(value, json_type_null)) return true;
    if (!text || strlen(text) >= limit) return false;
    for (p = (const unsigned char *)text; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}

static bool observation_time(json_object *object, const char *key) {
    json_object *value = f_field(object, key);
    return json_object_is_type(value, json_type_null) ||
        (json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0);
}

static bool bounded_string_array(json_object *array, size_t limit) {
    size_t i;
    if (!json_object_is_type(array, json_type_array) || json_object_array_length(array) > limit) return false;
    for (i = 0; i < json_object_array_length(array); i++) {
        const unsigned char *p; const char *value = f_text(json_object_array_get_idx(array, i));
        if (!value || strlen(value) >= 1024) return false;
        for (p = (const unsigned char *)value; *p; p++) if (*p < 32 || *p == 127) return false;
    }
    return true;
}

static bool pending_requests_valid(json_object *pending) {
    size_t i;
    if (!json_object_is_type(pending, json_type_array) || json_object_array_length(pending) > 32U) return false;
    for (i = 0; i < json_object_array_length(pending); i++) {
        json_object *request = json_object_array_get_idx(pending, i);
        if (!json_object_is_type(request, json_type_object) || !observation_text(request, "request_id", 128, false) ||
            !observation_text(request, "step_id", 128, false) || !observation_text(request, "state", 64, false) ||
            !observation_text(request, "message", 1024, true) || !observation_text(request, "expires_at", 128, true)) return false;
    }
    return true;
}

bool task_observation_response_valid(json_object *data, const char *id) {
    json_object *task, *owner, *waiting, *times, *steps, *pending, *contract; const char *reason; size_t i;
    if (!f_number_is(data, "snapshot_schema_version", 1) ||
        !observation_time(data, "receiver_observed_at") ||
        !(task = f_field(data, "task")) || !json_object_is_type(task, json_type_object) ||
        !observation_text(task, "task_id", 128, false) || strcmp(f_string(task, "task_id"), id) ||
        !observation_text(task, "run_id", 128, true) || !observation_text(task, "step_id", 128, true) ||
        !observation_text(task, "attempt_id", 128, true) || !observation_text(task, "assigned_host", 256, true) ||
        !observation_text(task, "workspace", F_PATH, true) || !observation_text(task, "agent_profile", 128, false) ||
        !observation_text(task, "execution_state", 64, false) ||
        !(owner = f_field(task, "execution_owner")) || !json_object_is_type(owner, json_type_object) ||
        !observation_text(owner, "kind", 64, false) || !observation_text(owner, "state", 64, false) ||
        !observation_text(owner, "recorded_state", 64, true) || !observation_text(owner, "failure", 1024, true) ||
        !(waiting = f_field(task, "waiting")) || !json_object_is_type(waiting, json_type_object) ||
        !observation_text(waiting, "reason", 32, false) || !observation_text(waiting, "detail", 1024, false) ||
        !observation_text(waiting, "next_action", 1024, false) || !(reason = f_string(waiting, "reason")) ||
        (strcmp(reason, "admission") && strcmp(reason, "dependency") && strcmp(reason, "authentication") &&
         strcmp(reason, "approval") && strcmp(reason, "reconciliation") && strcmp(reason, "none")) ||
        !(times = f_field(task, "observation_timestamps")) || !json_object_is_type(times, json_type_object) ||
        !observation_time(times, "accepted_at") || !observation_time(times, "started_at") ||
        !observation_time(times, "resumed_at") || !observation_time(times, "finished_at") ||
        !observation_time(times, "receiver_observed_at") ||
        !json_object_is_type(f_field(task, "effective_configuration"), json_type_object) ||
        strlen(json_object_to_json_string_ext(f_field(task, "effective_configuration"), JSON_C_TO_STRING_PLAIN)) >= 131072 ||
        !(contract = f_field(task, "contract")) || !json_object_is_type(contract, json_type_object) ||
        !observation_text(contract, "availability", 32, false) || !observation_text(contract, "reason", 1024, false) ||
        !bounded_string_array(f_field(contract, "missing_evidence"), 32U) ||
        !(steps = f_field(task, "steps")) || !json_object_is_type(steps, json_type_array) ||
        json_object_array_length(steps) > 512U || !(pending = f_field(task, "pending_requests")) ||
        !pending_requests_valid(pending)) return false;
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (!json_object_is_type(step, json_type_object) || !observation_text(step, "step_id", 128, false) ||
            !observation_text(step, "attempt_id", 128, true) || !observation_text(step, "state", 64, false) ||
            !observation_text(step, "agent_profile", 128, false) || !observation_text(step, "kind", 64, false) ||
            !observation_text(step, "waiting_reason", 32, false)) return false;
    }
    return true;
}

static bool response_binding_valid(json_object *response, const struct remote_options *options, json_object *package) {
    json_object *data = f_field(response, "data");
    const char *received = f_string(data, "task_id"), *digest = f_string(data, "spec_sha256"), *received_key = f_string(data, "submission_key");
    if (options->observe) {
        json_object *task = f_field(data, "task");
        return task_observation_response_valid(data, options->id) && f_string(task, "task_id");
    }
    if (!received || strncmp(received, "task_", 5) || !task_hex(received + 5, 64) || !task_hex(digest, 64)) return false;
    if (options->submit) return received_key && !strcmp(received_key, options->key) && !strcmp(digest, f_string(package, "spec_sha256"));
    return !strcmp(received, options->id);
}

static bool number(const char *text, unsigned minimum, unsigned maximum, unsigned *result) {
    unsigned long value = strtoul(text, NULL, 10);
    if (!*text || strspn(text, "0123456789") != strlen(text) || value < minimum || value > maximum) return false;
    *result = (unsigned)value;
    return true;
}
static json_object *parse_observe_limits(struct remote_options *options) {
    unsigned ignored;
    if (options->cursor && !number(options->cursor, 0, 4294967295U, &options->event_cursor)) return f_error("fleet-task-observe", "invalid_input", "cursor must be 0-4294967295");
    options->event_count = 128;
    if (options->event_limit && !number(options->event_limit, 1, 128, &options->event_count)) return f_error("fleet-task-observe", "invalid_input", "event limit must be 1-128");
    if (options->byte_offset && !number(options->byte_offset, 0, 4294967295U, &ignored)) return f_error("fleet-task-observe", "invalid_input", "byte offset must be 0-4294967295");
    return NULL;
}

static json_object *parse_limits(struct remote_options *options) {
    unsigned attempt;
    if (options->timeout && !number(options->timeout, 1, 300, &options->seconds))
        return f_error(options->cancel ? "fleet-task-cancel" : options->withdraw ? "fleet-task-withdraw" : "fleet-task-result", "invalid_input", "timeout must be 1-300 seconds");
    if (!options->logs && options->observe) {
        return parse_observe_limits(options);
    }
    if (!options->logs) return NULL;
    if (options->attempt && !number(options->attempt, 1, 10000, &attempt))
        return f_error("fleet-task-logs", "invalid_input", "attempt must be 1-10000");
    if (options->offset && !number(options->offset, 0, TASK_FILE_LIMIT, &options->log_offset))
        return f_error("fleet-task-logs", "invalid_input", "offset must be 0-524288");
    if (options->limit && !number(options->limit, 1, 65536, &options->log_limit))
        return f_error("fleet-task-logs", "invalid_input", "limit must be 1-65536");
    return NULL;
}
/* The host is explicit unless automatic placement is requested; then the
 * placement record names the receiver and the positional alias is omitted. */
static json_object *parse_placement(struct remote_options *options) {
    if (!options->submit) return options->host ? NULL : f_error("fleet-task", "invalid_input", "select a registered host alias");
    if (!options->placement) return options->host ? NULL : f_error("fleet-task", "invalid_input", "select a registered host alias or use --placement auto");
    if (!strcmp(options->placement, "auto")) return options->host ? f_error("fleet-task-submit", "invalid_input", "omit the host alias with --placement auto") : NULL;
    if (!f_name(options->placement)) return f_error("fleet-task-submit", "invalid_input", "--placement takes auto or a registered host alias");
    if (options->host && strcmp(options->host, options->placement)) return f_error("fleet-task-submit", "placement_mismatch", "the host alias and --placement name different hosts");
    options->host = options->placement;
    return NULL;
}
/* --separate-from TASK_ID (submit only, repeatable) names tasks whose current
 * host this one must avoid; returns the index of the consumed value. */
static json_object *parse_separation(int argc, char **argv, int *i, struct remote_options *options) {
    if (++*i == argc || !*argv[*i] || options->separations >= PLACEMENT_SEPARATIONS) return f_error("fleet-task-submit", "invalid_input", "--separate-from takes a task ID, at most 8 times");
    options->separate[options->separations++] = argv[*i];
    return NULL;
}
struct remote_field { const char *name; bool allowed; const char **destination; };
static const char **match_field(const struct remote_field *fields, size_t count, const char *name) {
    size_t field;
    for (field = 0; field < count; field++) if (fields[field].allowed && !strcmp(name, fields[field].name)) return fields[field].destination;
    return NULL;
}
/* Returns the index of the first option; a leading alias is the host. */
static int parse_operation(int argc, char **argv, struct remote_options *options) {
    const char *operation = argv[0];
    *options = (struct remote_options){0};
    if (argc >= 2 && argv[1][0] != '-') { options->host = argv[1]; }
    options->submit = !strcmp(operation, "submit");
    options->start = !strcmp(operation, "start");
    options->cancel = !strcmp(operation, "cancel");
    options->logs = !strcmp(operation, "logs");
    options->resume = !strcmp(operation, "resume");
    options->decide = !strcmp(operation, "decide");
    options->result_read = !strcmp(operation, "result");
    options->observe = !strcmp(operation, "observe");
    options->withdraw = !strcmp(operation, "withdraw");
    options->seconds = (options->result_read || options->cancel || options->withdraw) ? 30 : 5;
    options->log_limit = 4096;
    return options->host ? 2 : 1;
}
static json_object *parse_options(int argc, char **argv, struct remote_options *options) {
    int i, first = parse_operation(argc, argv, options);
    if (options->host && !f_name(options->host)) return f_error("fleet-task", "invalid_input", "select a registered host alias");
    const struct remote_field fields[] = {
        {"--output", options->result_read, &options->output},
        {"--timeout", options->result_read || options->cancel || options->withdraw, &options->timeout},
        {"--input", options->submit, &options->input},
        {"--key", options->submit, &options->key},
        {"--placement", options->submit, &options->placement},
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
        {"--limit", options->logs, &options->limit},
        {"--cursor", options->observe, &options->cursor},
        {"--event-limit", options->observe, &options->event_limit}
        ,{"--stream-id", options->observe, &options->stream_id}
        ,{"--byte-offset", options->observe, &options->byte_offset}
    };
    for (i = first; i < argc; i++) {
        const char **destination;
        if (options->submit && !strcmp(argv[i], "--separate-from")) {
            json_object *invalid = parse_separation(argc, argv, &i, options);
            if (invalid) return invalid;
            continue;
        }
        destination = match_field(fields, sizeof(fields) / sizeof(fields[0]), argv[i]);
        if (!destination) return f_error("fleet-task", "invalid_input", "use hydra fleet task help");
        if (*destination || ++i == argc || !*argv[i]) return f_error("fleet-task", "invalid_input", "each option requires one value");
        *destination = argv[i];
    }
    if ((options->submit ? !options->input || !options->key : !options->id) || ((options->start || options->resume || options->decide) && !options->trust) || (options->decide && (!options->request_id || !options->decision))) return f_error("fleet-task", "invalid_input", "required task options are missing");
    { json_object *invalid = parse_placement(options); if (invalid) return invalid; }
    return parse_limits(options);
}
static void request_log_options(json_object *request, const struct remote_options *options) {
    if (options->source) f_string_add(request, "source", options->source);
    if (options->step) f_string_add(request, "step", options->step);
    if (options->attempt) json_object_object_add(request, "attempt", json_object_new_int64(strtol(options->attempt, NULL, 10)));
    f_string_add(request, "stream", options->stream ? options->stream : "stdout");
    json_object_object_add(request, "offset", json_object_new_int64(options->log_offset)); json_object_object_add(request, "limit", json_object_new_int64(options->log_limit));
}

static void request_observe_options(json_object *request, const struct remote_options *options) {
    json_object_object_add(request, "cursor", json_object_new_int64(options->event_cursor)); json_object_object_add(request, "event_limit", json_object_new_int64(options->event_count));
    if (options->stream_id) f_string_add(request, "stream_id", options->stream_id);
    if (options->byte_offset) json_object_object_add(request, "byte_offset", json_object_new_int64(strtol(options->byte_offset, NULL, 10)));
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
    if (options->logs) request_log_options(request, options);
    if (options->observe) request_observe_options(request, options);
    if (options->generation) json_object_object_add(request, "assignment_generation", json_object_new_int64(options->generation));
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
/* One negotiated transport to a loaded alias; package is borrowed. Lost or
 * invalid responses to mutations become outcome_unknown, never a retry. */
static json_object *transact(struct remote_options *options, const struct f_remote *remote, const char *operation, json_object *package) {
    char capability[32]; json_object *request, *response = f_observe(remote, "handshake", options->seconds);
    if (!json_object_get_boolean(f_field(response, "ok"))) return response;
    snprintf(capability, sizeof(capability), "task-%s", options->submit ? "accept" : operation);
    if (!supported(response, capability) || (options->submit && options->trust && !supported(response, "task-start"))) {
        json_object_put(response); return f_error("fleet-task", "capability_unavailable", "the receiver lacks the requested task protocol 1 capability");
    }
    /* Receivers advertising task-withdraw record and enforce assignment generations. */
    if (options->generation && !supported(response, "task-withdraw")) options->generation = 0;
    json_object_put(response);
    request = make_request(options, operation, package);
    response = f_request(remote, request, options->seconds); json_object_put(request);
    if (json_object_get_boolean(f_field(response, "ok")) && !response_binding_valid(response, options, package)) {
        json_object_put(response); response = f_error("fleet-task", "invalid_response", "the receiver returned a task handle with inconsistent bindings");
    }
    if (options->result_read && json_object_get_boolean(f_field(response, "ok"))) response = result_output(options, response);
    if ((options->submit || options->start || options->cancel || options->resume || options->decide || options->withdraw) && uncertain(response)) {
        json_object *wrapped = f_error("fleet-task", "outcome_unknown", options->submit ? "the acceptance response was lost or invalid; retry this same package and key to reconcile, never invent a new key" : "the mutation response was lost or invalid; inspect this task's status; execution is never replayed");
        json_object_object_add(f_field(wrapped, "error"), "cause", json_object_get(f_field(response, "error")));
        json_object_put(response); response = wrapped;
    }
    return response;
}
json_object *task_remote_submit(const char *host, json_object *package, const char *key, const char *trust, int generation, unsigned seconds) {
    struct remote_options options = {0}; struct f_remote remote; json_object *response;
    options.submit = true; options.host = host; options.key = key; options.trust = trust; options.seconds = seconds;
    options.generation = generation > 0 ? (unsigned)generation : 0;
    if (!f_name(host) || f_remote_load(host, &remote)) return f_error("fleet-task", "invalid_alias", "register the remote before submitting a task");
    response = transact(&options, &remote, "submit", package);
    f_string_add(response, "host", host);
    return response;
}
static json_object *submit_command(struct remote_options *options) {
    json_object *package = f_read_json(options->input, TASK_PACKAGE_LIMIT), *checked = task_inspect(package), *response, *separate = NULL;
    const char *bound; unsigned i;
    if (!json_object_get_boolean(f_field(checked, "ok"))) { json_object_put(package); return checked; }
    bound = f_string(f_field(checked, "data"), "host");
    options->seconds = (unsigned)json_object_get_int(f_field(f_field(f_field(checked, "data"), "limits"), "transport_seconds"));
    if (options->placement) {
        separate = json_object_new_array();
        for (i = 0; i < options->separations; i++) json_object_array_add(separate, json_object_new_string(options->separate[i]));
        if (strcmp(options->placement, "auto") && strcmp(options->placement, bound)) response = f_error("fleet-task-submit", "placement_mismatch", "the pinned alias differs from the immutable task destination");
        else response = placement_submit(package, options->key, options->trust, options->placement, separate, options->seconds);
    } else if (strcmp(options->host, bound)) response = f_error("fleet-task-submit", "placement_mismatch", "the selected alias differs from the immutable task destination");
    else response = task_remote_submit(options->host, package, options->key, options->trust, 0, options->seconds);
    json_object_put(separate); json_object_put(checked); json_object_put(package);
    return response;
}
json_object *task_remote_cli(int argc, char **argv) {
    struct remote_options parsed, *options = &parsed;
    struct f_remote remote; char assigned[128];
    json_object *response = parse_options(argc, argv, options);
    if (response) return response;
    if (options->submit) return submit_command(options);
    if (f_remote_load(options->host, &remote)) return f_error("fleet-task", "invalid_alias", "register the remote before submitting a task");
    if (options->start || options->resume) options->generation = (unsigned)placement_generation(options->id, assigned);
    response = transact(options, &remote, argv[0], NULL);
    f_string_add(response, "host", options->host);
    return response;
}
