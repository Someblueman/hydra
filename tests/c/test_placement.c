#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/fleet.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
const char *f_home, *f_hydra;

#define CAPS "[\"list\",\"admission\",\"task-accept\",\"task-start\",\"task-withdraw\",\"workflow\",\"agent-headless\"]"
#define PLATFORM "{\"os\":\"linux\",\"arch\":\"x86_64\"}"
#define PROJECTS "[\"/srv/project\"]"
static const char *spec_text =
    "{\"schema_version\":1,\"host\":\"alpha\",\"project\":\"/srv/project\",\"source\":{\"commit\":\"0000000000000000000000000000000000000000\"},"
    "\"work\":{\"kind\":\"exec\",\"argv\":[\"true\"]},\"inputs\":[],\"outputs\":[],\"capabilities\":[\"exec\",\"label.gpu\",\"role.validator\",\"agent-headless\"],"
    "\"completion\":\"command-exit\",\"limits\":{\"transport_seconds\":30,\"queue_seconds\":60,\"startup_seconds\":60,\"execution_seconds\":120,"
    "\"cancellation_seconds\":10,\"log_bytes\":4096,\"artifact_bytes\":4096}}";
/* One healthy observation; admission values are substituted per host. */
static json_object *observation(const char *host, const char *admission, const char *error, bool drained) {
    char text[4096];
    snprintf(text, sizeof(text), "{\"host\":\"%s\",\"observed_at\":1000,\"drained\":%s,\"compatible\":true,\"error\":%s%s%s,\"hydra_version\":\"2.9.0\","
             "\"capabilities\":" CAPS ",\"projects\":" PROJECTS ",\"platform\":" PLATFORM ",\"admission\":%s}",
             host, drained ? "true" : "false", error ? "\"" : "", error ? error : "null", error ? "\"" : "", admission);
    json_object *out = f_parse(text); assert(out); return out;
}
static const char *admission(int limit, int reserved, int queued, int class_reserved, int queue_limit, const char *labels, long observed_at) {
    static char text[512];
    snprintf(text, sizeof(text), "{\"host_limit\":%d,\"project_limit\":%d,\"disk_floor_kb\":0,\"disk_free_kb\":1000000,\"queue_limit\":%d,\"reserved\":%d,\"queued\":%d,"
             "\"observed_at\":%ld,\"labels\":\"%s\",\"class_reserved\":%d}", limit, limit, queue_limit, reserved, queued, observed_at, labels, class_reserved);
    return text;
}
static json_object *candidate_named(json_object *candidates, const char *host) {
    size_t i;
    for (i = 0; i < json_object_array_length(candidates); i++)
        if (!strcmp(f_string(json_object_array_get_idx(candidates, i), "host"), host)) return json_object_array_get_idx(candidates, i);
    assert(0); return NULL;
}
static bool has_reason(json_object *candidate, const char *reason) {
    json_object *reasons = f_field(candidate, "reasons"); size_t i;
    for (i = 0; i < json_object_array_length(reasons); i++) if (!strcmp(f_text(json_object_array_get_idx(reasons, i)), reason)) return true;
    return false;
}
static int rank_of(json_object *candidates, const char *host) { return json_object_get_int(f_field(candidate_named(candidates, host), "rank")); }

static void filtering_and_ranking(json_object *inputs, json_object *policy) {
    json_object *observations = json_object_new_array(), *excluded = f_parse_value("[\"kilo\"]"), *separation = f_parse_value("[\"lima\"]"), *decision = NULL, *candidates, *again, *again_decision = NULL;
    json_object_array_add(observations, observation("alpha", admission(4, 2, 0, 0, 8, "gpu,linux", 1000), NULL, false));
    json_object_array_add(observations, observation("bravo", admission(2, 1, 0, 1, 8, "gpu", 1000), NULL, false));
    json_object_array_add(observations, observation("charlie", admission(0, 0, 0, 0, 8, "gpu", 1000), NULL, false));
    json_object_array_add(observations, observation("delta", admission(4, 2, 0, 0, 8, "gpu", 1000), NULL, false));
    json_object_array_add(observations, observation("echo", admission(4, 0, 0, 0, 8, "gpu", 100), NULL, false));
    json_object_array_add(observations, observation("foxtrot", "null", "offline", false));
    json_object_array_add(observations, observation("india", admission(4, 0, 8, 0, 8, "linux", 1000), NULL, false));
    json_object_array_add(observations, observation("juliet", admission(4, 0, 0, 0, 8, "gpu", 1000), NULL, true));
    json_object_array_add(observations, observation("kilo", admission(4, 0, 0, 0, 8, "gpu", 1000), NULL, false));
    json_object_array_add(observations, observation("lima", admission(4, 0, 0, 0, 8, "gpu", 1000), NULL, false));
    json_object_array_add(observations, observation("mike", admission(4, 0, 3, 0, 8, "gpu", 1000), NULL, false));
    {
        json_object *golf = observation("golf", admission(4, 0, 0, 0, 8, "gpu", 1000), NULL, false);
        json_object_object_add(golf, "projects", json_object_new_array()); json_object_object_add(golf, "capabilities", f_parse_value("[\"task-accept\"]"));
        json_object_array_add(observations, golf);
        json_object *hotel = observation("hotel", admission(4, 0, 0, 0, 8, "gpu", 1000), NULL, false);
        json_object_object_add(hotel, "platform", f_parse("{\"os\":\"darwin\",\"arch\":\"aarch64\"}"));
        json_object_array_add(observations, hotel);
        json_object *november = observation("november", admission(4, 0, 0, 0, 8, "gpu", 1000), NULL, false);
        json_object_object_add(november, "compatible", json_object_new_boolean(false));
        json_object_array_add(observations, november);
    }
    candidates = placement_decide(inputs, observations, policy, excluded, separation, 1010, &decision);
    assert(!strcmp(f_string(decision, "host"), "charlie") && !strcmp(f_string(decision, "reason"), "ranked") && f_number_is(decision, "policy_version", 1));
    /* Unknown capacity counts as one slot; normalized load, comparable class, queue, alias. */
    assert(rank_of(candidates, "charlie") == 1 && rank_of(candidates, "alpha") == 2 && rank_of(candidates, "delta") == 3 && rank_of(candidates, "bravo") == 4 && rank_of(candidates, "mike") == 5);
    assert(f_number_is(f_field(candidate_named(candidates, "alpha"), "score"), "load_milli", 500));
    assert(f_number_is(f_field(candidate_named(candidates, "charlie"), "score"), "capacity", 1));
    assert(has_reason(candidate_named(candidates, "echo"), "stale_observation") && f_number_is(candidate_named(candidates, "echo"), "observation_age_seconds", 910));
    assert(has_reason(candidate_named(candidates, "foxtrot"), "offline") && !json_object_get_boolean(f_field(candidate_named(candidates, "foxtrot"), "eligible")));
    assert(has_reason(candidate_named(candidates, "golf"), "project_unmapped") && has_reason(candidate_named(candidates, "golf"), "capability_missing"));
    assert(has_reason(candidate_named(candidates, "hotel"), "platform_mismatch"));
    assert(has_reason(candidate_named(candidates, "india"), "label_unsupported") && has_reason(candidate_named(candidates, "india"), "queue_full"));
    assert(has_reason(candidate_named(candidates, "juliet"), "drained"));
    assert(has_reason(candidate_named(candidates, "kilo"), "previous_generation"));
    assert(has_reason(candidate_named(candidates, "lima"), "validator_separation"));
    assert(has_reason(candidate_named(candidates, "november"), "version_incompatible"));
    assert(json_object_is_type(f_field(candidate_named(candidates, "foxtrot"), "rank"), json_type_null));
    assert(f_number_is(f_field(candidate_named(candidates, "alpha"), "estimates"), "schema_version", 1) && !json_object_get_boolean(f_field(f_field(candidate_named(candidates, "alpha"), "estimates"), "used_in_ranking")));
    /* Replay: the same observations and policy give byte-identical output. */
    again = placement_decide(inputs, observations, policy, excluded, separation, 1010, &again_decision);
    assert(json_object_equal(again, candidates) && json_object_equal(again_decision, decision));
    json_object_put(again); json_object_put(again_decision);
    /* A changed policy knob changes the outcome, so replay detects it. */
    json_object_object_add(policy, "max_queued", json_object_new_int64(2));
    again = placement_decide(inputs, observations, policy, excluded, separation, 1010, &again_decision);
    assert(has_reason(candidate_named(again, "mike"), "queue_bound") && !json_object_equal(again, candidates));
    json_object_put(again); json_object_put(again_decision);
    json_object_object_add(policy, "max_queued", json_object_new_int64(8));
    json_object_put(candidates); json_object_put(decision); json_object_put(observations); json_object_put(excluded); json_object_put(separation);
}
static void validator_headroom(json_object *validator_inputs, json_object *policy) {
    json_object *observations = json_object_new_array(), *producer_inputs = f_parse(json_object_to_json_string_ext(validator_inputs, JSON_C_TO_STRING_PLAIN)), *decision = NULL, *candidates;
    json_object *none = json_object_new_array();
    f_string_add(producer_inputs, "task_class", "producer");
    json_object_array_add(observations, observation("alpha", admission(2, 1, 0, 0, 8, "gpu", 1000), NULL, false));
    json_object_object_add(policy, "validator_reserve_slots", json_object_new_int64(1));
    candidates = placement_decide(producer_inputs, observations, policy, none, none, 1000, &decision);
    assert(has_reason(candidate_named(candidates, "alpha"), "validator_reserved") && !strcmp(f_string(decision, "reason"), "no_eligible_host") && json_object_is_type(f_field(decision, "host"), json_type_null));
    json_object_put(candidates); json_object_put(decision);
    candidates = placement_decide(validator_inputs, observations, policy, none, none, 1000, &decision);
    assert(rank_of(candidates, "alpha") == 1);
    json_object_put(candidates); json_object_put(decision);
    json_object_object_add(policy, "validator_reserve_slots", json_object_new_int64(0));
    json_object_put(observations); json_object_put(producer_inputs); json_object_put(none);
}
static void record_store(void) {
    char id[70], other[70], host[128], path[F_PATH], directory[F_PATH]; json_object *record, *read, *receipt, *error;
    assert(!placement_task_id("same-key", id) && !strncmp(id, "task_", 5) && strlen(id) == 69);
    assert(!placement_task_id("same-key", other) && !strcmp(id, other));
    assert(!placement_task_id("other-key", other) && strcmp(id, other));
    assert(placement_task_id("bad key", other));
    assert(!placement_record_read(id) && !placement_generation(id, host));
    record = f_parse("{\"schema_version\":1,\"task_id\":\"x\",\"submission_key\":\"same-key\",\"generation\":2,\"assigned_host\":\"bravo\",\"generations\":[]}");
    f_string_add(record, "task_id", id);
    assert(!placement_record_write(record) && f_string(record, "sha256"));
    read = placement_record_read(id);
    assert(read && json_object_equal(read, record) && placement_generation(id, host) == 2 && !strcmp(host, "bravo"));
    json_object_put(read);
    /* Result admission fence: only the current generation on the current host. */
    receipt = json_object_new_object(); f_string_add(receipt, "task_id", id); json_object_object_add(receipt, "assignment_generation", json_object_new_int(2));
    assert(!placement_result_fence(receipt, "bravo") && !placement_result_fence(receipt, NULL));
    error = placement_result_fence(receipt, "alpha"); assert(error && !strcmp(f_string(f_field(error, "error"), "code"), "stale_generation")); json_object_put(error);
    json_object_object_add(receipt, "assignment_generation", json_object_new_int(1));
    error = placement_result_fence(receipt, "bravo"); assert(error && f_number_is(f_field(error, "error"), "current_generation", 2)); json_object_put(error);
    json_object_object_del(receipt, "assignment_generation");
    error = placement_result_fence(receipt, NULL); assert(error && f_number_is(f_field(error, "error"), "result_generation", 1)); json_object_put(error);
    f_string_add(receipt, "task_id", other);
    assert(!placement_result_fence(receipt, "anything"));
    json_object_put(receipt);
    /* A tampered record fails its digest and is not trusted. */
    assert(!placement_directory(id, directory, false) && !f_path(path, sizeof(path), directory, "placement.json"));
    json_object_object_add(record, "generation", json_object_new_int(3));
    { const char *text = json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN); assert(!f_write(path, text, strlen(text), true)); }
    assert(!placement_record_read(id));
    json_object_put(record);
}
static void policy_file(void) {
    char path[F_PATH]; json_object *policy;
    policy = placement_policy_load(); assert(policy && f_number_is(policy, "max_queued", 8) && f_number_is(policy, "max_observation_age_seconds", 60) && f_number_is(policy, "validator_reserve_slots", 0)); json_object_put(policy);
    assert(!f_path(path, sizeof(path), f_home, "fleet/placement-policy.json"));
    assert(!f_write(path, "{\"max_queued\":3}", 16, true));
    policy = placement_policy_load(); assert(policy && f_number_is(policy, "max_queued", 3) && f_number_is(policy, "policy_version", 1)); json_object_put(policy);
    assert(!f_write(path, "{\"max_queued\":3,\"unknown\":1}", 28, true)); assert(!placement_policy_load());
    assert(!f_write(path, "{\"policy_version\":2}", 20, true)); assert(!placement_policy_load());
    assert(!f_write(path, "{\"max_queued\":999}", 18, true)); assert(!placement_policy_load());
    unlink(path);
}
int main(void) {
    char dir[] = "/tmp/hydra-placement-unit.XXXXXX", fleet_dir[F_PATH]; json_object *spec, *inputs, *policy;
    assert(mkdtemp(dir)); f_home = dir; f_hydra = "hydra";
    assert(!f_path(fleet_dir, sizeof(fleet_dir), dir, "fleet") && !f_mkdirs(fleet_dir));
    spec = f_parse(spec_text); assert(spec);
    inputs = placement_inputs(spec);
    assert(!strcmp(f_string(inputs, "task_class"), "validator") && !strcmp(f_string(inputs, "required_labels_key"), "gpu") && json_object_is_type(f_field(inputs, "platform"), json_type_null));
    assert(json_object_array_length(f_field(inputs, "adapter_capabilities")) == 1 && f_number_is(inputs, "disk_bytes_required", 8192));
    policy = placement_policy_defaults();
    {
        json_object *platform_inputs = f_parse(json_object_to_json_string_ext(inputs, JSON_C_TO_STRING_PLAIN));
        f_string_add(platform_inputs, "platform", "linux-x86_64");
        filtering_and_ranking(platform_inputs, policy);
        json_object_put(platform_inputs);
    }
    validator_headroom(inputs, policy);
    record_store();
    policy_file();
    json_object_put(policy); json_object_put(inputs); json_object_put(spec);
    assert(!f_remove_tree(dir));
    puts("Placement filtering, ranking ties, stale capacity, replay identity, fencing and policy passed");
    return 0;
}
