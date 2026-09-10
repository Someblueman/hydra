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
static bool snapshot_valid(json_object *snapshot) {
    static const char *const keys[] = {"kind", "locator", "scope", "observed_at", "freshness", "records", NULL};
    json_object *records; size_t i;
    { int64_t observed = json_object_get_int64(f_field(snapshot, "observed_at"));
    if (!hd_keys(snapshot, keys) || !hd_text(f_string(snapshot, "kind"), 64) ||
        (strcmp(f_string(snapshot, "kind"), "mdns") && strcmp(f_string(snapshot, "kind"), "vpn") && strcmp(f_string(snapshot, "kind"), "cloud-tags") && strcmp(f_string(snapshot, "kind"), "config-management")) ||
        !hd_text(f_string(snapshot, "locator"), F_PATH) || !hd_text(f_string(snapshot, "scope"), 256) || !json_object_is_type(f_field(snapshot, "observed_at"), json_type_int) || observed < 0 || observed > (int64_t)time(NULL) ||
        !hd_text(f_string(snapshot, "freshness"), 64) || !(records = f_field(snapshot, "records")) || !json_object_is_type(records, json_type_array) || json_object_array_length(records) > 100) return false;
    }
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i);
        const char *kind = f_string(snapshot, "kind");
        if (f_field(record, "password") || f_field(record, "token") || f_field(record, "secret") || f_field(record, "private_key")) return false;
        if (!strcmp(kind, "mdns")) { static const char *const k[] = {"instance", "host", "port", "labels", NULL}; if (!hd_keys(record, k) || !hd_text(f_string(record, "instance"), 128) || !hd_text(f_string(record, "host"), 256) || !json_object_is_type(f_field(record, "port"), json_type_int) || !labels_valid(f_field(record, "labels"))) return false; }
        else if (!strcmp(kind, "vpn")) { static const char *const k[] = {"peer", "address", "labels", NULL}; if (!hd_keys(record, k) || !hd_text(f_string(record, "peer"), 128) || !target_valid(f_string(record, "address")) || !labels_valid(f_field(record, "labels"))) return false; }
        else if (!strcmp(kind, "cloud-tags")) { static const char *const k[] = {"instance_id", "private_ip", "labels", NULL}; if (!hd_keys(record, k) || !hd_text(f_string(record, "instance_id"), 128) || !target_valid(f_string(record, "private_ip")) || !labels_valid(f_field(record, "labels"))) return false; }
        else { static const char *const k[] = {"host", "address", "labels", NULL}; if (!hd_keys(record, k) || !hd_text(f_string(record, "host"), 128) || !target_valid(f_string(record, "address")) || !labels_valid(f_field(record, "labels"))) return false; }
    }
    return true;
}
static bool inventory_valid(json_object *inventory) {
    static const char *const keys1[] = {"schema_version", "observed_at", "hosts", NULL};
    static const char *const keys2[] = {"schema_version", "observed_at", "hosts", "source_snapshot", NULL};
    json_object *hosts = f_field(inventory, "hosts"), *at = f_field(inventory, "observed_at"); size_t i, j;
    if ((!f_number_is(inventory, "schema_version", 1) && !f_number_is(inventory, "schema_version", 2)) || (f_number_is(inventory, "schema_version", 1) ? !hd_keys(inventory, keys1) : !hd_keys(inventory, keys2)) ||
        !json_object_is_type(at, json_type_int) || json_object_get_int64(at) < 0 ||
        json_object_get_int64(at) > (int64_t)time(NULL) || !json_object_is_type(hosts, json_type_array) ||
        json_object_array_length(hosts) > 1024) return false;
    if (f_number_is(inventory, "schema_version", 2) && !snapshot_valid(f_field(inventory, "source_snapshot"))) return false;
    for (i = 0; i < json_object_array_length(hosts); i++) {
        json_object *record = json_object_array_get_idx(hosts, i);
        if (!record_valid(record)) return false;
        for (j = 0; j < i; j++) if (!strcmp(f_string(record, "name"),
            f_string(json_object_array_get_idx(hosts, j), "name"))) return false;
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
static int add_inventory(json_object *rows, const struct hd_options *options) {
    json_object *inventory, *hosts, *snapshot; const char *kind = "static", *locator = NULL; int64_t observed_at; struct stat st;
    size_t i; int status = -1;
    if (stat(options->inventory, &st) || !S_ISREG(st.st_mode) || st.st_size > 1024 * 1024) return -1;
    inventory = f_read_json(options->inventory, 1024 * 1024);
    if (!inventory_valid(inventory)) goto done;
    observed_at = json_object_get_int64(f_field(inventory, "observed_at"));
    snapshot = f_field(inventory, "source_snapshot");
    hosts = f_field(inventory, "hosts");
    if (snapshot) { kind = f_string(snapshot, "kind"); locator = f_string(snapshot, "locator"); observed_at = json_object_get_int64(f_field(snapshot, "observed_at")); hosts = f_field(snapshot, "records"); }
    for (i = 0; i < options->select_count; i++) {
        json_object *record = selected_record(hosts, options->select[i]), *row; const char *target = NULL, *name = options->select[i];
        if (snapshot) {
            if (!record) { size_t j; for (j = 0; j < json_object_array_length(hosts); j++) { json_object *r = json_object_array_get_idx(hosts, j); const char *candidate = !strcmp(kind, "mdns") ? f_string(r, "instance") : !strcmp(kind, "vpn") ? f_string(r, "peer") : !strcmp(kind, "cloud-tags") ? f_string(r, "instance_id") : f_string(r, "host"); if (candidate && !strcmp(candidate, name)) { record = r; break; } } }
            target = record ? (!strcmp(kind, "mdns") ? f_string(record, "host") : !strcmp(kind, "vpn") ? f_string(record, "address") : !strcmp(kind, "cloud-tags") ? f_string(record, "private_ip") : f_string(record, "address")) : NULL;
        } else target = record ? f_string(record, "target") : NULL;
        if (!record || !target || !(row = candidate_add(rows, target))) goto done;
        source_add(row, kind, locator ? locator : options->inventory, name, observed_at, f_field(record, "labels"));
    }
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
