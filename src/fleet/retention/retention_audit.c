#define _XOPEN_SOURCE 700
#include "fleet/retention/retention.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int64_t retention_audit_until(const char *directory) {
    char path[F_PATH]; struct stat st;
    if (f_path(path, sizeof(path), directory, "retention-audit.json")) return INT64_MAX;
    if (lstat(path, &st) && errno == ENOENT) return 0;
    json_object *audit = task_read_record(directory, "retention-audit.json");
    json_object *value = f_field(audit, "retain_until");
    const char *id = strrchr(directory, '/'), *stored = f_string(audit, "record_id");
    int64_t deadline = INT64_MAX;
    if (f_number_is(audit, "schema_version", 1) && id && stored && !strcmp(id + 1, stored) &&
        json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0) deadline = json_object_get_int64(value);
    json_object_put(audit); return deadline;
}
int rt_audit_commit(struct rt_inventory *inventory) {
    for (size_t i = 0; i < inventory->count; i++) {
        struct rt_record *record = &inventory->records[i];
        if (record->expired) continue;
        int64_t prior = retention_audit_until(record->path);
        if (record->newest <= 0 || record->newest > INT64_MAX - inventory->audit_seconds) return -1;
        int64_t deadline = record->newest + inventory->audit_seconds;
        if (deadline <= prior) continue;
        json_object *audit = json_object_new_object();
        json_object_object_add(audit, "schema_version", json_object_new_int(1)); f_string_add(audit, "record_id", record->id);
        json_object_object_add(audit, "retain_until", json_object_new_int64(deadline));
        int status = task_write_json(record->path, "retention-audit.json", audit, true); json_object_put(audit);
        if (status) return -1;
    }
    return 0;
}
