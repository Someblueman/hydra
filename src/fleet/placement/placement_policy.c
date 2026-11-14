#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Policy version 1 knobs, all documented in docs/FLEET.md. Defaults are
 * conservative: no validator headroom is reserved and queues stay bounded. */
static const struct { const char *key; int64_t value, min, max; } knobs[] = {
    {"max_observation_age_seconds", 60, 1, 86400},
    {"max_queued", 8, 0, 128},
    {"validator_reserve_slots", 0, 0, 64},
};
json_object *placement_policy_defaults(void) {
    json_object *policy = json_object_new_object(); size_t i;
    json_object_object_add(policy, "policy_version", json_object_new_int(PLACEMENT_POLICY_VERSION));
    for (i = 0; i < sizeof(knobs) / sizeof(knobs[0]); i++) json_object_object_add(policy, knobs[i].key, json_object_new_int64(knobs[i].value));
    return policy;
}
json_object *placement_policy_load(void) {
    const char *const keys[] = {"policy_version", "max_observation_age_seconds", "max_queued", "validator_reserve_slots", NULL};
    char path[F_PATH]; json_object *file, *policy = placement_policy_defaults(); size_t i;
    if (f_path(path, sizeof(path), f_home, "fleet/placement-policy.json")) { json_object_put(policy); return NULL; }
    if (access(path, F_OK)) return policy;
    file = f_read_json(path, 16384);
    if (!task_keys(file, keys) || (f_field(file, "policy_version") && !f_number_is(file, "policy_version", PLACEMENT_POLICY_VERSION))) goto bad;
    for (i = 0; i < sizeof(knobs) / sizeof(knobs[0]); i++) {
        json_object *value = f_field(file, knobs[i].key);
        if (!value) continue;
        if (!json_object_is_type(value, json_type_int) || json_object_get_int64(value) < knobs[i].min || json_object_get_int64(value) > knobs[i].max) goto bad;
        json_object_object_add(policy, knobs[i].key, json_object_new_int64(json_object_get_int64(value)));
    }
    json_object_put(file); return policy;
bad:
    json_object_put(file); json_object_put(policy); return NULL;
}
/* Capability conventions understood by placement: label.X is a receiver
 * admission label, role.producer/role.validator the task class, and
 * platform.OS-ARCH a required receiver platform. Other names are adapter
 * capabilities the receiver must advertise. */
json_object *placement_inputs(json_object *spec) {
    json_object *inputs = json_object_new_object(), *caps = f_field(spec, "capabilities");
    json_object *labels = json_object_new_array(), *adapters = json_object_new_array(); const char *class_name = "general", *platform = NULL;
    char joined[2049] = ""; size_t i, used = 0;
    f_string_add(inputs, "project", f_string(spec, "project"));
    f_string_add(inputs, "work_kind", f_string(f_field(spec, "work"), "kind"));
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *name = f_text(json_object_array_get_idx(caps, i));
        if (!strncmp(name, "label.", 6)) {
            int n = snprintf(joined + used, sizeof(joined) - used, "%s%s", used ? "," : "", name + 6);
            if (n > 0 && (size_t)n < sizeof(joined) - used) used += (size_t)n;
            json_object_array_add(labels, json_object_new_string(name + 6));
        } else if (!strcmp(name, "role.validator")) class_name = "validator";
        else if (!strcmp(name, "role.producer")) class_name = "producer";
        else if (!strncmp(name, "platform.", 9)) platform = name + 9;
        else if (!strncmp(name, "role.", 5)) continue;
        else if (strcmp(name, "exec") && strcmp(name, "git")) json_object_array_add(adapters, json_object_new_string(name));
    }
    f_string_add(inputs, "task_class", class_name);
    json_object_object_add(inputs, "required_labels", labels);
    f_string_add(inputs, "required_labels_key", used ? joined : "-");
    json_object_object_add(inputs, "adapter_capabilities", adapters);
    if (platform) f_string_add(inputs, "platform", platform); else json_object_object_add(inputs, "platform", json_object_new_null());
    json_object_object_add(inputs, "disk_bytes_required", json_object_new_int64(
        json_object_get_int64(f_field(f_field(spec, "limits"), "artifact_bytes")) + json_object_get_int64(f_field(f_field(spec, "limits"), "log_bytes"))));
    return inputs;
}
json_object *placement_estimates(void) {
    json_object *estimates = json_object_new_object(); const char *hooks[] = {"queue_delay_seconds", "measured_duration_seconds", "transfer_cost_bytes", NULL}; size_t i;
    json_object_object_add(estimates, "schema_version", json_object_new_int(PLACEMENT_ESTIMATES_SCHEMA));
    for (i = 0; hooks[i]; i++) json_object_object_add(estimates, hooks[i], json_object_new_null());
    json_object_object_add(estimates, "used_in_ranking", json_object_new_boolean(false));
    return estimates;
}
static bool in_list(json_object *array, const char *value) {
    size_t i;
    for (i = 0; value && json_object_is_type(array, json_type_array) && i < json_object_array_length(array); i++) {
        const char *item = f_text(json_object_array_get_idx(array, i));
        if (item && !strcmp(item, value)) return true;
    }
    return false;
}
static bool label_supported(const char *host_labels, const char *label) {
    char haystack[2100], needle[200];
    if (!host_labels || !label || snprintf(haystack, sizeof(haystack), ",%s,", host_labels) >= (int)sizeof(haystack) ||
        snprintf(needle, sizeof(needle), ",%s,", label) >= (int)sizeof(needle)) return false;
    return strstr(haystack, needle) != NULL;
}
static void reason(json_object *reasons, const char *code) { json_object_array_add(reasons, json_object_new_string(code)); }
static bool admission_valid(json_object *admission) {
    const char *numbers[] = {"host_limit", "project_limit", "disk_floor_kb", "disk_free_kb", "queue_limit", "reserved", "queued", "observed_at", "class_reserved", NULL}; size_t i;
    if (!json_object_is_type(admission, json_type_object) || !f_string(admission, "labels")) return false;
    for (i = 0; numbers[i]; i++) {
        json_object *value = f_field(admission, numbers[i]);
        if (!json_object_is_type(value, json_type_int) || json_object_get_int64(value) < 0) return false;
    }
    return true;
}
/* Static eligibility: trust, protocol, adapters, project mapping, platform. */
static void filter_host(json_object *inputs, json_object *observation, json_object *reasons) {
    json_object *adapters = f_field(inputs, "adapter_capabilities"), *caps = f_field(observation, "capabilities"), *host_platform = f_field(observation, "platform");
    const char *platform = f_string(inputs, "platform"); bool missing; size_t i; char expected[160];
    if (!json_object_get_boolean(f_field(observation, "compatible"))) reason(reasons, "version_incompatible");
    missing = !in_list(caps, "task-accept") || !in_list(caps, "task-withdraw");
    for (i = 0; json_object_is_type(adapters, json_type_array) && i < json_object_array_length(adapters); i++)
        if (!in_list(caps, f_text(json_object_array_get_idx(adapters, i)))) missing = true;
    if (missing) reason(reasons, "capability_missing");
    if (!in_list(f_field(observation, "projects"), f_string(inputs, "project"))) reason(reasons, "project_unmapped");
    if (!platform) return;
    snprintf(expected, sizeof(expected), "%s-%s", f_string(host_platform, "os") ? f_string(host_platform, "os") : "?", f_string(host_platform, "arch") ? f_string(host_platform, "arch") : "?");
    if (strcmp(expected, platform)) reason(reasons, "platform_mismatch");
}
struct load { int64_t capacity, reserved, queued, class_reserved, age; bool observed; };
/* Capacity eligibility from the receiver's admission observation. */
static void filter_admission(json_object *inputs, json_object *admission, json_object *policy, int64_t now, json_object *reasons, struct load *load) {
    json_object *labels = f_field(inputs, "required_labels"); const char *class_name = f_string(inputs, "task_class");
    int64_t host_limit = json_object_get_int64(f_field(admission, "host_limit")), free_slots, age_limit = json_object_get_int64(f_field(policy, "max_observation_age_seconds"));
    int64_t disk_free = json_object_get_int64(f_field(admission, "disk_free_kb")); size_t i;
    load->observed = true; load->age = now - json_object_get_int64(f_field(admission, "observed_at"));
    load->reserved = json_object_get_int64(f_field(admission, "reserved")); load->queued = json_object_get_int64(f_field(admission, "queued"));
    load->class_reserved = json_object_get_int64(f_field(admission, "class_reserved"));
    load->capacity = host_limit > 0 ? host_limit : 1;
    for (i = 0; i < json_object_array_length(labels); i++)
        if (!label_supported(f_string(admission, "labels"), f_text(json_object_array_get_idx(labels, i)))) { reason(reasons, "label_unsupported"); break; }
    if (load->age > age_limit || load->age < -age_limit) reason(reasons, "stale_observation");
    if (load->queued >= json_object_get_int64(f_field(admission, "queue_limit"))) reason(reasons, "queue_full");
    else if (load->queued > json_object_get_int64(f_field(policy, "max_queued"))) reason(reasons, "queue_bound");
    if (disk_free < json_object_get_int64(f_field(admission, "disk_floor_kb"))) reason(reasons, "disk_floor");
    if (disk_free * 1024 < json_object_get_int64(f_field(inputs, "disk_bytes_required"))) reason(reasons, "disk_insufficient");
    free_slots = host_limit > 0 ? host_limit - load->reserved : 1;
    if (host_limit > 0 && class_name && strcmp(class_name, "validator") && free_slots <= json_object_get_int64(f_field(policy, "validator_reserve_slots")))
        reason(reasons, "validator_reserved");
}
static json_object *score_object(const struct load *load) {
    json_object *score = json_object_new_object();
    json_object_object_add(score, "capacity", json_object_new_int64(load->capacity));
    json_object_object_add(score, "reserved", json_object_new_int64(load->reserved));
    json_object_object_add(score, "queued", json_object_new_int64(load->queued));
    json_object_object_add(score, "class_reserved", json_object_new_int64(load->class_reserved));
    json_object_object_add(score, "load_milli", json_object_new_int64((load->reserved + load->queued) * 1000 / load->capacity));
    return score;
}
/* Filters one observation. Every applicable exclusion is recorded. */
static json_object *candidate(json_object *inputs, json_object *observation, json_object *policy, json_object *excluded, json_object *separation, int64_t now) {
    json_object *out = json_object_new_object(), *reasons = json_object_new_array(), *admission = f_field(observation, "admission");
    const char *host = f_string(observation, "host"); struct load load = {1, 0, 0, 0, 0, false};
    f_string_add(out, "host", host);
    if (in_list(excluded, host)) reason(reasons, "previous_generation");
    if (in_list(separation, host)) reason(reasons, "validator_separation");
    if (json_object_get_boolean(f_field(observation, "drained"))) reason(reasons, "drained");
    if (f_string(observation, "error")) reason(reasons, f_string(observation, "error"));
    else {
        filter_host(inputs, observation, reasons);
        if (!admission_valid(admission)) reason(reasons, "admission_unobserved");
        else filter_admission(inputs, admission, policy, now, reasons, &load);
    }
    json_object_object_add(out, "eligible", json_object_new_boolean(json_object_array_length(reasons) == 0));
    json_object_object_add(out, "reasons", reasons);
    json_object_object_add(out, "observation_age_seconds", load.observed ? json_object_new_int64(load.age) : json_object_new_null());
    json_object_object_add(out, "score", score_object(&load));
    json_object_object_add(out, "estimates", placement_estimates());
    json_object_object_add(out, "rank", json_object_new_null());
    return out;
}
static int64_t score_of(json_object *candidate_entry, const char *key) { return json_object_get_int64(f_field(f_field(candidate_entry, "score"), key)); }
/* Ascending: normalized load, comparable-class reservations, queue depth, alias. */
static int better(json_object *a, json_object *b) {
    const char *keys[] = {"load_milli", "class_reserved", "queued", NULL}; size_t i;
    for (i = 0; keys[i]; i++) {
        int64_t x = score_of(a, keys[i]), y = score_of(b, keys[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return strcmp(f_string(a, "host"), f_string(b, "host"));
}
json_object *placement_decide(json_object *inputs, json_object *observations, json_object *policy,
                              json_object *excluded, json_object *separation, int64_t now, json_object **decision) {
    json_object *candidates = json_object_new_array(), *eligible[PLACEMENT_HOSTS]; size_t i, j, count = 0;
    *decision = json_object_new_object();
    if (!json_object_is_type(observations, json_type_array) || json_object_array_length(observations) > PLACEMENT_HOSTS ||
        !json_object_is_type(inputs, json_type_object) || !json_object_is_type(policy, json_type_object)) {
        json_object_object_add(*decision, "host", json_object_new_null()); f_string_add(*decision, "reason", "invalid_inputs"); return candidates;
    }
    for (i = 0; i < json_object_array_length(observations); i++) {
        json_object *entry = candidate(inputs, json_object_array_get_idx(observations, i), policy, excluded, separation, now);
        json_object_array_add(candidates, entry);
        if (json_object_get_boolean(f_field(entry, "eligible"))) eligible[count++] = entry;
    }
    for (i = 1; i < count; i++) {
        json_object *key = eligible[i];
        for (j = i; j > 0 && better(key, eligible[j - 1]) < 0; j--) eligible[j] = eligible[j - 1];
        eligible[j] = key;
    }
    for (i = 0; i < count; i++) json_object_object_add(eligible[i], "rank", json_object_new_int64((int64_t)i + 1));
    if (count) { f_string_add(*decision, "host", f_string(eligible[0], "host")); f_string_add(*decision, "reason", "ranked"); }
    else { json_object_object_add(*decision, "host", json_object_new_null()); f_string_add(*decision, "reason", "no_eligible_host"); }
    json_object_object_add(*decision, "policy_version", json_object_new_int(PLACEMENT_POLICY_VERSION));
    return candidates;
}
