#define _XOPEN_SOURCE 700
#include "fleet/retention/retention.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

json_object *retention_state(const char *directory) {
    char path[F_PATH]; struct stat st = {0}; json_object *state = NULL;
    if (f_path(path, sizeof(path), directory, "retention.json")) return NULL;
    if (lstat(path, &st) && errno == ENOENT) return NULL;
    if (S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022)) state = task_read_record(directory, "retention.json");
    const char *value = f_string(state, "state");
    if (!f_number_is(state, "schema_version", 1) || !value || (strcmp(value, "expired") && strcmp(value, "expiring"))) {
        json_object_put(state); state = json_object_new_object();
        json_object_object_add(state, "schema_version", json_object_new_int(1)); f_string_add(state, "state", "invalid_expiry_record");
    }
    return state;
}
bool retention_expired(const char *directory) {
    json_object *state = retention_state(directory); bool expired = state != NULL;
    json_object_put(state); return expired;
}
static json_object *manifest_create(struct rt_record *record, int directory) {
    json_object *manifest = json_object_new_object(), *files = json_object_new_array();
    json_object_object_add(manifest, "schema_version", json_object_new_int(1)); f_string_add(manifest, "record_id", record->id);
    json_object_object_add(manifest, "files", files);
    for (size_t i = 0; i < json_object_array_length(record->files); i++) {
        json_object *file = json_object_array_get_idx(record->files, i), *copy; char name[256], digest[65];
        int parent = rt_parent(directory, f_string(file, "path"), name);
        if (parent < 0) goto bad;
        int result = rt_hash_at(parent, name, digest); close(parent); if (result) goto bad;
        copy = f_parse(json_object_to_json_string_ext(file, JSON_C_TO_STRING_PLAIN));
        f_string_add(copy, "sha256", digest); json_object_array_add(files, copy);
    }
    if (strlen(json_object_to_json_string_ext(manifest, JSON_C_TO_STRING_PLAIN)) <= F_LIMIT) return manifest;
bad:
    json_object_put(manifest); return NULL;
}
static json_object *manifest_read(struct rt_record *record, int directory, json_object *state) {
    char path[F_PATH], digest[65]; json_object *manifest = NULL;
    if (!task_hex(f_string(state, "manifest_sha256"), 64) ||
        rt_hash_at(directory, "retention-manifest.json", digest) || strcmp(digest, f_string(state, "manifest_sha256")) ||
        f_path(path, sizeof(path), record->path, "retention-manifest.json") || !(manifest = f_read_json(path, F_LIMIT))) return NULL;
    if (!f_number_is(manifest, "schema_version", 1) || !f_string(manifest, "record_id") ||
        strcmp(f_string(manifest, "record_id"), record->id) || !json_object_is_type(f_field(manifest, "files"), json_type_array) ||
        json_object_array_length(f_field(manifest, "files")) > RT_FILES) { json_object_put(manifest); return NULL; }
    return manifest;
}
static int remove_file(struct rt_record *record, int directory, json_object *file) {
    const char *relative = f_string(file, "path");
    char name[256], digest[65]; struct stat st; int parent;
    if (!relative || !rt_payload(record->task, relative) || !task_hex(f_string(file, "sha256"), 64) ||
        !json_object_is_type(f_field(file, "bytes"), json_type_int) || json_object_get_int64(f_field(file, "bytes")) < 0 ||
        (parent = rt_parent(directory, relative, name)) < 0) return -1;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) {
        int saved = errno; close(parent); return saved == ENOENT ? 0 : -1;
    }
    bool valid = S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022) &&
        st.st_size == json_object_get_int64(f_field(file, "bytes")) && !rt_hash_at(parent, name, digest) &&
        !strcmp(digest, f_string(file, "sha256"));
    int status = valid ? unlinkat(parent, name, 0) : -1;
    if (!status) status = fsync(parent);
    close(parent); return status;
}
static int remove_manifest_files(struct rt_record *record, int directory, json_object *manifest) {
    json_object *files = f_field(manifest, "files");
    for (size_t i = 0; i < json_object_array_length(files); i++) {
        if (remove_file(record, directory, json_object_array_get_idx(files, i))) return -1;
    }
    return 0;
}
static json_object *publish_expiry(struct rt_inventory *inventory, struct rt_record *record,
                               int directory, json_object *manifest) {
    char digest[65]; json_object *state;
    if (task_write_json(record->path, "retention-manifest.json", manifest, true) ||
        rt_hash_at(directory, "retention-manifest.json", digest)) return NULL;
    state = json_object_new_object(); json_object_object_add(state, "schema_version", json_object_new_int(1));
    f_string_add(state, "state", "expiring"); f_string_add(state, "record_id", record->id);
    f_string_add(state, "manifest_sha256", digest);
    json_object_object_add(state, "expired_at", json_object_new_int64(inventory->now));
    json_object_object_add(state, "audit_seconds", json_object_new_int64(inventory->audit_seconds));
    json_object_object_add(state, "evidence_bytes", json_object_new_int64(record->payload_bytes));
    json_object_object_add(state, "evidence_files", json_object_new_int64((int64_t)json_object_array_length(record->files)));
    /* Publication precedes the first unlink. Readers treat even interrupted
     * expiry as unavailable evidence, and replay remains forbidden. */
    if (task_write_json(record->path, "retention.json", state, false)) { json_object_put(state); return NULL; }
    return state;
}
int rt_expire(struct rt_inventory *inventory, struct rt_record *record) {
    int directory = open(record->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW), status = -1;
    json_object *state = retention_state(record->path), *manifest = NULL;
    if (directory < 0) goto done;
    if (state) {
        const char *value = f_string(state, "state");
        if (!value || (strcmp(value, "expired") && strcmp(value, "expiring"))) goto done;
        manifest = manifest_read(record, directory, state);
    } else {
        manifest = manifest_create(record, directory);
        if (manifest) state = publish_expiry(inventory, record, directory, manifest);
    }
    if (!manifest || !state || remove_manifest_files(record, directory, manifest)) goto done;
    f_string_add(state, "state", "expired"); status = task_write_json(record->path, "retention.json", state, true);
done:
    if (directory >= 0) close(directory);
    json_object_put(state); json_object_put(manifest); return status;
}
