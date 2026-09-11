#define _XOPEN_SOURCE 700
#include "fleet/retention/retention.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static char *scalar(const char *directory, const char *name) {
    char path[F_PATH], *value;
    if (f_path(path, sizeof(path), directory, name) || !(value = f_read(path, 128))) return NULL;
    value[strcspn(value, "\r\n")] = '\0'; return value;
}
static bool terminal(const char *state) {
    return state && (!strcmp(state, "succeeded") || !strcmp(state, "failed") || !strcmp(state, "cancelled") || !strcmp(state, "expired"));
}
static int owner_lock(struct rt_record *record) {
    char path[F_PATH]; struct stat st; int fd;
    if (f_path(path, sizeof(path), record->path, record->task ? "owner.lock" : "coordinator.lock")) return -1;
    fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) { close(fd); return -1; }
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    if (record->task ? flock(fd, LOCK_EX | LOCK_NB) : fcntl(fd, F_SETLK, &lock)) { close(fd); return -1; }
    return fd;
}
static const char *inspect_task(struct rt_record *record, json_object **state_out, json_object **accepted_out) {
    json_object *state = task_read_record(record->path, "state.json");
    json_object *accepted = task_read_record(record->path, "acceptance.json");
    const char *current = f_string(state, "state");
    if (!task_runtime_valid(state) || !f_number_is(accepted, "schema_version", 1) ||
        !f_string(accepted, "task_id") || strcmp(f_string(accepted, "task_id"), record->id) ||
        !f_string(accepted, "project") || !f_string(accepted, "project_id") ||
        !json_object_is_type(f_field(accepted, "accepted_at"), json_type_int) ||
        !task_hex(f_string(accepted, "spec_sha256"), 64)) current = NULL;
    if (f_string(accepted, "project_id") && f_copy(record->project, sizeof(record->project), f_string(accepted, "project_id"))) current = NULL;
    const char *key = f_string(accepted, "submission_key");
    if (!key || !f_name(key) || strlen(key) > 128 || snprintf(record->submission_key, sizeof(record->submission_key), "key:%s", key) >= (int)sizeof(record->submission_key)) current = NULL;
    const char *result_state = f_string(state, "result_state");
    if (result_state && (!strcmp(result_state, "sealing") || !strcmp(result_state, "unknown"))) current = NULL;
    const char *cancellation = f_string(state, "cancellation");
    if (cancellation && !strcmp(cancellation, "unknown")) current = NULL;
    *state_out = state; *accepted_out = accepted; return current;
}
static const char *inspect_run(struct rt_record *record, char **run_state_out, char **run_id_out, char **project_out) {
    char *run_state = scalar(record->path, "state"); char *run_id = scalar(record->path, "run-id");
    char *project = scalar(record->path, "project-id"); struct stat st; char path[F_PATH];
    const char *current = run_state;
    if (!run_id || strcmp(run_id, record->id) || !project || strcmp(project, record->project)) current = NULL;
    if (f_path(path, sizeof(path), record->path, ".drive.lock") || !lstat(path, &st)) current = NULL;
    if (f_path(path, sizeof(path), record->path, "residual-children.tsv") || (!lstat(path, &st) && st.st_size)) current = NULL;
    *run_state_out = run_state; *run_id_out = run_id; *project_out = project; return current;
}
static int inspect_record(struct rt_record *record) {
    struct stat st; char path[F_PATH]; json_object *state = NULL, *accepted = NULL, *expiry = NULL;
    const char *current; char *run_state = NULL, *run_id = NULL, *project = NULL;
    if (lstat(record->path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) return -1;
    record->owner = owner_lock(record);
    if (record->owner < 0) { record->protected = true; record->reason = "owner_locked_or_unavailable"; }
    current = record->task ? inspect_task(record, &state, &accepted) : inspect_run(record, &run_state, &run_id, &project);
    if (!terminal(current)) { record->protected = true; record->reason = "active_or_unresolved"; }
    if (f_path(path, sizeof(path), record->path, "retention-pin.json")) goto bad;
    if (!lstat(path, &st)) { record->protected = true; record->reason = "pinned"; }
    else if (errno != ENOENT) goto bad;
    expiry = retention_state(record->path);
    record->expired = expiry != NULL;
    json_object_put(expiry); json_object_put(accepted); json_object_put(state);
    free(run_state); free(run_id); free(project); return 0;
bad:
    json_object_put(accepted); json_object_put(state); free(run_state); free(run_id); free(project); return -1;
}
static int record_add(struct rt_inventory *inventory, const char *root, const char *project,
                      bool tasks, const char *id) {
    struct rt_record *record = &inventory->records[inventory->count++];
    record->owner = -1; record->task = tasks;
    record->files = json_object_new_array(); record->refs = json_object_new_object();
    if (f_copy(record->id, sizeof(record->id), id) || f_copy(record->project, sizeof(record->project), project) ||
        f_path(record->path, sizeof(record->path), root, id) || inspect_record(record) || rt_record_files(inventory, record)) return -1;
    int64_t audit_until = retention_audit_until(record->path);
    if (!record->expired && audit_until > inventory->now) {
        record->protected = true; record->reason = "declared_audit_window";
    } else if (!record->expired && (record->newest <= 0 || record->newest > inventory->now || inventory->now - record->newest < inventory->audit_seconds)) {
        record->protected = true; record->reason = "audit_window";
    }
    return 0;
}
static DIR *private_directory(const char *root) {
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); DIR *dir;
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) { close(fd); errno = EACCES; return NULL; }
    dir = fdopendir(fd); if (!dir) close(fd);
    return dir;
}
static bool record_name(const char *name, bool task) {
    return f_name(name) && (!task || task_hex(name + 5, 64));
}
static int record_list(struct rt_inventory *inventory, const char *root, const char *project, bool tasks) {
    DIR *dir = private_directory(root); struct dirent *entry; int result = -1;
    if (!dir) return errno == ENOENT ? 0 : -1;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, tasks ? "task_" : "run_", tasks ? 5 : 4)) continue;
        if (!record_name(entry->d_name, tasks) || inventory->count == RT_RECORDS) goto done;
        if (record_add(inventory, root, project, tasks, entry->d_name)) goto done;
    }
    result = 0;
done:
    closedir(dir); return result;
}
static int project_directory(const char *home) {
    /* Validate every ancestor, including an empty projects directory. */
    int root = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW), fd = root;
    const char *const parts[] = {"state", "v2", "projects", NULL};
    for (size_t i = 0; fd >= 0 && parts[i]; i++) {
        int child = task_owned_directory(fd, parts[i], false); close(fd); fd = child;
    }
    return fd;
}
static int scan_project(struct rt_inventory *inventory, int parent, const char *projects, const char *name) {
    char path[F_PATH]; int project, workflows, runs;
    if (!f_name(name)) return -1;
    project = task_owned_directory(parent, name, false); if (project < 0) return -1;
    workflows = task_owned_directory(project, "workflows", false); close(project);
    if (workflows < 0) return errno == ENOENT ? 0 : -1;
    runs = task_owned_directory(workflows, "runs", false); close(workflows);
    if (runs < 0) return errno == ENOENT ? 0 : -1;
    close(runs);
    if (snprintf(path, sizeof(path), "%s/%s/workflows/runs", projects, name) >= (int)sizeof(path)) return -1;
    return record_list(inventory, path, name, false);
}
static int scan_projects(struct rt_inventory *inventory, const char *home, const char *projects) {
    struct dirent *entry; int fd = project_directory(home);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    DIR *dir = fdopendir(fd); if (!dir) { close(fd); return -1; }
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "project_", 8)) continue;
        if (scan_project(inventory, dirfd(dir), projects, entry->d_name)) goto done;
    }
    closedir(dir); return 0;
done:
    closedir(dir); return -1;
}
static bool protect_from_record(struct rt_inventory *inventory, struct rt_record *from) {
    bool changed = false;
    if (!from->protected) return false;
    for (size_t j = 0; j < inventory->count; j++) {
        struct rt_record *to = &inventory->records[j];
        if (!to->protected && (f_field(from->refs, to->id) || (*to->submission_key && f_field(from->refs, to->submission_key)))) {
            to->protected = true; to->reason = "referenced_evidence"; changed = true;
        }
    }
    return changed;
}
static void protect_references(struct rt_inventory *inventory) {
    /* All still-retained roots preserve their complete local reference closure.
     * Ambiguous run IDs protect every matching record, never the first match. */
    for (size_t pass = 0; pass < inventory->count; pass++) {
        bool changed = false;
        for (size_t i = 0; i < inventory->count; i++) {
            if (protect_from_record(inventory, &inventory->records[i])) changed = true;
        }
        if (!changed) break;
    }
}
int rt_scan(struct rt_inventory *inventory) {
    char home[F_PATH], tasks[F_PATH], projects[F_PATH];
    if (!realpath(f_home, home) || task_store_root(tasks) ||
        snprintf(projects, sizeof(projects), "%s/state/v2/projects", home) >= (int)sizeof(projects) ||
        record_list(inventory, tasks, "", true) || scan_projects(inventory, home, projects)) return -1;
    protect_references(inventory); return 0;
}
void rt_release(struct rt_inventory *inventory) {
    for (size_t i = 0; i < inventory->count; i++) {
        struct rt_record *record = &inventory->records[i];
        if (record->owner >= 0) close(record->owner);
        json_object_put(record->files); json_object_put(record->refs);
    }
    free(inventory->records);
}
