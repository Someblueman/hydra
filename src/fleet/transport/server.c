#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/server.h"
#include "fleet/transport/bundle.h"
#include "fleet/fleet.h"
#include "fleet/task/task.h"
#include "fleet/auth/agent_auth.h"
#include "fleet/enrollment/receiver.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *capabilities[] = {"list", "overview", "doctor", "admission", "init", "spawn", "signal", "cancel", "workflow", "attach", "export", "import", "task-accept", "task-status", "task-observe", "task-start", "task-resume", "task-requests", "task-decide", "task-cancel", "task-logs", "task-result", "agent-headless", "workflow-data", "workflow-approval-wait", "agent-auth", "execution-headless", NULL};
bool f_terminal_available(void) {
    struct f_capture cap = {0}; char *argv[] = {"tmux", "-V", NULL};
    unsigned major = 0, minor = 0;
    bool ok = !f_run(argv, NULL, 0, 5, &cap) && !cap.status &&
        sscanf(cap.out, "tmux %u.%u", &major, &minor) == 2 && major >= 3;
    f_capture_free(&cap); return ok;
}
static json_object *host_capabilities(void) {
    json_object *caps = json_object_new_array();
    for (size_t i = 0; capabilities[i]; i++) json_object_array_add(caps, json_object_new_string(capabilities[i]));
    if (f_terminal_available()) json_object_array_add(caps, json_object_new_string("tmux"));
    return caps;
}
json_object *f_handshake(void) {
    json_object *data = json_object_new_object(), *projects = json_object_new_array(), *native = json_object_new_object();
    char root[F_PATH]; DIR *dir; struct dirent *entry;
    f_string_add(data, "hydra_version", F_VERSION);
    json_object_object_add(data, "fleet_protocol", json_object_new_int(F_PROTOCOL));
    json_object_object_add(data, "task_protocol", json_object_new_int(1));
    json_object_object_add(data, "state_schema", json_object_new_int(2));
    json_object_object_add(data, "event_schema", json_object_new_int(1));
    json_object_object_add(data, "json_schema", json_object_new_int(1));
    {
        json_object *protocols = json_object_new_object(), *signals = json_object_new_array();
        json_object_object_add(protocols, "core", json_object_new_int(1)); json_object_object_add(protocols, "tui", json_object_new_int(2));
        json_object_object_add(data, "native_protocols", protocols);
        json_object_array_add(signals, json_object_new_string("INT")); json_object_object_add(data, "signals", signals);
    }
    json_object_object_add(data, "capabilities", host_capabilities());
    if (!f_path(root, sizeof(root), f_home, "state/v2/projects") && (dir = opendir(root))) {
        while ((entry = readdir(dir))) {
            char path[F_PATH], project[F_PATH], *value; json_object *item;
            if (strncmp(entry->d_name, "project_", 8) || !f_name(entry->d_name) || f_path(project, sizeof(project), root, entry->d_name) || f_path(path, sizeof(path), project, "repo-root")) continue;
            value = f_read(path, F_PATH - 1); if (!value) continue;
            value[strcspn(value, "\r\n")] = '\0'; item = json_object_new_object();
            f_string_add(item, "project_id", entry->d_name); f_string_add(item, "path", value); free(value);
            json_object_array_add(projects, item);
        }
        closedir(dir);
    }
    json_object_object_add(data, "projects", projects);
    {
        const char *bin = getenv("HYDRA_BIN_DIR"), *kinds[] = {"core", "tui"}; size_t k;
        for (k = 0; k < 2; k++) {
            char path[F_PATH]; bool available = false;
            if (bin && snprintf(path, sizeof(path), "%s/../build/hydra-%s", bin, kinds[k]) < (int)sizeof(path)) available = access(path, X_OK) == 0;
            if (!available && bin && snprintf(path, sizeof(path), "%s/../libexec/hydra/hydra-%s", bin, kinds[k]) < (int)sizeof(path)) available = access(path, X_OK) == 0;
            json_object_object_add(native, kinds[k], json_object_new_boolean(available));
        }
    }
    json_object_object_add(native, "fleet", json_object_new_boolean(true));
    json_object_object_add(data, "native", native);
    return f_success("fleet-handshake", data);
}
json_object *f_run_hydra(char **argv, unsigned seconds) {
    struct f_capture cap = {0}; json_object *result, *data;
    if (f_run(argv, NULL, 0, seconds, &cap)) { f_capture_free(&cap); return f_error("fleet", "exec_failed", "cannot execute local Hydra"); }
    result = f_parse(cap.out);
    if (!result || !json_object_is_type(f_field(result, "ok"), json_type_boolean)) {
        json_object_put(result);
        data = json_object_new_object(); f_string_add(data, "stdout", cap.out); f_string_add(data, "stderr", cap.err);
        json_object_object_add(data, "exit_status", json_object_new_int(cap.status));
        result = cap.status ? f_error("fleet", cap.timeout ? "outcome_unknown" : "command_failed", cap.err[0] ? cap.err : "remote command failed; inspect output") : f_success("fleet", NULL);
        json_object_object_add(result, "data", data);
    } else if (cap.status && json_object_get_boolean(f_field(result, "ok"))) {
        json_object_put(result); result = f_error("fleet", "outcome_unknown", "command did not exit successfully");
    }
    f_capture_free(&cap); return result;
}
static json_object *snapshot(void) {
    char *argv[] = {(char *)f_hydra, (char *)"snapshot", (char *)"--json", NULL};
    char path[F_PATH];
    if (!f_path(path, sizeof(path), f_home, "state/v2/schema-version") && access(path, F_OK)) {
        json_object *data = json_object_new_object(); json_object_object_add(data, "heads", json_object_new_array());
        json_object_object_add(data, "projects", json_object_new_int(0)); return f_success("snapshot", data);
    }
    {
        json_object *result = f_run_hydra(argv, 15), *heads = f_field(f_field(result, "data"), "heads"); size_t i;
        char *capacity[] = {(char *)f_hydra, "admission", "status", "--summary", NULL};
        if (f_field(result, "data")) json_object_object_add(f_field(result, "data"), "admission", f_run_hydra(capacity, 5));
        for (i = 0; json_object_is_type(heads, json_type_array) && i < json_object_array_length(heads); i++) {
            json_object *head = json_object_array_get_idx(heads, i); const char *project = f_string(head, "project_id"); char *value;
            if (!project || !f_name(project) || snprintf(path, sizeof(path), "%s/state/v2/projects/%s/repo-root", f_home, project) >= (int)sizeof(path)) continue;
            value = f_read(path, F_PATH - 1); if (!value) continue;
            value[strcspn(value, "\r\n")] = '\0'; f_string_add(head, "project_path", value); free(value);
        }
        return result;
    }
}
static json_object *workflow_runs(void) {
    struct f_capture cap = {0}; char *argv[] = {(char *)"git", (char *)"rev-parse", (char *)"--git-common-dir", NULL};
    char path[F_PATH], root[F_PATH], *id; DIR *dir; struct dirent *entry;
    json_object *data = json_object_new_object(), *runs = json_object_new_array();
    json_object_object_add(data, "runs", runs);
    if (f_run(argv, NULL, 0, 5, &cap) || cap.status) { f_capture_free(&cap); json_object_put(data); return f_error("fleet-workflow", "invalid_project", "cannot resolve project identity"); }
    cap.out[strcspn(cap.out, "\r\n")] = '\0';
    if (f_path(path, sizeof(path), cap.out, "hydra/project-id")) { f_capture_free(&cap); json_object_put(data); return f_error("fleet-workflow", "invalid_project", "project path exceeds bounds"); }
    f_capture_free(&cap); id = f_read(path, 128);
    if (!id) { json_object_put(data); return f_error("fleet-workflow", "not_initialized", "initialize this project first"); }
    id[strcspn(id, "\r\n")] = '\0';
    if (!f_name(id) || snprintf(root, sizeof(root), "%s/state/v2/projects/%s/workflows/runs", f_home, id) >= (int)sizeof(root)) { free(id); json_object_put(data); return f_error("fleet-workflow", "invalid_project", "invalid project identity"); }
    free(id); dir = opendir(root);
    if (dir) {
        while ((entry = readdir(dir))) {
            char record[F_PATH], *state; json_object *run;
            if (strncmp(entry->d_name, "run_", 4) || !f_name(entry->d_name) || f_path(record, sizeof(record), root, entry->d_name) || f_path(path, sizeof(path), record, "state")) continue;
            state = f_read(path, 128); if (!state) continue; state[strcspn(state, "\r\n")] = '\0';
            run = json_object_new_object(); f_string_add(run, "run_id", entry->d_name); f_string_add(run, "recorded_state", state); free(state); json_object_array_add(runs, run);
        }
        closedir(dir);
    }
    return f_success("fleet-workflow-runs", data);
}
/* args have already passed the transport's bounded string-array validation. */
static json_object *admission_inspect(json_object *args, size_t count) {
    const char *sub = count ? f_text(json_object_array_get_idx(args, 0)) : "";
    const char *option = count == 2 ? f_text(json_object_array_get_idx(args, 1)) : "";
    bool status = !strcmp(sub, "status") && (count == 1 || (count == 2 && (!strcmp(option, "--json") || !strcmp(option, "--summary"))));
    bool inspect = !strcmp(sub, "inspect") && count == 2 && f_name(option);
    char *argv[] = {(char *)f_hydra, "admission", (char *)sub, count == 2 ? (char *)option : NULL, NULL};
    if (!status && !inspect) return f_error("fleet-admission", "invalid_input", "use status [--json|--summary] or inspect ID; configure policy on the receiving host");
    return f_run_hydra(argv, 5);
}

static json_object *builtin_request(const char *action, json_object *request) {
    if (!strcmp(action, "handshake")) return f_handshake();
    if (!strcmp(action, "list")) return snapshot();
    if (!strcmp(action, "overview")) return task_overview();
    if (!strcmp(action, "auth")) return auth_serve(request);
    if (!strcmp(action, "task")) return task_serve(request);
    return NULL;
}
json_object *f_serve(json_object *request) {
    const char *action = f_string(request, "action"), *project = f_string(request, "project"), *instance = f_string(request, "instance");
    json_object *operation = NULL;
    json_object *args = f_field(request, "args"); size_t i, n = 0, count = 0;
    char *argv[140]; unsigned seconds = 300;
    if (!f_number_is(request, "protocol", F_PROTOCOL) || !action)
        return f_error("fleet", "version_mismatch", "unsupported request protocol");
    { json_object *builtin = builtin_request(action, request); if (builtin) return builtin; }
    if (args && !json_object_is_type(args, json_type_array)) return f_error("fleet", "invalid_input", "args must be an array");
    count = args ? json_object_array_length(args) : 0;
    if (count > 128) return f_error("fleet", "invalid_input", "too many command arguments");
    for (i = 0; i < count; i++) {
        json_object *arg = json_object_array_get_idx(args, i);
        if (!json_object_is_type(arg, json_type_string) || strlen(json_object_get_string(arg)) != (size_t)json_object_get_string_len(arg))
            return f_error("fleet", "invalid_input", "arguments must be strings without NUL");
    }
    if (!strcmp(action, "admission")) return admission_inspect(args, count);
    if (strcmp(action, "doctor") && (!project || *project != '/' || chdir(project)))
        return f_error("fleet", "invalid_project", "an existing absolute remote project path is required");
    if (!strcmp(action, "export")) return f_bundle_export(project, args, f_string(request, "run"));
    if (!strcmp(action, "import")) return f_bundle_import(project, f_field(request, "bundle"));
    argv[n++] = (char *)f_hydra;
    if (!strcmp(action, "attach") || !strcmp(action, "signal") || !strcmp(action, "cancel")) {
        if (!instance || !f_name(instance) || count < 1 || count > 2) return f_error("fleet", "invalid_input", "head and observed --instance are required");
        argv[n++] = (char *)"fleet-local"; argv[n++] = (char *)(!strcmp(action, "attach") ? "session" : action);
        argv[n++] = (char *)json_object_get_string(json_object_array_get_idx(args, 0)); argv[n++] = (char *)instance;
        if (count == 2) argv[n++] = (char *)json_object_get_string(json_object_array_get_idx(args, 1));
    } else {
        bool allowed = !strcmp(action, "doctor") || !strcmp(action, "spawn") || !strcmp(action, "init") || !strcmp(action, "workflow");
        if (!allowed) return f_error("fleet", "capability_unavailable", "unknown operation");
        /* Doctor is observation, never a remote repair endpoint. */
        if (!strcmp(action, "doctor") && count) return f_error("fleet", "invalid_input", "doctor takes no remote flags");
        if (!strcmp(action, "workflow")) {
            const char *sub = count ? json_object_get_string(json_object_array_get_idx(args, 0)) : "";
            if (!strcmp(sub, "runs") && count == 1) return workflow_runs();
            if (strcmp(sub, "list") && strcmp(sub, "show") && strcmp(sub, "validate") && strcmp(sub, "dry-run") && strcmp(sub, "run") && strcmp(sub, "status") && strcmp(sub, "cancel") && strcmp(sub, "resume"))
                return f_error("fleet", "invalid_input", "unknown workflow operation");
        }
        argv[n++] = (char *)action;
        for (i = 0; i < count; i++) argv[n++] = (char *)json_object_get_string(json_object_array_get_idx(args, i));
    }
    argv[n] = NULL;
    setenv("HYDRA_NONINTERACTIVE", "1", 1);
    if (!strcmp(action, "init") && json_object_object_get_ex(request, "enrollment_operation_id", &operation))
        return enrollment_apply_request(request, argv, seconds);
    return f_run_hydra(argv, seconds);
}
