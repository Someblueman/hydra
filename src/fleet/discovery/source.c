#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static bool target_valid(const char *target) {
    return hd_text(target, 256) && f_target(target);
}
static bool labels_valid(json_object *labels) {
    size_t i;
    if (!json_object_is_type(labels, json_type_array) || json_object_array_length(labels) > 16) return false;
    for (i = 0; i < json_object_array_length(labels); i++)
        if (!hd_text(f_text(json_object_array_get_idx(labels, i)), 128)) return false;
    return true;
}
static bool record_valid(json_object *record) {
    static const char *const keys[] = {"name", "target", "labels", NULL};
    return hd_keys(record, keys) && hd_text(f_string(record, "name"), 128) &&
        target_valid(f_string(record, "target")) && labels_valid(f_field(record, "labels"));
}
static bool timestamp_valid(json_object *value) {
    int64_t observed = json_object_get_int64(value);
    return json_object_is_type(value, json_type_int) && observed >= 0 && observed <= (int64_t)time(NULL);
}
static bool snapshot_kind(const char *kind) {
    return kind && (!strcmp(kind, "mdns") || !strcmp(kind, "vpn") ||
        !strcmp(kind, "cloud-tags") || !strcmp(kind, "config-management"));
}
static const char *snapshot_identifier(json_object *record, const char *kind) {
    if (!strcmp(kind, "mdns")) return f_string(record, "instance");
    if (!strcmp(kind, "vpn")) return f_string(record, "peer");
    if (!strcmp(kind, "cloud-tags")) return f_string(record, "instance_id");
    return f_string(record, "host");
}
static const char *snapshot_target(json_object *record, const char *kind) {
    if (!strcmp(kind, "mdns")) return f_string(record, "host");
    if (!strcmp(kind, "vpn")) return f_string(record, "address");
    if (!strcmp(kind, "cloud-tags")) return f_string(record, "private_ip");
    return f_string(record, "address");
}
static bool snapshot_record_valid(json_object *record, const char *kind) {
    static const char *const mdns[] = {"instance", "host", "port", "labels", NULL};
    static const char *const vpn[] = {"peer", "address", "labels", NULL};
    static const char *const cloud[] = {"instance_id", "private_ip", "labels", NULL};
    static const char *const config[] = {"host", "address", "labels", NULL};
    const char *const *keys = !strcmp(kind, "mdns") ? mdns : !strcmp(kind, "vpn") ? vpn :
        !strcmp(kind, "cloud-tags") ? cloud : config;
    if (!hd_keys(record, keys) || f_field(record, "password") || f_field(record, "token") ||
        f_field(record, "secret") || f_field(record, "private_key") ||
        !hd_text(snapshot_identifier(record, kind), 128) || !target_valid(snapshot_target(record, kind)) ||
        !labels_valid(f_field(record, "labels"))) return false;
    return !strcmp(kind, "mdns") ? json_object_is_type(f_field(record, "port"), json_type_int) &&
        json_object_get_int(f_field(record, "port")) == 22 : true;
}
static bool duplicate_snapshot_id(json_object *records, size_t index, const char *kind) {
    const char *identifier = snapshot_identifier(json_object_array_get_idx(records, index), kind);
    for (size_t j = 0; j < index; j++)
        if (!strcmp(identifier, snapshot_identifier(json_object_array_get_idx(records, j), kind))) return true;
    return false;
}
static bool duplicate_inventory_name(json_object *hosts, size_t index) {
    const char *name = f_string(json_object_array_get_idx(hosts, index), "name");
    for (size_t j = 0; j < index; j++)
        if (!strcmp(name, f_string(json_object_array_get_idx(hosts, j), "name"))) return true;
    return false;
}
static bool snapshot_valid(json_object *snapshot) {
    static const char *const keys[] = {"kind", "locator", "scope", "observed_at", "freshness", "records", NULL};
    const char *kind = f_string(snapshot, "kind"); json_object *records = f_field(snapshot, "records");
    if (!hd_keys(snapshot, keys) || !snapshot_kind(kind) || !hd_text(f_string(snapshot, "locator"), F_PATH) ||
        !hd_text(f_string(snapshot, "scope"), 256) || !timestamp_valid(f_field(snapshot, "observed_at")) ||
        !hd_text(f_string(snapshot, "freshness"), 64) || !json_object_is_type(records, json_type_array) || json_object_array_length(records) > 100) return false;
    for (size_t i = 0; i < json_object_array_length(records); i++)
        if (!snapshot_record_valid(json_object_array_get_idx(records, i), kind) || duplicate_snapshot_id(records, i, kind)) return false;
    return true;
}
static bool inventory_valid(json_object *inventory) {
    static const char *const keys1[] = {"schema_version", "observed_at", "hosts", NULL};
    static const char *const keys2[] = {"schema_version", "observed_at", "hosts", "source_snapshot", NULL};
    json_object *hosts = f_field(inventory, "hosts"), *at = f_field(inventory, "observed_at");
    if ((!f_number_is(inventory, "schema_version", 1) && !f_number_is(inventory, "schema_version", 2)) || (f_number_is(inventory, "schema_version", 1) ? !hd_keys(inventory, keys1) : !hd_keys(inventory, keys2)) ||
        !timestamp_valid(at) || !json_object_is_type(hosts, json_type_array) ||
        json_object_array_length(hosts) > 1024) return false;
    if (f_number_is(inventory, "schema_version", 2) && !snapshot_valid(f_field(inventory, "source_snapshot"))) return false;
    for (size_t i = 0; i < json_object_array_length(hosts); i++) {
        json_object *record = json_object_array_get_idx(hosts, i);
        if (!record_valid(record) || duplicate_inventory_name(hosts, i)) return false;
    }
    return true;
}
static json_object *candidate_find(json_object *rows, const char *target) {
    size_t i;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!strcmp(f_string(row, "target"), target)) return row;
    }
    return NULL;
}
static json_object *candidate_add(json_object *rows, const char *target) {
    json_object *row = candidate_find(rows, target); char id[520];
    if (row) return row;
    if (json_object_array_length(rows) == HD_HOSTS) return NULL;
    row = json_object_new_object(); hd_id(id, target);
    f_string_add(row, "candidate_id", id); f_string_add(row, "target", target);
    f_string_add(row, "deduplication", "exact_target_only");
    json_object_object_add(row, "sources", json_object_new_array());
    json_object_object_add(row, "verified_identity", NULL);
    json_object_array_add(rows, row); return row;
}
static void source_add(json_object *row, const char *kind, const char *locator,
                       const char *name, int64_t observed, json_object *labels) {
    json_object *source = json_object_new_object();
    f_string_add(source, "kind", kind); f_string_add(source, "locator", locator); f_string_add(source, "name", name);
    json_object_object_add(source, "observed_at", json_object_new_int64(observed));
    json_object_object_add(source, "age_seconds", json_object_new_int64((int64_t)time(NULL) - observed));
    f_string_add(source, "freshness", !strcmp(kind, "static") ? "source_reported" : "resolved_now");
    json_object_object_add(source, "labels", labels ? json_object_get(labels) : json_object_new_array());
    json_object_array_add(f_field(row, "sources"), source);
}
static int add_aliases(json_object *rows, const struct hd_options *options) {
    size_t i;
    for (i = 0; i < options->ssh_count; i++) {
        json_object *row;
        if (!target_valid(options->ssh[i]) || !(row = candidate_add(rows, options->ssh[i]))) return -1;
        source_add(row, "ssh-config", options->config ? options->config : "OpenSSH user/system config",
                   options->ssh[i], (int64_t)time(NULL), NULL);
    }
    return 0;
}
static json_object *selected_record(json_object *hosts, const char *name) {
    size_t i;
    for (i = 0; i < json_object_array_length(hosts); i++) {
        json_object *record = json_object_array_get_idx(hosts, i);
        if (!strcmp(f_string(record, "name"), name)) return record;
    }
    return NULL;
}
static json_object *snapshot_record(json_object *hosts, const char *kind, const char *name) {
    for (size_t i = 0; i < json_object_array_length(hosts); i++) {
        json_object *record = json_object_array_get_idx(hosts, i);
        if (!strcmp(snapshot_identifier(record, kind), name)) return record;
    }
    return NULL;
}
struct inventory_view { json_object *hosts, *snapshot; const char *kind, *locator; int64_t observed_at; };
static void inventory_view_init(struct inventory_view *view, json_object *inventory, const struct hd_options *options) {
    view->snapshot = f_field(inventory, "source_snapshot"); view->hosts = f_field(inventory, "hosts");
    view->kind = "static"; view->locator = options->inventory;
    view->observed_at = json_object_get_int64(f_field(inventory, "observed_at"));
    if (view->snapshot) {
        view->kind = f_string(view->snapshot, "kind"); view->locator = f_string(view->snapshot, "locator");
        view->observed_at = json_object_get_int64(f_field(view->snapshot, "observed_at"));
        view->hosts = f_field(view->snapshot, "records");
    }
}
static json_object *inventory_record(struct inventory_view *view, const char *name) {
    return view->snapshot ? snapshot_record(view->hosts, view->kind, name) : selected_record(view->hosts, name);
}
static const char *inventory_target(struct inventory_view *view, json_object *record) {
    return record ? (view->snapshot ? snapshot_target(record, view->kind) : f_string(record, "target")) : NULL;
}
static bool add_inventory_row(json_object *rows, struct inventory_view *view, const char *name) {
    json_object *record = inventory_record(view, name), *row; const char *target = inventory_target(view, record);
    if (!record || !target || !(row = candidate_add(rows, target))) return false;
    source_add(row, view->kind, view->locator, name, view->observed_at, f_field(record, "labels"));
    if (view->snapshot) {
        json_object *source = json_object_array_get_idx(f_field(row, "sources"), json_object_array_length(f_field(row, "sources")) - 1);
        f_string_add(source, "scope", f_string(view->snapshot, "scope")); f_string_add(source, "freshness", f_string(view->snapshot, "freshness"));
    }
    return true;
}
static int add_inventory(json_object *rows, const struct hd_options *options) {
    json_object *inventory; struct inventory_view view; struct stat st; size_t i; int status = -1;
    if (stat(options->inventory, &st) || !S_ISREG(st.st_mode) || st.st_size > 1024 * 1024) return -1;
    inventory = f_read_json(options->inventory, 1024 * 1024);
    if (!inventory_valid(inventory)) goto done;
    inventory_view_init(&view, inventory, options);
    for (i = 0; i < options->select_count; i++) if (!add_inventory_row(rows, &view, options->select[i])) goto done;
    status = 0;
done:
    json_object_put(inventory); return status;
}
static int candidate_order(const void *left, const void *right) {
    return strcmp(f_string(*(json_object *const *)left, "target"), f_string(*(json_object *const *)right, "target"));
}
static int source_order(const void *left, const void *right) {
    json_object *a = *(json_object *const *)left, *b = *(json_object *const *)right;
    int order = strcmp(f_string(a, "kind"), f_string(b, "kind"));
    if (!order) order = strcmp(f_string(a, "locator"), f_string(b, "locator"));
    return order ? order : strcmp(f_string(a, "name"), f_string(b, "name"));
}
json_object *hd_sources(const struct hd_options *options) {
    json_object *rows = json_object_new_array(); size_t i;
    if (add_aliases(rows, options) || (options->inventory && add_inventory(rows, options))) {
        json_object_put(rows); return NULL;
    }
    json_object_array_sort(rows, candidate_order);
    for (i = 0; i < json_object_array_length(rows); i++)
        json_object_array_sort(f_field(json_object_array_get_idx(rows, i), "sources"), source_order);
    return rows;
}
