#include "fleet/discovery/discovery.h"
#include "fleet/enrollment/enrollment.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Private per-source snapshot cache under $HYDRA_HOME/fleet/discovery/sources.
 * The cache is the previous observation for diffs and stale reporting only;
 * it never becomes a selection by itself. */
static int cache_path(const char *kind, const char *locator, char path[F_PATH]) {
    char directory[F_PATH], label[F_PATH + 80], digest[65], name[72]; json_object *value; int status;
    if (snprintf(label, sizeof(label), "%s:%s", kind, locator) >= (int)sizeof(label)) return -1;
    value = json_object_new_string(label); status = enrollment_hash(value, digest); json_object_put(value);
    if (status || hd_private_directory(directory, "sources") || snprintf(name, sizeof(name), "%s.json", digest) >= (int)sizeof(name)) return -1;
    return f_path(path, F_PATH, directory, name);
}
static bool snapshot_valid(json_object *snapshot) {
    const char *kind = f_string(snapshot, "kind");
    return f_number_is(snapshot, "schema_version", 1) && kind && !strcmp(kind, "fleet-discovery-source-snapshot") &&
        json_object_is_type(f_field(snapshot, "source"), json_type_object) &&
        json_object_is_type(f_field(snapshot, "records"), json_type_array) && json_object_array_length(f_field(snapshot, "records")) <= HD_SOURCE_RECORDS &&
        json_object_is_type(f_field(snapshot, "observed_at"), json_type_int);
}
json_object *hd_cache_read(const char *kind, const char *locator) {
    char path[F_PATH]; struct stat st; json_object *snapshot;
    if (cache_path(kind, locator, path) || lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) return NULL;
    snapshot = f_read_json(path, HD_SOURCE_BYTES * 2);
    if (snapshot_valid(snapshot)) return snapshot;
    json_object_put(snapshot); return NULL;
}
static json_object *summary(json_object *snapshot) {
    json_object *out = json_object_new_object(); int64_t observed = json_object_get_int64(f_field(snapshot, "observed_at"));
    json_object_object_add(out, "observed_at", json_object_new_int64(observed));
    json_object_object_add(out, "age_seconds", json_object_new_int64((int64_t)time(NULL) - observed));
    json_object_object_add(out, "record_count", json_object_new_int64((int64_t)json_object_array_length(f_field(snapshot, "records"))));
    if (f_string(snapshot, "status")) f_string_add(out, "status", f_string(snapshot, "status"));
    return out;
}
json_object *hd_cache_summary(const char *kind, const char *locator) {
    json_object *snapshot = hd_cache_read(kind, locator), *out;
    if (!snapshot) return NULL;
    out = summary(snapshot); json_object_put(snapshot); return out;
}
int hd_cache_write(json_object *report) {
    char path[F_PATH]; json_object *snapshot = json_object_new_object(), *source = json_object_new_object(); int status;
    const char *const keys[] = {"kind", "locator", "scope", "client", NULL}; size_t i;
    for (i = 0; keys[i]; i++) if (f_string(report, keys[i])) f_string_add(source, keys[i], f_string(report, keys[i]));
    json_object_object_add(snapshot, "schema_version", json_object_new_int(1));
    f_string_add(snapshot, "kind", "fleet-discovery-source-snapshot");
    json_object_object_add(snapshot, "source", source);
    f_string_add(snapshot, "status", f_string(report, "status"));
    json_object_object_add(snapshot, "observed_at", json_object_get(f_field(report, "observed_at")));
    json_object_object_add(snapshot, "records", json_object_get(f_field(report, "records")));
    status = cache_path(f_string(report, "kind"), f_string(report, "locator"), path) || enrollment_write(path, snapshot);
    json_object_put(snapshot);
    return status ? -1 : 0;
}
static json_object *by_name(json_object *records, const char *name) {
    size_t i;
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i);
        if (!strcmp(f_string(record, "name"), name)) return record;
    }
    return NULL;
}
static json_object *by_target(json_object *records, const char *target) {
    size_t i;
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i);
        if (!strcmp(f_string(record, "target"), target)) return record;
    }
    return NULL;
}
static json_object *change(json_object *record, json_object *prior) {
    json_object *entry = json_object_new_object();
    f_string_add(entry, "name", f_string(record, "name"));
    f_string_add(entry, "previous_target", f_string(prior, "target")); f_string_add(entry, "target", f_string(record, "target"));
    json_object_object_add(entry, "previous_labels", json_object_get(f_field(prior, "labels")));
    json_object_object_add(entry, "labels", json_object_get(f_field(record, "labels")));
    return entry;
}
static void compare(json_object *records, json_object *prior, json_object *added, json_object *changed) {
    size_t i;
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i), *before = by_name(prior, f_string(record, "name")), *holder;
        if (!before) { json_object_array_add(added, json_object_new_string(f_string(record, "name"))); continue; }
        if (strcmp(f_string(before, "target"), f_string(record, "target")) || !json_object_equal(f_field(before, "labels"), f_field(record, "labels")))
            json_object_array_add(changed, change(record, before));
        holder = by_target(prior, f_string(record, "target"));
        if (holder && strcmp(f_string(holder, "name"), f_string(record, "name")) && !f_string(record, "conflict")) f_string_add(record, "conflict", "target_reassigned");
    }
}
static void removed_names(json_object *records, json_object *prior, json_object *removed) {
    size_t i;
    for (i = 0; i < json_object_array_length(prior); i++) {
        const char *name = f_string(json_object_array_get_idx(prior, i), "name");
        if (!by_name(records, name)) json_object_array_add(removed, json_object_new_string(name));
    }
}
static json_object *conflicts(json_object *records) {
    json_object *out = json_object_new_array(); size_t i;
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i), *entry;
        if (!f_string(record, "conflict")) continue;
        entry = json_object_new_object();
        f_string_add(entry, "name", f_string(record, "name")); f_string_add(entry, "target", f_string(record, "target"));
        f_string_add(entry, "reason", f_string(record, "conflict"));
        json_object_array_add(out, entry);
    }
    return out;
}
void hd_live_diff(json_object *report, json_object *previous) {
    json_object *records = f_field(report, "records"), *diff = json_object_new_object();
    json_object *added = json_object_new_array(), *removed = json_object_new_array(), *changed = json_object_new_array();
    json_object *prior = previous ? f_field(previous, "records") : NULL;
    json_object_object_add(report, "cache", previous ? summary(previous) : NULL);
    if (prior) { compare(records, prior, added, changed); removed_names(records, prior, removed); }
    else {
        json_object_object_add(diff, "first_observation", json_object_new_boolean(true));
        for (size_t i = 0; i < json_object_array_length(records); i++)
            json_object_array_add(added, json_object_new_string(f_string(json_object_array_get_idx(records, i), "name")));
    }
    json_object_object_add(diff, "added", added); json_object_object_add(diff, "removed", removed);
    json_object_object_add(diff, "changed", changed); json_object_object_add(diff, "conflicting", conflicts(records));
    json_object_object_add(report, "diff", diff);
}
