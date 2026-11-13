#define _XOPEN_SOURCE 700
#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Placement records live beside the alias store, private and inspectable:
 * $HYDRA_HOME/fleet/placements/TASK_ID/placement.json. */
static int private_directory(const char *path, bool create) {
    struct stat st;
    if (create && mkdir(path, 0700) && errno != EEXIST) return -1;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) return -1;
    return 0;
}
int placement_root(char root[F_PATH], bool create) {
    char fleet_dir[F_PATH];
    if (f_path(fleet_dir, F_PATH, f_home, "fleet") || f_path(root, F_PATH, fleet_dir, "placements")) return -1;
    if (create && (f_mkdirs(f_home) || private_directory(fleet_dir, true))) return -1;
    return private_directory(root, create);
}
int placement_directory(const char *task_id, char directory[F_PATH], bool create) {
    char root[F_PATH];
    if (!task_id || strncmp(task_id, "task_", 5) || !task_hex(task_id + 5, 64) || placement_root(root, create) ||
        f_path(directory, F_PATH, root, task_id)) return -1;
    return private_directory(directory, create);
}
/* The receiver derives the task ID from the submission key alone, so the
 * submitter can bind a record before any host accepts the task. */
int placement_task_id(const char *key, char id[70]) {
    char scratch[F_PATH], hash[65]; json_object *binding; int status;
    if (!f_name(key) || strlen(key) > 128 || snprintf(scratch, sizeof(scratch), "%s/hydra-placement-id.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") >= (int)sizeof(scratch) || !mkdtemp(scratch)) return -1;
    binding = json_object_new_object(); f_string_add(binding, "submission_key", key);
    status = task_json_hash(binding, scratch, hash); json_object_put(binding); f_remove_tree(scratch);
    if (status) return -1;
    snprintf(id, 70, "task_%s", hash);
    return 0;
}
int placement_record_digest(json_object *record, char digest[65]) {
    char scratch[F_PATH]; json_object *copy = f_parse(json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN)); int status;
    if (!copy || snprintf(scratch, sizeof(scratch), "%s/hydra-placement-digest.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") >= (int)sizeof(scratch) || !mkdtemp(scratch)) { json_object_put(copy); return -1; }
    json_object_object_del(copy, "sha256");
    status = task_json_hash(copy, scratch, digest); json_object_put(copy); f_remove_tree(scratch);
    return status;
}
json_object *placement_record_read(const char *task_id) {
    char directory[F_PATH], path[F_PATH], digest[65]; json_object *record; const char *stored;
    if (placement_directory(task_id, directory, false) || f_path(path, sizeof(path), directory, "placement.json")) return NULL;
    record = f_read_json(path, PLACEMENT_RECORD_LIMIT);
    stored = f_string(record, "sha256");
    if (!f_number_is(record, "schema_version", PLACEMENT_SCHEMA) || !f_string(record, "task_id") || strcmp(f_string(record, "task_id"), task_id) ||
        !task_hex(stored, 64) || placement_record_digest(record, digest) || strcmp(digest, stored)) { json_object_put(record); return NULL; }
    return record;
}
int placement_record_write(json_object *record) {
    char directory[F_PATH], digest[65];
    if (placement_directory(f_string(record, "task_id"), directory, true) || placement_record_digest(record, digest)) return -1;
    f_string_add(record, "sha256", digest);
    return task_write_json(directory, "placement.json", record, true);
}
bool placement_drained(const char *alias) {
    char path[F_PATH], dir[F_PATH]; struct stat st;
    if (!f_name(alias) || f_path(dir, sizeof(dir), f_home, "fleet/placement-drained") || f_path(path, sizeof(path), dir, alias)) return false;
    return !lstat(path, &st) && S_ISREG(st.st_mode);
}
json_object *placement_drained_list(void) {
    char dir[F_PATH]; DIR *handle; struct dirent *entry; json_object *names = json_object_new_array();
    if (f_path(dir, sizeof(dir), f_home, "fleet/placement-drained") || !(handle = opendir(dir))) return names;
    while ((entry = readdir(handle))) if (f_name(entry->d_name) && placement_drained(entry->d_name)) json_object_array_add(names, json_object_new_string(entry->d_name));
    closedir(handle);
    return names;
}
int placement_generation(const char *task_id, char host[128]) {
    json_object *record = placement_record_read(task_id); int generation;
    const char *assigned;
    host[0] = '\0';
    if (!record) return 0;
    generation = json_object_get_int(f_field(record, "generation")); assigned = f_string(record, "assigned_host");
    if (assigned && f_copy(host, 128, assigned)) generation = 0;
    json_object_put(record);
    return generation > 0 ? generation : 0;
}
/* A result is admitted only from the current assignment generation and host.
 * Receivers without a recorded generation report generation 1. */
json_object *placement_result_fence(json_object *receipt, const char *host) {
    char assigned[128]; const char *id = f_string(receipt, "task_id"); int generation, current;
    json_object *value = f_field(receipt, "assignment_generation");
    if (!id) return NULL;
    current = placement_generation(id, assigned);
    if (!current) return NULL;
    generation = json_object_is_type(value, json_type_int) ? json_object_get_int(value) : 1;
    if (generation == current && (!host || !strcmp(host, assigned))) return NULL;
    {
        json_object *error = f_error("fleet-task-collect", "stale_generation", "the result belongs to a superseded assignment generation or host; it is never admitted or promoted");
        json_object_object_add(f_field(error, "error"), "current_generation", json_object_new_int(current));
        json_object_object_add(f_field(error, "error"), "result_generation", json_object_new_int(generation));
        f_string_add(f_field(error, "error"), "current_host", assigned);
        return error;
    }
}
