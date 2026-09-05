#include "task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

json_object *task_cancel(const char *id) {
    json_object *response = task_status(id), *data = f_field(response, "data"), *request = NULL, *state = NULL;
    const char *current = f_string(f_field(data, "runtime"), "state"); char root[F_PATH], directory[F_PATH], path[F_PATH]; int owner = -1;
    if (!json_object_get_boolean(f_field(response, "ok"))) return response;
    if (!strcmp(current, "succeeded") || !strcmp(current, "failed") || !strcmp(current, "expired") || !strcmp(current, "cancelled")) {
        f_string_add(f_field(data, "runtime"), "cancel_response", "already_terminal"); return response;
    }
    if (task_store_root(root) || f_path(directory, sizeof(directory), root, id)) goto bad;
    request = json_object_new_object(); json_object_object_add(request, "schema_version", json_object_new_int(1));
    f_string_add(request, "spec_sha256", f_string(data, "spec_sha256"));
    json_object_object_add(request, "requested_at", json_object_new_int64((int64_t)time(NULL)));
    if (task_write_json(directory, "cancel.json", request, false)) {
        json_object *existing = task_read_record(directory, "cancel.json"); const char *digest = f_string(existing, "spec_sha256");
        bool valid = f_number_is(existing, "schema_version", 1) && digest && !strcmp(digest, f_string(data, "spec_sha256")) &&
            json_object_is_type(f_field(existing, "requested_at"), json_type_int);
        json_object_put(existing); if (!valid || task_sync_dir(directory)) goto bad;
    }
    if (f_path(path, sizeof(path), directory, "owner.lock")) goto bad;
    owner = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600); if (owner < 0) goto bad;
    if (!flock(owner, LOCK_EX | LOCK_NB)) {
        struct stat st;
        state = task_read_record(directory, "state.json"); if (!task_runtime_valid(state)) goto bad;
        if (!strcmp(f_string(state, "state"), "accepted")) {
            if (f_path(path, sizeof(path), directory, "launch.json")) goto bad;
            if (lstat(path, &st) && errno == ENOENT) {
                f_string_add(state, "state", "cancelled"); f_string_add(state, "cancellation", "confirmed_stopped");
                f_string_add(state, "cancellation_scope", "not_launched");
                if (task_write_json(directory, "state.json", state, true)) goto bad;
            }
        }
    } else if (errno != EWOULDBLOCK && errno != EAGAIN) goto bad;
    close(owner); owner = -1; json_object_put(response); response = task_status(id); goto done;
bad:
    json_object_put(response); response = f_error("fleet-task-cancel", "outcome_unknown", "cancellation could not be durably confirmed; inspect this task without replaying execution");
done:
    if (owner >= 0) close(owner);
    json_object_put(request); json_object_put(state); return response;
}
/* Walk recorded state paths without following any child symlink. */
static int descend(int parent, const char *name) {
    int child;
    if (parent < 0) return -1;
    child = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    close(parent); return child;
}
static int log_directory(const char *id, json_object *runtime, json_object *request, json_object *log) {
    const char *source = f_field(request, "source") ? f_string(request, "source") : "owner";
    const char *step = f_string(request, "step"), *kind = f_string(runtime, "work_kind");
    const char *project = f_string(runtime, "execution_project_id"), *run = f_string(runtime, "run_id");
    char root[F_PATH], path[F_PATH]; int dir;
    if (!source) { errno = EINVAL; return -1; }
    f_string_add(log, "source", source);
    if (!strcmp(source, "owner")) {
        if (f_field(request, "step") || f_field(request, "attempt") || task_store_root(root) || f_path(path, sizeof(path), root, id)) { errno = EINVAL; return -1; }
        return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    }
    if (strcmp(source, "work") || !kind) { errno = EINVAL; return -1; }
    if (!strcmp(kind, "exec")) {
        if (f_field(request, "step") || f_field(request, "attempt")) { errno = EINVAL; return -1; }
    } else if (strcmp(kind, "workflow") || !step || !f_name(step) || strlen(step) > 64) { errno = EINVAL; return -1; }
    if (f_field(request, "attempt") && (!json_object_is_type(f_field(request, "attempt"), json_type_int) ||
        json_object_get_int64(f_field(request, "attempt")) < 1 || json_object_get_int64(f_field(request, "attempt")) > 10000)) { errno = EINVAL; return -1; }
    if (!run || !project) { errno = ENOENT; return -1; }
    if (!f_name(project) || strncmp(project, "project_", 8) || !f_name(run) || strncmp(run, "run_", 4)) { errno = EINVAL; return -1; }
    f_string_add(log, "run_id", run);
    dir = open(f_home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    dir = descend(dir, "state"); dir = descend(dir, "v2"); dir = descend(dir, "projects"); dir = descend(dir, project);
    if (!strcmp(kind, "exec")) {
        const char *head = f_string(runtime, "execution_head_id");
        if (!head || !f_name(head) || strncmp(head, "head_", 5)) { if (dir >= 0) close(dir); errno = EINVAL; return -1; }
        f_string_add(log, "head_id", head);
        dir = descend(dir, "exec"); dir = descend(dir, run); dir = descend(dir, head);
    } else {
        json_object *attempt = f_field(request, "attempt"); int64_t number = attempt ? json_object_get_int64(attempt) : 1;
        if ((attempt && !json_object_is_type(attempt, json_type_int)) || number < 1 || number > 10000) { if (dir >= 0) close(dir); errno = EINVAL; return -1; }
        snprintf(path, sizeof(path), "attempt-%lld", (long long)number);
        f_string_add(log, "step", step); json_object_object_add(log, "attempt", json_object_new_int64(number));
        dir = descend(dir, "workflows"); dir = descend(dir, "runs"); dir = descend(dir, run);
        dir = descend(dir, "steps"); dir = descend(dir, step); dir = descend(dir, path);
    }
    return dir;
}
json_object *task_logs(const char *id, json_object *request) {
    json_object *response = task_status(id), *data = f_field(response, "data"), *log = NULL;
    const char *stream = f_field(request, "stream") ? f_string(request, "stream") : "stdout";
    json_object *start = f_field(request, "offset"), *count = f_field(request, "limit");
    int64_t offset = start ? json_object_get_int64(start) : 0, limit = count ? json_object_get_int64(count) : 4096;
    char *bytes = NULL, *hex = NULL, *preview = NULL;
    int dir = -1, fd = -1; struct stat st; ssize_t length = 0; size_t i; const char *digits = "0123456789abcdef";
    if (!json_object_get_boolean(f_field(response, "ok"))) return response;
    if ((start && !json_object_is_type(start, json_type_int)) || (count && !json_object_is_type(count, json_type_int)) || offset < 0 || limit < 1 || !stream || (strcmp(stream, "stdout") && strcmp(stream, "stderr")) || !limit || limit > 65536 || offset > TASK_FILE_LIMIT) goto bad;
    log = json_object_new_object(); f_string_add(log, "stream", stream);
    dir = log_directory(id, f_field(data, "runtime"), request, log);
    if (dir >= 0) fd = openat(dir, stream, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT) goto bad;
        json_object_object_add(log, "available", json_object_new_boolean(false));
        json_object_object_add(data, "log", log); log = NULL; goto done;
    }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) goto bad;
    {
        char root[F_PATH], directory[F_PATH]; json_object *package;
        if (task_store_root(root) || f_path(directory, sizeof(directory), root, id) || f_path(root, sizeof(root), directory, "package.json")) goto bad;
        package = f_read_json(root, TASK_PACKAGE_LIMIT);
        int budget = json_object_get_int(f_field(f_field(f_field(package, "spec"), "limits"), "log_bytes"));
        json_object_put(package); if (budget < 1 || budget > (int)TASK_FILE_LIMIT) goto bad;
        json_object_object_add(log, "limit_reached", json_object_new_boolean(st.st_size >= budget));
        if (st.st_size > budget) { st.st_size = budget; json_object_object_add(log, "truncated", json_object_new_boolean(true)); }
    }
    bytes = malloc((size_t)limit); hex = malloc((size_t)limit * 2 + 1); preview = malloc((size_t)limit + 1);
    if (!bytes || !hex || !preview) goto bad;
    if ((off_t)offset >= st.st_size) limit = 0;
    else if ((off_t)limit > st.st_size - (off_t)offset) limit = (unsigned)(st.st_size - (off_t)offset);
    do { length = pread(fd, bytes, limit, (off_t)offset); } while (length < 0 && errno == EINTR);
    if (length < 0) goto bad;
    for (i = 0; i < (size_t)length; i++) {
        unsigned char c = (unsigned char)bytes[i]; hex[2*i] = digits[c >> 4]; hex[2*i+1] = digits[c & 15];
        preview[i] = c == '\n' || c == '\t' || (c >= 32 && c < 127) ? (char)c : '?';
    }
    hex[2*i] = '\0'; preview[i] = '\0';
    json_object_object_add(log, "available", json_object_new_boolean(true));
    json_object_object_add(log, "offset", json_object_new_int64(offset));
    json_object_object_add(log, "next_offset", json_object_new_int64(offset + length));
    json_object_object_add(log, "available_bytes", json_object_new_int64((int64_t)st.st_size));
    json_object_object_add(log, "eof", json_object_new_boolean(offset + length >= st.st_size));
    f_string_add(log, "hex", hex); f_string_add(log, "text_preview", preview);
    json_object_object_add(data, "log", log); log = NULL; goto done;
bad:
    json_object_put(response); response = f_error("fleet-task-logs", "invalid_log", "select owner/work logs, stdout/stderr, offset 0-524288, limit 1-65536, and a safe workflow step/attempt; log paths must be regular and link-free");
done:
    if (fd >= 0) close(fd);
    if (dir >= 0) close(dir);
    json_object_put(log); free(bytes); free(hex); free(preview); return response;
}
