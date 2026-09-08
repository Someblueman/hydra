#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/task/task.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Resume and decisions bind to the already-created execution checkout. */
int task_workspace(const char *directory, json_object *state) {
    char workspace[F_PATH], canonical[F_PATH], path[F_PATH], *project = NULL;
    const char *stored = f_string(state, "workspace"), *identity = f_string(state, "execution_project_id");
    const char *clear[] = {"HYDRA_PROJECT_ID", "HYDRA_HEAD_ID", "HYDRA_INSTANCE_ID", "HYDRA_STATE_DIR", "HYDRA_BRANCH", "HYDRA_WORKTREE", NULL};
    struct stat st; size_t i;
    if (!stored || !identity || f_path(workspace, sizeof(workspace), directory, "workspace") ||
        lstat(workspace, &st) || !S_ISDIR(st.st_mode) || !realpath(workspace, canonical) || strcmp(canonical, stored) ||
        f_path(path, sizeof(path), workspace, ".git/hydra/project-id") || !(project = f_read(path, 128))) return -1;
    project[strcspn(project, "\r\n")] = '\0';
    bool matches = !strcmp(project, identity); free(project);
    if (!matches || chdir(workspace) || f_path(path, sizeof(path), directory, "inputs") || lstat(path, &st) || !S_ISDIR(st.st_mode)) return -1;
    for (i = 0; clear[i]; i++) unsetenv(clear[i]);
    return setenv("HYDRA_HOME", f_home, 1) || setenv("HYDRA_NONINTERACTIVE", "1", 1) || setenv("HYDRA_TASK_INPUT_DIR", path, 1) ? -1 : 0;
}
json_object *task_approval(const char *id, json_object *request) {
    json_object *response = task_status(id), *data = f_field(response, "data"), *state = NULL, *parsed = NULL;
    const char *operation = f_string(request, "operation"), *trust = f_string(request, "trust_spec");
    const char *request_id = f_string(request, "request_id"), *decision = f_string(request, "decision"), *actor = f_string(request, "by");
    bool decide = !strcmp(operation, "decide");
    char root[F_PATH], directory[F_PATH], path[F_PATH]; int owner = -1;
    struct f_capture cap = {0}; char *argv[10]; size_t n = 0;
    if (!json_object_get_boolean(f_field(response, "ok"))) return response;
    if (decide && (!task_hex(trust, 64) || strcmp(trust, f_string(data, "spec_sha256")))) {
        json_object_put(response); return f_error("fleet-task-decide", "trust_required", "supply the exact reviewed specification digest with --trust-spec");
    }
    if (decide && (!f_name(request_id) || !decision || (strcmp(decision, "approve") && strcmp(decision, "reject")) ||
        (f_field(request, "by") && (!actor || strlen(actor) > 128 || strpbrk(actor, "\r\n\t"))))) goto bad;
    if (task_store_root(root) || f_path(directory, sizeof(directory), root, id) || f_path(path, sizeof(path), directory, "owner.lock")) goto bad;
    owner = open(path, O_RDWR | O_NOFOLLOW);
    if (owner < 0 || fcntl(owner, F_SETFD, FD_CLOEXEC) || flock(owner, LOCK_EX | LOCK_NB)) goto bad;
    state = task_read_record(directory, "state.json");
    if (!task_runtime_valid(state) || !f_string(state, "work_kind") || strcmp(f_string(state, "work_kind"), "workflow") ||
        !f_name(f_string(state, "run_id")) || (decide && strcmp(f_string(state, "state"), "waiting_approval")) || task_workspace(directory, state)) goto bad;
    argv[n++] = (char *)f_hydra; argv[n++] = "workflow"; argv[n++] = (char *)operation; argv[n++] = (char *)f_string(state, "run_id");
    if (decide) { argv[n++] = (char *)request_id; argv[n++] = (char *)decision; if (actor) { argv[n++] = "--by"; argv[n++] = (char *)actor; } }
    else argv[n++] = "--json";
    argv[n] = NULL;
    if (f_run(argv, NULL, 0, 10, &cap) || cap.status) goto bad;
    if (!decide) {
        parsed = f_parse(cap.out);
        if (!json_object_get_boolean(f_field(parsed, "ok")) || !json_object_is_type(f_field(f_field(parsed, "data"), "requests"), json_type_array)) goto bad;
        json_object_object_add(data, "requests", json_object_get(f_field(f_field(parsed, "data"), "requests")));
    } else { f_string_add(data, "request_id", request_id); f_string_add(data, "decision", decision); }
    goto done;
bad:
    json_object_put(response); response = f_error("fleet-task-approval", "recovery_required", "request unavailable, stale, or owner active; inspect task status and requests, then explicitly resume to refresh changed evidence");
done:
    if (owner >= 0) close(owner);
    json_object_put(state); json_object_put(parsed); f_capture_free(&cap); return response;
}
