#define _XOPEN_SOURCE 700
#include "task.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool task_id(const char *id) { return id && !strncmp(id, "task_", 5) && task_hex(id + 5, 64); }
static json_object *receipt(const char *directory, const char *id, const char *digest) {
    json_object *accepted = task_read_record(directory, "acceptance.json"), *state = task_read_record(directory, "state.json"), *result = NULL;
    const char *stored_id = f_string(accepted, "task_id"), *stored_digest = f_string(accepted, "spec_sha256");
    const char *current = f_string(state, "state");
    if (!f_number_is(accepted, "schema_version", 1) || !stored_id || strcmp(stored_id, id) || !task_hex(stored_digest, 64) ||
        !f_string(accepted, "project") || !f_string(accepted, "project_id") || !f_string(accepted, "submission_key") ||
        !json_object_is_type(f_field(accepted, "accepted_at"), json_type_int) || !task_runtime_valid(state)) goto done;
    if (digest && strcmp(stored_digest, digest)) {
        result = f_error("fleet-task-submit", "submission_conflict", "this receiving-host submission key already binds a different task specification");
        goto done;
    }
    if ((!strcmp(current, "starting") || !strcmp(current, "running")) && !task_owner_active(directory)) {
        f_string_add(state, "recorded_state", current); f_string_add(state, "state", "outcome_unknown");
        f_string_add(state, "failure", "owner_unavailable");
    }
    if (f_string(state, "result_state") && !strcmp(f_string(state, "result_state"), "sealing") && !task_owner_active(directory)) {
        f_string_add(state, "result_state", "unknown"); f_string_add(state, "result_error", "owner_unavailable");
    }
    task_cancel_view(directory, stored_digest, state);
    json_object_object_add(accepted, "runtime", json_object_get(state));
    result = f_success("fleet-task", json_object_get(accepted));
done:
    json_object_put(state); json_object_put(accepted);
    return result ? result : f_error("fleet-task", "recovery_required", "the accepted task record is incomplete or invalid; execution must not be replayed");
}
json_object *task_status(const char *id) {
    char root[F_PATH], directory[F_PATH]; struct stat st;
    if (!task_id(id)) return f_error("fleet-task-status", "invalid_input", "use the task ID returned by submission");
    if (task_store_root(root) || f_path(directory, sizeof(directory), root, id)) return f_error("fleet-task-status", "io_failed", "cannot open private task storage");
    if (lstat(directory, &st)) return f_error("fleet-task-status", errno == ENOENT ? "task_not_found" : "io_failed", "task storage is unavailable for this ID");
    return receipt(directory, id, NULL);
}
/* Placement is an existing host-local mapping, never a client-created directory. */
static bool project_identity(const char *project, const char *identity) {
    char *argv[] = {"git", "-C", (char *)project, "rev-parse", "--git-common-dir", NULL};
    struct f_capture cap = {0}; char common[F_PATH], path[F_PATH], *stored = NULL; bool matches = false;
    if (f_run(argv, NULL, 0, 5, &cap) || cap.status) goto done;
    cap.out[strcspn(cap.out, "\r\n")] = '\0';
    if (cap.out[0] == '/') { if (f_copy(common, sizeof(common), cap.out)) goto done; }
    else if (f_path(common, sizeof(common), project, cap.out)) goto done;
    if (f_path(path, sizeof(path), common, "hydra/project-id") || !(stored = f_read(path, 128))) goto done;
    stored[strcspn(stored, "\r\n")] = '\0'; matches = !strcmp(stored, identity);
done:
    free(stored); f_capture_free(&cap); return matches;
}
static int mapped_project(const char *requested, char canonical[F_PATH], char identity[128]) {
    json_object *handshake, *projects; size_t i; int status = -1;
    if (!requested || requested[0] != '/' || !realpath(requested, canonical)) return -1;
    handshake = f_handshake(); projects = f_field(f_field(handshake, "data"), "projects");
    for (i = 0; i < json_object_array_length(projects); i++) {
        json_object *project = json_object_array_get_idx(projects, i); char path[F_PATH];
        const char *registered = f_string(project, "path"), *id = f_string(project, "project_id");
        if (registered && id && realpath(registered, path) && !strcmp(path, canonical) && !f_copy(identity, 128, id)) { status = 0; break; }
    }
    json_object_put(handshake);
    return status == 0 && project_identity(canonical, identity) ? 0 : -1;
}
static bool dependencies(json_object *spec) {
    json_object *caps = f_field(spec, "capabilities"); size_t i;
    const char *supported[] = {"exec", "workflow", "git", "tmux", NULL};
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *name = task_text(json_object_array_get_idx(caps, i)); size_t j;
        for (j = 0; supported[j] && strcmp(name, supported[j]); j++) {}
        if (!supported[j]) return false;
    }
    return true;
}
static bool executable(const char *program, const char *option) {
    struct f_capture cap = {0}; char *argv[] = {(char *)program, (char *)option, NULL};
    bool ok = !f_run(argv, NULL, 0, 5, &cap) && !cap.status;
    f_capture_free(&cap); return ok;
}
json_object *task_accept(json_object *package, const char *key) {
    char root[F_PATH], stage[F_PATH] = "", destination[F_PATH], canonical[F_PATH], project_id[128], hash[65], id[70];
    json_object *inspected = NULL, *spec, *binding = NULL, *accepted = NULL, *state = NULL, *result = NULL;
    const char *digest; bool staged = false; struct stat st;
    if (!f_name(key) || strlen(key) > 128) return f_error("fleet-task-submit", "invalid_input", "submission keys must be 1-128 letters, digits, dots, underscores, or hyphens, starting with a letter or digit");
    inspected = task_inspect(package);
    if (!json_object_get_boolean(f_field(inspected, "ok"))) return inspected;
    spec = f_field(inspected, "data"); digest = f_string(package, "spec_sha256");
    if (task_store_root(root) || snprintf(stage, sizeof(stage), "%s/.accept.XXXXXX", root) >= (int)sizeof(stage) || !mkdtemp(stage)) goto io;
    staged = true; binding = json_object_new_object();
    f_string_add(binding, "submission_key", key);
    if (task_json_hash(binding, stage, hash)) goto io;
    snprintf(id, sizeof(id), "task_%s", hash);
    if (f_path(destination, sizeof(destination), root, id)) goto io;
    if (!lstat(destination, &st)) {
        if (task_sync_dir(root)) goto unknown;
        result = receipt(destination, id, digest); goto done;
    }
    if (errno != ENOENT) goto io;
    if (mapped_project(f_string(spec, "project"), canonical, project_id)) {
        result = f_error("fleet-task-submit", "unmapped_project", "initialize and explicitly select an existing project on the receiving host"); goto done;
    }
    if (!dependencies(spec)) {
        result = f_error("fleet-task-submit", "capability_unavailable", "the task requires a capability this receiver does not implement"); goto done;
    }
    if (!executable("git", "--version") || !executable("tmux", "-V") || !executable(f_hydra, "version")) {
        result = f_error("fleet-task-submit", "missing_dependency", "the receiving host requires working Git, tmux, and the Hydra shell CLI"); goto done;
    }
    if (f_stopped) { result = f_error("fleet-task-submit", "cancelled", "submission was interrupted before acceptance"); goto done; }
    accepted = json_object_new_object(); json_object_object_add(accepted, "schema_version", json_object_new_int(1));
    f_string_add(accepted, "task_id", id); f_string_add(accepted, "project", canonical); f_string_add(accepted, "project_id", project_id);
    f_string_add(accepted, "submission_key", key); f_string_add(accepted, "spec_sha256", digest);
    json_object_object_add(accepted, "accepted_at", json_object_new_int64((int64_t)time(NULL)));
    state = json_object_new_object(); json_object_object_add(state, "schema_version", json_object_new_int(1));
    f_string_add(state, "state", "accepted"); f_string_add(state, "launch_intent", "pending");
    if (task_write_json(stage, "package.json", package, false) || task_write_json(stage, "acceptance.json", accepted, false) ||
        task_write_json(stage, "state.json", state, false)) goto io;
    /* A published directory is nonempty. rename cannot replace another winner. */
    if (rename(stage, destination)) {
        if (errno != EEXIST && errno != ENOTEMPTY) goto io;
    } else staged = false;
    if (task_sync_dir(root)) goto unknown;
    result = receipt(destination, id, digest); goto done;
unknown:
    result = f_error("fleet-task-submit", "outcome_unknown", "acceptance may be present but directory durability could not be confirmed; reconcile this key"); goto done;
io:
    result = f_error("fleet-task-submit", "io_failed", "cannot publish acceptance in private receiving-host storage");
done:
    if (staged) f_remove_tree(stage);
    json_object_put(binding); json_object_put(accepted); json_object_put(state); json_object_put(inspected); return result;
}
json_object *task_serve(json_object *request) {
    const char *const keys[] = {"protocol", "action", "operation", "package", "submission_key", "task_id", "trust_spec", "stream", "offset", "limit", "source", "step", "attempt", NULL};
    const char *operation = f_string(request, "operation");
    if (!task_keys(request, keys) || !operation) return f_error("fleet-task", "invalid_input", "invalid task request");
    if (strcmp(operation, "logs") && (f_field(request, "stream") || f_field(request, "offset") || f_field(request, "limit") || f_field(request, "source") || f_field(request, "step") || f_field(request, "attempt"))) return f_error("fleet-task", "invalid_input", "log options require the logs operation");
    if (!strcmp(operation, "logs") && !f_field(request, "package") && !f_field(request, "submission_key") && !f_field(request, "trust_spec")) {
        return task_logs(f_string(request, "task_id"), request);
    }
    if (!strcmp(operation, "result") && !f_field(request, "package") && !f_field(request, "submission_key") && !f_field(request, "trust_spec")) return task_result(f_string(request, "task_id"));
    if (!strcmp(operation, "cancel") && !f_field(request, "package") && !f_field(request, "submission_key") && !f_field(request, "trust_spec")) return task_cancel(f_string(request, "task_id"));
    if (!strcmp(operation, "submit") && !f_field(request, "task_id")) {
        const char *trust = f_string(request, "trust_spec"), *digest = f_string(f_field(request, "package"), "spec_sha256"); json_object *accepted;
        if (f_field(request, "trust_spec") && (!task_hex(trust, 64) || !digest || strcmp(trust, digest))) return f_error("fleet-task-submit", "trust_required", "--trust-spec must match the prepared specification digest");
        accepted = task_accept(f_field(request, "package"), f_string(request, "submission_key"));
        if (trust && json_object_get_boolean(f_field(accepted, "ok"))) {
            json_object *started = task_start(f_string(f_field(accepted, "data"), "task_id"), trust);
            json_object_put(accepted); return started;
        }
        return accepted;
    }
    if (!strcmp(operation, "status") && !f_field(request, "package") && !f_field(request, "submission_key") && !f_field(request, "trust_spec")) return task_status(f_string(request, "task_id"));
    if (!strcmp(operation, "start") && !f_field(request, "package") && !f_field(request, "submission_key")) return task_start(f_string(request, "task_id"), f_string(request, "trust_spec"));
    return f_error("fleet-task", "invalid_input", "unknown task operation or conflicting fields");
}
