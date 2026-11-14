#include "fleet/discovery/discovery.h"
#include "fleet/support/json.h"
#include <string.h>
#include <time.h>

/* One acquisition report. Records are {name, target, labels} plus a
 * `conflict` reason when identity is ambiguous; skipped entries name the
 * record and a typed reason only, never the rejected value or a credential. */
json_object *hd_report(const char *status, const char *reason) {
    json_object *report = json_object_new_object();
    f_string_add(report, "status", status);
    if (reason) f_string_add(report, "reason", reason);
    json_object_object_add(report, "records", json_object_new_array());
    json_object_object_add(report, "skipped", json_object_new_array());
    json_object_object_add(report, "credentials_omitted", json_object_new_int(0));
    json_object_object_add(report, "observed_at", json_object_new_int64((int64_t)time(NULL)));
    return report;
}
void hd_skip(json_object *report, const char *name, const char *reason) {
    json_object *skipped = f_field(report, "skipped"), *entry;
    if (json_object_array_length(skipped) >= HD_SOURCE_RECORDS) {
        json_object_object_add(report, "skipped_truncated", json_object_new_boolean(true)); return;
    }
    entry = json_object_new_object();
    if (name && hd_text(name, 128)) f_string_add(entry, "name", name);
    f_string_add(entry, "reason", reason);
    json_object_array_add(skipped, entry);
}
static bool label_present(json_object *labels, const char *label) {
    size_t i;
    for (i = 0; i < json_object_array_length(labels); i++)
        if (!strcmp(f_text(json_object_array_get_idx(labels, i)), label)) return true;
    return false;
}
static void merge_labels(json_object *into, json_object *labels) {
    size_t i;
    for (i = 0; labels && i < json_object_array_length(labels); i++) {
        const char *label = f_text(json_object_array_get_idx(labels, i));
        if (!label || !hd_text(label, 128) || label_present(into, label) || json_object_array_length(into) >= 16) continue;
        json_object_array_add(into, json_object_new_string(label));
    }
}
static void conflict(json_object *record, const char *reason) {
    if (!f_string(record, "conflict")) f_string_add(record, "conflict", reason);
}
void hd_record_add(json_object *report, const char *name, const char *target, json_object *labels) {
    json_object *records = f_field(report, "records"), *record; size_t i;
    if (!hd_text(name, 128) || !hd_text(target, 256)) { hd_skip(report, name, "invalid_record"); return; }
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *other = json_object_array_get_idx(records, i);
        bool same_name = !strcmp(f_string(other, "name"), name), same_target = !strcmp(f_string(other, "target"), target);
        if (same_name && same_target) { merge_labels(f_field(other, "labels"), labels); return; }
        if (same_name) conflict(other, "duplicate_name");
        else if (same_target) conflict(other, "duplicate_target");
        else continue;
        record = json_object_new_object();
        f_string_add(record, "name", name); f_string_add(record, "target", target);
        json_object_object_add(record, "labels", json_object_new_array()); merge_labels(f_field(record, "labels"), labels);
        conflict(record, same_name ? "duplicate_name" : "duplicate_target");
        if (json_object_array_length(records) < HD_SOURCE_RECORDS) json_object_array_add(records, record);
        else { json_object_put(record); json_object_object_add(report, "truncated", json_object_new_boolean(true)); }
        return;
    }
    if (json_object_array_length(records) >= HD_SOURCE_RECORDS) {
        json_object_object_add(report, "truncated", json_object_new_boolean(true)); hd_skip(report, name, "record_limit"); return;
    }
    record = json_object_new_object();
    f_string_add(record, "name", name); f_string_add(record, "target", target);
    json_object_object_add(record, "labels", json_object_new_array()); merge_labels(f_field(record, "labels"), labels);
    json_object_array_add(records, record);
}
