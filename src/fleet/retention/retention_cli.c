#define _XOPEN_SOURCE 700
#include "fleet/retention/retention.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include "fleet/plan/plan.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool integer(json_object *policy, const char *key, int64_t minimum, int64_t maximum, int64_t *out) {
    json_object *field = f_field(policy, key);
    if (!json_object_is_type(field, json_type_int)) return false;
    *out = json_object_get_int64(field); return *out >= minimum && *out <= maximum;
}
static bool policy_read(struct rt_inventory *inventory, const char *path) {
    const char *const keys[] = {"schema_version", "audit_days", "max_bytes", "max_evidence_records", NULL};
    struct stat st;
    json_object *policy = !stat(path, &st) && st.st_size >= 0 && st.st_size <= 4096 ? plan_read(path) : NULL; int64_t days = 0;
    bool valid = task_keys(policy, keys) && f_number_is(policy, "schema_version", 1) &&
        integer(policy, "audit_days", 1, 3650, &days) && integer(policy, "max_bytes", 4096, INT64_C(1099511627776), &inventory->max_bytes) &&
        integer(policy, "max_evidence_records", 0, RT_RECORDS, &inventory->max_evidence_records);
    inventory->audit_seconds = days * 86400; json_object_put(policy); return valid;
}
static int64_t retained_bytes(struct rt_record *record) {
    /* Reserve the complete audit receipt even when extending an existing one. */
    if (!record->selected) return record->bytes + (record->expired ? 0 : 512);
    int64_t predicted = record->bytes - record->payload_bytes;
    if (record->expired) return predicted;
    predicted += 2048;
    for (size_t j = 0; j < json_object_array_length(record->files); j++)
        predicted += (int64_t)strlen(f_string(json_object_array_get_idx(record->files, j), "path")) * 2 + 192;
    return predicted;
}
static json_object *record_view(struct rt_record *record) {
    json_object *item = json_object_new_object();
    const char *reason = record->expired ? "previous_expiry" : "audit_window_elapsed";
    f_string_add(item, "kind", record->task ? "task" : "run"); f_string_add(item, "id", record->id); f_string_add(item, "project_id", record->project);
    f_string_add(item, "action", record->selected ? "expire" : "preserve");
    f_string_add(item, "reason", record->reason ? record->reason : reason);
    json_object_object_add(item, "bytes", json_object_new_int64(record->bytes));
    json_object_object_add(item, "expirable_bytes", json_object_new_int64(record->payload_bytes));
    return item;
}
static json_object *projection(struct rt_inventory *inventory, bool *fits) {
    json_object *out = json_object_new_object(), *records = json_object_new_array();
    int64_t before = 0, after = 0, retained = 0;
    json_object_object_add(out, "schema_version", json_object_new_int(1));
    f_string_add(out, "scope", "local_receiver_and_workflow_evidence; workspaces_and_git_worktrees_excluded");
    json_object_object_add(out, "records", records);
    for (size_t i = 0; i < inventory->count; i++) {
        struct rt_record *record = &inventory->records[i];
        record->selected = !record->protected && (record->expired || json_object_array_length(record->files));
        if (!record->selected && !record->expired) retained++;
        before += record->bytes; after += retained_bytes(record);
        json_object_array_add(records, record_view(record));
    }
    *fits = after <= inventory->max_bytes && retained <= inventory->max_evidence_records;
    json_object_object_add(out, "bytes_before", json_object_new_int64(before));
    json_object_object_add(out, "bytes_after_upper_bound", json_object_new_int64(after));
    json_object_object_add(out, "retained_evidence_records", json_object_new_int64(retained));
    json_object_object_add(out, "capacity_satisfied", json_object_new_boolean(*fits));
    return out;
}
static int record_index(struct rt_inventory *inventory, const char *kind, const char *id) {
    int chosen = -1;
    for (size_t i = 0; i < inventory->count; i++) {
        struct rt_record *record = &inventory->records[i];
        if (record->task == !strcmp(kind, "task") && !strcmp(record->id, id)) {
            if (chosen >= 0) return -2;
            chosen = (int)i;
        }
    }
    return chosen;
}
static int write_pin(struct rt_inventory *inventory, struct rt_record *record, bool pinned) {
    if (!pinned) {
        char path[F_PATH];
        if (f_path(path, sizeof(path), record->path, "retention-pin.json") || (unlink(path) && errno != ENOENT)) return -1;
        return task_sync_dir(record->path);
    }
    json_object *pin = json_object_new_object(); json_object_object_add(pin, "schema_version", json_object_new_int(1));
    f_string_add(pin, "record_id", record->id); json_object_object_add(pin, "pinned_at", json_object_new_int64(inventory->now));
    int status = task_write_json(record->path, "retention-pin.json", pin, true); json_object_put(pin); return status;
}
static json_object *pin_record(struct rt_inventory *inventory, const char *action, const char *kind, const char *id) {
    int index = record_index(inventory, kind, id);
    if (index == -2) return f_error("fleet retention", "ambiguous_id", "record identity matches multiple projects");
    if (index < 0) return f_error("fleet retention", "record_not_found", "no matching local record was found");
    struct rt_record *chosen = &inventory->records[index];
    if (chosen->expired) return f_error("fleet retention", "evidence_expired", "expired evidence cannot be restored by pinning it");
    if (write_pin(inventory, chosen, !strcmp(action, "pin"))) return f_error("fleet retention", "io_failed", "cannot durably update evidence pin");
    json_object *out = json_object_new_object(); f_string_add(out, "id", id); f_string_add(out, "action", action);
    return f_success("fleet retention", out);
}
static int retention_lock(void) {
    char root[F_PATH], path[F_PATH]; struct stat st;
    if (task_store_root(root) || snprintf(path, sizeof(path), "%s/../retention.lock", root) >= (int)sizeof(path)) return -1;
    int lock = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lock < 0 || fstat(lock, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022) || flock(lock, LOCK_EX | LOCK_NB)) {
        if (lock >= 0) close(lock);
        return -1;
    }
    return lock;
}
static int apply_expiry(struct rt_inventory *inventory) {
    for (size_t i = 0; i < inventory->count; i++)
        if (inventory->records[i].selected && rt_expire(inventory, &inventory->records[i])) return -1;
    return 0;
}
static json_object *expire_command(struct rt_inventory *inventory, bool apply) {
    bool fits; json_object *out = projection(inventory, &fits), *result;
    json_object_object_add(out, "applied", json_object_new_boolean(false));
    if (apply && fits) {
        if (rt_audit_commit(inventory) || apply_expiry(inventory)) {
            json_object_put(out); return f_error("fleet retention", "expiry_incomplete", "expiry stopped with inspectable receipts; rerun the same policy to finish, never replay execution");
        }
        json_object_object_add(out, "applied", json_object_new_boolean(true));
    }
    result = f_success("fleet retention", out);
    if (apply && !fits) {
        json_object_object_add(result, "ok", json_object_new_boolean(false));
        json_object *error = json_object_new_object(); f_string_add(error, "code", "protected_capacity_exceeded");
        f_string_add(error, "message", "protected evidence and retained identity receipts exceed the quota; nothing was expired"); json_object_object_add(result, "error", error);
    }
    return result;
}
static bool options(struct rt_inventory *inventory, int argc, char **argv, bool *pin) {
    if (argc != 3) return false;
    *pin = !strcmp(argv[0], "pin") || !strcmp(argv[0], "unpin");
    if (*pin) return (!strcmp(argv[1], "task") || !strcmp(argv[1], "run")) && f_name(argv[2]);
    return (!strcmp(argv[0], "apply") || !strcmp(argv[0], "preview")) && !strcmp(argv[1], "--policy") && policy_read(inventory, argv[2]);
}
json_object *retention_cli(int argc, char **argv) {
    struct rt_inventory inventory = {.now = (int64_t)time(NULL)}; json_object *result; bool pin = false;
    if (!argc || !strcmp(argv[0], "help")) {
        json_object *out = json_object_new_object(); f_string_add(out, "usage", "fleet retention preview|apply --policy FILE; fleet retention pin|unpin task|run ID; affects this local HYDRA_HOME only");
        return f_success("fleet retention", out);
    }
    if (!options(&inventory, argc, argv, &pin)) return f_error("fleet retention", "invalid_input", "use preview/apply --policy with schema_version1, audit_days1-3650, max_bytes4096-1TiB and max_evidence_records0-1024; or pin/unpin task/run ID");
    int lock = retention_lock();
    if (lock < 0) return f_error("fleet retention", "retention_busy", "another retention operation owns storage or its lock is invalid");
    inventory.records = calloc(RT_RECORDS, sizeof(*inventory.records));
    if (!inventory.records || rt_scan(&inventory)) result = f_error("fleet retention", "incomplete_inventory", "private link-free bounded inventory could not be established; no evidence was expired");
    else if (pin) result = pin_record(&inventory, argv[0], argv[1], argv[2]);
    else result = expire_command(&inventory, !strcmp(argv[0], "apply"));
    rt_release(&inventory); close(lock); return result;
}
