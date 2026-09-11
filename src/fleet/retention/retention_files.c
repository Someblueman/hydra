#define _XOPEN_SOURCE 700
#include "fleet/retention/retention.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/task/task.h"
#include "fleet/plan/plan.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool listed(const char *name, const char *const list[]) {
    for (size_t i = 0; list[i]; i++) if (!strcmp(name, list[i])) return true;
    return false;
}
bool rt_payload(bool task, const char *relative) {
    const char *base = strrchr(relative, '/'); base = base ? base + 1 : relative;
    const char *const task_files[] = {"package.json", "result.json", "source.bundle", "stdout", "stderr", NULL};
    const char *const run_files[] = {"compiled.json", "tasks.json", "data.json", "inputs.json", "delivery.json", "events.jsonl", "schedule.jsonl", "workspace-controls.log", NULL};
    const char *const controls[] = {"state", "attempts", "authoritative-attempt", "initial-ready-at", "initial-started-at", "started-at", "completed-at", "exit-code", "worker-pid", "command-pid", "request-id", "cancel-reconciled", NULL};
    if (task) return listed(relative, task_files) || !strncmp(relative, "inputs/", 7);
    if (!strncmp(relative, "artifacts/", 10)) return true;
    if (!strncmp(relative, "steps/", 6)) return !listed(base, controls) && strcmp(base, "owner.lock");
    return listed(relative, run_files) || (!strchr(relative, '/') && !strncmp(relative, "repair-", 7) && strstr(relative, ".json"));
}
/* Return a private, link-free parent fd; caller closes it. */
int rt_parent(int root, const char *relative, char name[256]) {
    char path[1024], *part, *next; int fd = -1;
    if (!task_path(relative) || f_copy(path, sizeof(path), relative) || (fd = dup(root)) < 0) return -1;
    part = path;
    while ((next = strchr(part, '/'))) {
        *next = '\0'; int child = task_owned_directory(fd, part, false); close(fd); fd = child;
        if (fd < 0) return -1;
        part = next + 1;
    }
    if (f_copy(name, 256, part)) { close(fd); return -1; }
    return fd;
}
static int rt_hash_bytes(const char *bytes, size_t used, char digest[65]);
int rt_hash_at(int directory, const char *name, char digest[65]) {
    struct stat st; char *bytes = NULL; size_t used = 0; int fd = -1, status = -1;
    fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > RT_BYTES) goto done;
    bytes = malloc((size_t)st.st_size + 1); if (!bytes) goto done;
    while (used < (size_t)st.st_size) {
        ssize_t n = read(fd, bytes + used, (size_t)st.st_size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        used += (size_t)n;
    }
    status = rt_hash_bytes(bytes, used, digest);
done:
    if (fd >= 0) close(fd);
    free(bytes); return status;
}
static int rt_hash_bytes(const char *bytes, size_t used, char digest[65]) {
    struct f_capture cap = {0}; int status = -1;
    char *args[] = {"shasum", "-a", "256", NULL};
    if (f_run(args, bytes, used, 30, &cap)) goto done;
    if (cap.status == 127) {
        char *gnu[] = {"sha256sum", NULL}; f_capture_free(&cap);
        if (f_run(gnu, bytes, used, 30, &cap)) goto done;
    }
    if (cap.status || cap.out_bytes < 64) goto done;
    memcpy(digest, cap.out, 64); digest[64] = '\0';
    if (!task_hex(digest, 64)) goto done;
    status = 0;
done:
    f_capture_free(&cap); return status;
}
static void references(json_object *value, json_object *refs, unsigned depth);
static void reference_object(json_object *value, json_object *refs, unsigned depth) {
    json_object_object_foreach(value, key, child) {
        const char *s = f_text(child);
        if (s && ((!strcmp(key, "task_id") && !strncmp(s, "task_", 5) && task_hex(s + 5, 64)) ||
                  (!strcmp(key, "run_id") && !strncmp(s, "run_", 4) && f_name(s))))
            json_object_object_add(refs, s, json_object_new_boolean(true));
        if (s && !strcmp(key, "submission_key") && f_name(s) && strlen(s) <= 128) {
            char binding[160]; snprintf(binding, sizeof(binding), "key:%s", s);
            json_object_object_add(refs, binding, json_object_new_boolean(true));
        }
        references(child, refs, depth + 1);
    }
}
static void reference_array(json_object *value, json_object *refs, unsigned depth) {
    for (size_t i = 0; i < json_object_array_length(value); i++)
        references(json_object_array_get_idx(value, i), refs, depth + 1);
}
static void references(json_object *value, json_object *refs, unsigned depth) {
    if (!value || depth > 32) return;
    if (json_object_is_type(value, json_type_object)) reference_object(value, refs, depth);
    else if (json_object_is_type(value, json_type_array)) reference_array(value, refs, depth);
}
static int scan_json(struct rt_record *record, const char *relative, const char *name, off_t size) {
    size_t n = strlen(name); char path[F_PATH]; json_object *object;
    if (n < 5 || strcmp(name + n - 5, ".json") || !strncmp(name, "retention", 9)) return 0;
    const char *const controls[] = {"receipt.json", "state.json", "acceptance.json", "dispatch.json", "compiled.json", "tasks.json", "data.json", "inputs.json", "delivery.json", NULL};
    bool control = listed(name, controls);
    if (size > F_LIMIT) return control ? -1 : 0;
    if (f_path(path, sizeof(path), record->path, relative)) return -1;
    char *bytes = f_read(path, F_LIMIT);
    object = bytes && plan_json_unique(bytes) ? f_parse(bytes) : NULL;
    free(bytes);
    if (!object && control) return -1;
    references(object, record->refs, 0); json_object_put(object); return 0;
}
static int scan_regular(struct rt_inventory *inventory, struct rt_record *record,
                        const char *relative, const char *name, const struct stat *st) {
    char path[F_PATH];
    if (!S_ISREG(st->st_mode) || st->st_size < 0 || st->st_size > INT64_C(1099511627776) ||
        ++inventory->files > RT_FILES || record->bytes > INT64_C(1099511627776) - st->st_size) return -1;
    record->bytes += st->st_size;
    if (strcmp(name, "owner.lock") && strcmp(name, "coordinator.lock") && strncmp(name, "retention", 9) &&
        st->st_mtime > record->newest) record->newest = st->st_mtime;
    if (!record->task && !strncmp(relative, "steps/", 6) && !strcmp(name, "state")) {
        if (f_path(path, sizeof(path), record->path, relative)) return -1;
        char *state = f_read(path, 128);
        if (state) state[strcspn(state, "\r\n")] = '\0';
        if (!state || (strcmp(state, "succeeded") && strcmp(state, "failed") && strcmp(state, "cancelled") && strcmp(state, "skipped"))) {
            record->protected = true; record->reason = "unresolved_step";
        }
        free(state);
    }
    if (rt_payload(record->task, relative)) {
        json_object *file = json_object_new_object(); f_string_add(file, "path", relative);
        json_object_object_add(file, "bytes", json_object_new_int64(st->st_size));
        json_object_array_add(record->files, file); record->payload_bytes += st->st_size;
    }
    return scan_json(record, relative, name, st->st_size);
}
static int scan_files(struct rt_inventory *inventory, struct rt_record *record, int parent,
                      const char *prefix, unsigned depth);
static int scan_entry(struct rt_inventory *inventory, struct rt_record *record, int parent,
                     const char *prefix, unsigned depth, const char *name) {
    struct stat st; char relative[1024];
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 0;
    /* Workspaces and Git worktrees are outside the evidence quota. */
    if (!*prefix && record->task && (!strcmp(name, "workspace") || !strcmp(name, "heads"))) return 0;
    if (snprintf(relative, sizeof(relative), "%s%s%s", prefix, *prefix ? "/" : "", name) >= (int)sizeof(relative) ||
        fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) return -1;
    if (!strcmp(name, ".git") || S_ISLNK(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) return -1;
    if (!S_ISDIR(st.st_mode)) return scan_regular(inventory, record, relative, name, &st);
    int child = task_owned_directory(parent, name, false);
    if (child < 0) return -1;
    int result = scan_files(inventory, record, child, relative, depth + 1); close(child);
    return result;
}
static int scan_files(struct rt_inventory *inventory, struct rt_record *record, int parent,
                      const char *prefix, unsigned depth) {
    DIR *dir = fdopendir(dup(parent)); struct dirent *entry; int status = -1;
    if (!dir || depth > 16) { if (dir) closedir(dir); return -1; }
    while ((entry = readdir(dir))) {
        if (scan_entry(inventory, record, parent, prefix, depth, entry->d_name)) goto done;
    }
    status = 0;
done:
    closedir(dir); return status;
}
int rt_record_files(struct rt_inventory *inventory, struct rt_record *record) {
    int fd = open(record->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW), status;
    if (fd < 0) return -1;
    status = scan_files(inventory, record, fd, "", 0); close(fd); return status;
}
