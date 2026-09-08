#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static json_object *read_json(const char *directory, const char *name, size_t limit) {
    char path[F_PATH];
    return f_path(path, sizeof(path), directory, name) ? NULL : f_read_json(path, limit);
}
static json_object *read_dispatch(const char *directory) {
    char path[F_PATH], hash[65], *expected = NULL; json_object *record = NULL;
    if (f_path(path, sizeof(path), directory, "dispatch-sha256") || !(expected = f_read(path, 64)) ||
        f_path(path, sizeof(path), directory, "dispatch.json") || f_hash(path, hash) || strcmp(hash, expected)) goto done;
    record = f_read_json(path, TASK_PACKAGE_LIMIT + WD_LIMIT);
done:
    free(expected); return record;
}
static int write_dispatch(const char *directory, json_object *record) {
    char path[F_PATH], hash[65];
    if (task_write_json(directory, "dispatch.json", record, false) || f_path(path, sizeof(path), directory, "dispatch.json") ||
        f_hash(path, hash) || f_path(path, sizeof(path), directory, "dispatch-sha256") || f_write(path, hash, 64, false)) return -1;
    return task_sync_dir(directory);
}
static int dispatch_key(const char *run, const char *step, const char *attempt, char digest[65]) {
    char path[F_PATH], *id; json_object *identity; int status;
    if (f_path(path, sizeof(path), run, "run-id") || !(id = f_read(path, 128))) return -1;
    identity = json_object_new_object(); f_string_add(identity, "run", id); free(id);
    f_string_add(identity, "step", step);
    f_string_add(identity, "attempt", strrchr(attempt, '/') ? strrchr(attempt, '/') + 1 : attempt);
    status = plan_digest(identity, digest); json_object_put(identity); return status;
}
static json_object *dispatch_spec(json_object *binding, json_object *source) {
    json_object *spec = plan_canonical(f_field(binding, "spec"));
    if (source) f_string_add(f_field(spec, "source"), "commit", f_string(source, "commit"));
    return spec;
}
static json_object *dispatch(const char *run, const char *step, const char *attempt, json_object *bindings, json_object *source) {
    char root[F_PATH], stage[F_PATH], inputs[F_PATH], digest[65];
    json_object *existing = NULL, *prepared = NULL, *out = NULL, *spec = NULL;
    json_object *binding = f_field(f_field(bindings, "steps"), step); struct stat st;
    if (f_path(root, sizeof(root), attempt, "remote")) return NULL;
    if (!lstat(root, &st)) return read_dispatch(root);
    if (errno != ENOENT || snprintf(stage, sizeof(stage), "%s/.remote.XXXXXX", attempt) >= (int)sizeof(stage) || !mkdtemp(stage)) return NULL;
    if (f_path(inputs, sizeof(inputs), attempt, "inputs")) goto done;
    spec = dispatch_spec(binding, source);
    prepared = task_prepare(f_string(bindings, "source"), inputs, spec);
    if (!json_object_get_boolean(f_field(prepared, "ok"))) goto done;
    if (dispatch_key(run, step, attempt, digest)) goto done;
    existing = json_object_new_object(); f_string_add(existing, "submission_key", digest);
    json_object_object_add(existing, "package", json_object_get(f_field(prepared, "data")));
    json_object_object_add(existing, "binding", json_object_get(binding));
    if (source) json_object_object_add(existing, "source", json_object_get(source));
    if (write_dispatch(stage, existing) || rename(stage, root) || task_sync_dir(attempt)) goto done;
    stage[0] = '\0'; out = json_object_get(existing);
done:
    if (stage[0]) f_remove_tree(stage);
    json_object_put(spec); json_object_put(existing); json_object_put(prepared); return out;
}
static bool dispatch_valid(json_object *record, json_object *binding, json_object *source) {
    json_object *package = f_field(record, "package"), *checked = task_inspect(package), *spec;
    bool valid = json_object_get_boolean(f_field(checked, "ok")) && json_object_equal(binding, f_field(record, "binding")) &&
        task_hex(f_string(record, "submission_key"), 64) && json_object_equal(source, f_field(record, "source"));
    if (valid) {
        spec = plan_canonical(f_field(package, "spec"));
        json_object_object_del(f_field(spec, "source"), "bundle_sha256");
        json_object_object_add(spec, "inputs", json_object_get(f_field(f_field(binding, "spec"), "inputs")));
        json_object *expected = dispatch_spec(binding, source);
        valid = json_object_equal(spec, expected); json_object_put(expected); json_object_put(spec);
    }
    json_object_put(checked); return valid;
}
static json_object *request(json_object *record, const char *operation, const char *id, bool start) {
    json_object *req = json_object_new_object(), *package = f_field(record, "package"), *response;
    json_object_object_add(req, "protocol", json_object_new_int(F_PROTOCOL));
    f_string_add(req, "action", "task"); f_string_add(req, "operation", operation);
    if (id) f_string_add(req, "task_id", id);
    else {
        json_object_object_add(req, "package", json_object_get(package));
        f_string_add(req, "submission_key", f_string(record, "submission_key"));
        if (start) f_string_add(req, "trust_spec", f_string(package, "spec_sha256"));
    }
    unsigned seconds = (unsigned)json_object_get_int(f_field(f_field(f_field(package, "spec"), "limits"), "transport_seconds"));
    response = wt_request(f_field(f_field(record, "binding"), "destination"), req, seconds);
    json_object_put(req); return response;
}
static bool receipt_matches(json_object *receipt, json_object *record, const char *id) {
    const char *observed = f_string(receipt, "task_id"), *digest = f_string(receipt, "spec_sha256");
    const char *key = f_string(receipt, "submission_key");
    return observed && !strncmp(observed, "task_", 5) && task_hex(observed + 5, 64) &&
        (!id || !strcmp(observed, id)) && digest && !strcmp(digest, f_string(f_field(record, "package"), "spec_sha256")) &&
        key && !strcmp(key, f_string(record, "submission_key"));
}
bool wt_parent_receipt(const char *run, const char *step, const char *attempt, json_object *binding, json_object *receipt) {
    char directory[F_PATH], expected[65]; json_object *record = NULL; bool valid = false;
    if (f_path(directory, sizeof(directory), attempt, "remote") || !(record = read_dispatch(directory))) goto done;
    json_object *source = f_field(record, "source"); const char *producer = f_string(binding, "source_step");
    if ((producer != NULL) != (source != NULL)) goto done;
    if (producer && (!f_string(source, "step") || strcmp(producer, f_string(source, "step")))) goto done;
    valid = dispatch_valid(record, binding, source) && !dispatch_key(run, step, attempt, expected) &&
        !strcmp(expected, f_string(record, "submission_key")) && receipt_matches(receipt, record, NULL);
done:
    json_object_put(record); return valid;
}
static int materialize(const char *attempt, json_object *result) {
    char root[F_PATH], path[F_PATH], hash[65]; json_object *files = f_field(result, "artifacts");
    if (f_path(root, sizeof(root), attempt, "outputs")) return -1;
    for (size_t i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); const char *relative = f_string(file, "path");
        if (task_collection_file(root, relative, f_string(file, "hex")) || f_path(path, sizeof(path), root, relative) ||
            f_hash(path, hash) || strcmp(hash, f_string(file, "sha256"))) return -1;
    }
    return 0;
}
static bool collection_transport_failure(json_object *response) {
    const char *code = f_string(f_field(response, "error"), "code");
    const char *transient[] = {"outcome_unknown", "timeout", "offline", "cancelled", "invalid_response", "remote_failed", "output_limit", "transport_failed", "authentication_failed", "host_key_failed", NULL};
    for (size_t i = 0; code && transient[i]; i++) if (!strcmp(code, transient[i])) return true;
    return false;
}
static json_object *collect(const char *attempt, const char *remote, json_object *record, const char *id, const char *source) {
    json_object *response = request(record, "result", id, false), *envelope = f_field(f_field(response, "data"), "collection");
    json_object *checked = task_result_verify(envelope), *collected = NULL; bool valid = false;
    if (!json_object_get_boolean(f_field(response, "ok"))) {
        bool transient = collection_transport_failure(response);
        json_object_put(response); json_object_put(checked); json_object_put(collected);
        return f_error("workflow task", transient ? "waiting_remote" : "result_unavailable",
            transient ? "result collection transport was interrupted; reconcile the same task identity" :
                        "task result or collection is unavailable or failed binding verification");
    }
    if (!json_object_get_boolean(f_field(checked, "ok")) ||
        !receipt_matches(f_field(f_field(envelope, "result"), "receipt"), record, id)) goto done;
    collected = task_collect(source, envelope);
    if (!json_object_get_boolean(f_field(collected, "ok")) || task_write_json(remote, "result.json", envelope, true) ||
        task_write_json(remote, "collection.json", collected, true) || materialize(attempt, f_field(envelope, "result"))) goto done;
    valid = true;
done:
    json_object_put(response); json_object_put(checked); json_object_put(collected);
    return valid ? f_success("workflow task", json_object_new_object()) :
        f_error("workflow task", "result_unavailable", "task result or collection is unavailable or failed binding verification");
}
enum observation { OBSERVE_AGAIN, OBSERVE_COLLECT, OBSERVE_FAILED, OBSERVE_PENDING, OBSERVE_INVALID };
struct execution {
    const char *run, *attempt, *source;
    char remote[F_PATH], id[70];
    json_object *record;
};
static enum observation classify(json_object *runtime, bool cancelling) {
    const char *state = f_string(runtime, "state");
    if (!state) return OBSERVE_INVALID;
    if (!strcmp(state, "succeeded")) {
        const char *sealed = f_string(runtime, "result_state");
        if (!sealed || !strcmp(sealed, "sealing")) return OBSERVE_AGAIN;
        return !strcmp(sealed, "ready") ? OBSERVE_COLLECT : OBSERVE_PENDING;
    }
    if (!strcmp(state, "failed") || !strcmp(state, "expired") || !strcmp(state, "cancelled")) return OBSERVE_FAILED;
    if (cancelling || !strcmp(state, "outcome_unknown") || !strcmp(state, "waiting_approval")) return OBSERVE_PENDING;
    return OBSERVE_AGAIN;
}
static enum observation record_observation(struct execution *execution, json_object *data, bool cancelling) {
    if (!receipt_matches(data, execution->record, *execution->id ? execution->id : NULL)) return OBSERVE_INVALID;
    if (!*execution->id) {
        if (f_copy(execution->id, sizeof(execution->id), f_string(data, "task_id")) ||
            task_write_json(execution->remote, "receipt.json", data, false)) return OBSERVE_INVALID;
        if (cancelling) return OBSERVE_AGAIN;
    }
    if (task_write_json(execution->remote, "observation.json", data, true)) return OBSERVE_INVALID;
    return classify(f_field(data, "runtime"), cancelling);
}
static enum observation observe(struct execution *execution) {
    char path[F_PATH]; enum observation next = OBSERVE_PENDING;
    if (f_path(path, sizeof(path), execution->run, "cancel-requested")) return OBSERVE_INVALID;
    bool cancelling = f_stopped || access(path, F_OK) == 0;
    if (cancelling) f_stopped = 0; /* Permit one bounded cancellation request. */
    const char *operation = cancelling ? "cancel" : "status";
    if (!*execution->id) operation = "submit";
    json_object *response = request(execution->record, operation, *execution->id ? execution->id : NULL, !cancelling);
    if (json_object_get_boolean(f_field(response, "ok")))
        next = record_observation(execution, f_field(response, "data"), cancelling);
    json_object_put(response); return next;
}
static json_object *drive(struct execution *execution) {
    for (;;) {
        switch (observe(execution)) {
            case OBSERVE_COLLECT:
                return collect(execution->attempt, execution->remote, execution->record, execution->id, execution->source);
            case OBSERVE_FAILED:
                return f_error("workflow task", "task_failed", "the recorded remote execution is terminal without success");
            case OBSERVE_PENDING:
                return f_error("workflow task", "waiting_remote", "reconcile this same workflow run and task; do not replace the dispatch");
            case OBSERVE_INVALID:
                return f_error("workflow task", "binding_invalid", "task observation differs from its recorded identity");
            case OBSERVE_AGAIN: break;
        }
        struct timespec delay = {1, 0}; nanosleep(&delay, NULL);
    }
}
static bool attempt_valid(const char *run, const char *step, const char *attempt) {
    char expected[F_PATH]; int number = plan_task_attempt(run);
    return number > 0 && snprintf(expected, sizeof(expected), "%s/steps/%s/attempt-%d", run, step, number) < (int)sizeof(expected) && !strcmp(expected, attempt);
}
json_object *wt_execute(const char *run, const char *step, const char *attempt) {
    json_object *bindings = wt_bindings(run), *response = NULL, *receipt = NULL, *source = NULL;
    struct execution execution = {.run = run, .attempt = attempt}; char path[F_PATH], expected[65]; int lock = -1;
    if (!bindings || !f_field(f_field(bindings, "steps"), step) || !attempt_valid(run, step, attempt) || f_path(path, sizeof(path), attempt, "remote.lock")) goto done;
    lock = open(path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) goto done;
    json_object *binding = f_field(f_field(bindings, "steps"), step);
    if (f_field(binding, "source_step") && !(source = wt_source_binding(run, bindings, binding))) goto done;
    execution.record = dispatch(run, step, attempt, bindings, source); execution.source = f_string(bindings, "source");
    if (!execution.record || !dispatch_valid(execution.record, binding, source) ||
        dispatch_key(run, step, attempt, expected) || strcmp(expected, f_string(execution.record, "submission_key")) ||
        f_path(execution.remote, sizeof(execution.remote), attempt, "remote")) goto done;
    receipt = read_json(execution.remote, "receipt.json", 16384);
    if (receipt && (!receipt_matches(receipt, execution.record, NULL) ||
        f_copy(execution.id, sizeof(execution.id), f_string(receipt, "task_id")))) goto done;
    response = drive(&execution);
done:
    if (lock >= 0) close(lock);
    json_object_put(source); json_object_put(bindings); json_object_put(execution.record); json_object_put(receipt);
    return response ? response : f_error("workflow task", "binding_invalid", "recorded task identity, destination, or artifacts changed");
}
