#include "task.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool cancelled(void *context) {
    struct task_control *control = context;
    json_object *request; const char *digest;
    if (control->cancel_seen) return true;
    request = task_read_record(control->directory, "cancel.json"); digest = f_string(request, "spec_sha256");
    if (f_number_is(request, "schema_version", 1) && digest && !strcmp(digest, control->digest) &&
        json_object_is_type(f_field(request, "requested_at"), json_type_int)) {
        control->cancel_seen = true;
        f_string_add(control->state, "cancellation", "delivered");
        json_object_object_add(control->state, "cancel_requested_at", json_object_get(f_field(request, "requested_at")));
        (void)task_write_json(control->directory, "state.json", control->state, true);
    }
    json_object_put(request); return control->cancel_seen;
}
static void observe_run(void *context, const char *text) {
    struct task_control *control = context;
    const char *kind = f_string(control->state, "work_kind"), *project = f_string(control->state, "execution_project_id"), *start, *end;
    const char *prefix = "{\"schema_version\":1,\"ok\":true,\"command\":\"exec\",\"data\":{\"run_id\":\"";
    char run[128], path[F_PATH]; struct stat st; size_t size;
    if (!kind || !project || f_string(control->state, "run_id")) return;
    if (!strcmp(kind, "exec")) {
        if (strncmp(text, prefix, strlen(prefix))) return;
        start = text + strlen(prefix); end = strchr(start, '"');
    } else { start = text; end = strchr(start, '\n'); }
    if (!end || (size = (size_t)(end - start)) >= sizeof(run)) return;
    memcpy(run, start, size); run[size] = '\0';
    if (!f_name(run) || strncmp(run, "run_", 4) || !f_name(project)) return;
    if (snprintf(path, sizeof(path), "%s/state/v2/projects/%s/%s/%s", f_home, project, !strcmp(kind, "exec") ? "exec" : "workflows/runs", run) >= (int)sizeof(path) ||
        lstat(path, &st) || !S_ISDIR(st.st_mode)) return;
    f_string_add(control->state, "run_id", run);
    (void)task_write_json(control->directory, "state.json", control->state, true);
}
int task_control_open(struct task_control *control, const char *directory, const char *digest, json_object *state, json_object *limits) {
    const char *names[] = {"stdout", "stderr"}; size_t i;
    memset(control, 0, sizeof(*control)); control->directory = directory; control->digest = digest; control->state = state;
    control->process.log_fd[0] = control->process.log_fd[1] = -1;
    control->process.stop = cancelled; control->process.context = control;
    control->process.observe = observe_run;
    control->process.grace_seconds = (unsigned)json_object_get_int(f_field(limits, "cancellation_seconds"));
    for (i = 0; i < 2; i++) {
        char path[F_PATH];
        if (f_path(path, sizeof(path), directory, names[i])) return -1;
        control->process.log_fd[i] = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (control->process.log_fd[i] < 0 || fcntl(control->process.log_fd[i], F_SETFD, FD_CLOEXEC)) return -1;
        control->process.remaining[i] = (size_t)json_object_get_int(f_field(limits, "log_bytes"));
    }
    return task_sync_dir(directory);
}
static char *scalar(const char *directory, const char *name) {
    char path[F_PATH], *value;
    if (f_path(path, sizeof(path), directory, name) || !(value = f_read(path, 128))) return NULL;
    value[strcspn(value, "\r\n")] = '\0';
    if (!f_name(value)) { free(value); return NULL; }
    return value;
}
/* Plain terminal heads are not executing agents. Other profiles need independent
 * shutdown evidence; stopping the CLI process cannot certify those sessions. */
static bool no_agent_workers(json_object *state) {
    const char *workspace = f_string(state, "workspace"); char common[F_PATH], root[F_PATH];
    char *project; DIR *dir; struct dirent *entry; bool quiet = true;
    if (!workspace) return true; /* No spawn step was reached. */
    if (f_path(common, sizeof(common), workspace, ".git/hydra") || !(project = scalar(common, "project-id"))) return false;
    if (snprintf(root, sizeof(root), "%s/state/v2/projects/%s/heads", f_home, project) >= (int)sizeof(root)) { free(project); return false; }
    free(project); dir = opendir(root); if (!dir) return false;
    while ((entry = readdir(dir))) {
        char head[F_PATH], instance_dir[F_PATH]; char *instance, *profile;
        if (strncmp(entry->d_name, "head_", 5)) continue;
        if (!f_name(entry->d_name) || f_path(head, sizeof(head), root, entry->d_name) || !(instance = scalar(head, "current-instance"))) { quiet = false; break; }
        if (snprintf(instance_dir, sizeof(instance_dir), "%s/instances/%s", head, instance) >= (int)sizeof(instance_dir)) { free(instance); quiet = false; break; }
        free(instance); profile = scalar(instance_dir, "resolved-profile");
        if (!profile || strcmp(profile, "none")) quiet = false;
        free(profile); if (!quiet) break;
    }
    closedir(dir); return quiet;
}
void task_control_close(struct task_control *control) {
    size_t i;
    for (i = 0; i < 2; i++) if (control->process.log_fd[i] >= 0) {
        if (fsync(control->process.log_fd[i])) control->process.log_error = true;
        close(control->process.log_fd[i]); control->process.log_fd[i] = -1;
    }
    if (control->process.truncated) json_object_object_add(control->state, "log_truncated", json_object_new_boolean(true));
    if (control->process.log_error) f_string_add(control->state, "log_error", "io_failed");
    if (control->cancel_seen) {
        bool confirmed = !control->process.stop_unknown && no_agent_workers(control->state);
        f_string_add(control->state, "cancellation", confirmed ? "confirmed_stopped" : "unknown");
        f_string_add(control->state, "cancellation_scope", "managed_commands");
        f_string_add(control->state, "state", confirmed ? "cancelled" : "outcome_unknown");
        if (confirmed) json_object_object_del(control->state, "failure");
        else f_string_add(control->state, "failure", "cancellation_unconfirmed");
    }
}
void task_cancel_view(const char *directory, const char *digest, json_object *state) {
    json_object *request = task_read_record(directory, "cancel.json"); const char *bound = f_string(request, "spec_sha256");
    if (f_number_is(request, "schema_version", 1) && bound && !strcmp(bound, digest)) {
        json_object_object_add(state, "cancel_requested_at", json_object_get(f_field(request, "requested_at")));
        if (!strcmp(f_string(state, "state"), "outcome_unknown")) f_string_add(state, "cancellation", "unknown");
        else if (!f_string(state, "cancellation")) f_string_add(state, "cancellation", "requested");
    }
    json_object_put(request);
}
