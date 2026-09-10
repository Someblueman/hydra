#include "fixture.h"

static int is_mode(const char *mode, const char *value) { return !strcmp(mode, value); }
static void digest(json_object *value, char out[65]) {
    fx_require(!plan_digest(value, out), "JSON digest");
}
static void report(const char *subject, const char *validation, const char *output) {
    char *bytes = f_read(subject, 1024 * 1024), raw_hash[65], evidence_hash[65], subject_hash[65];
    fx_require(bytes != NULL && !f_hash(subject, subject_hash), "subject bytes");
    json_object *context = fx_read(validation), *data = fx_field(context, "data");
    const char *mode = getenv("HYDRA_V3_FAULT");
    if (!mode)
        mode = "";
    int assessment = !strncmp(mode, "assessment-", 11);
    int failed = is_mode(mode, "wrong-artifact") || is_mode(mode, "nonzero") ||
                 is_mode(mode, "assessment-fail");
    const char *domain = is_mode(mode, "assessment-inconclusive") ? "inconclusive"
                         : failed                                 ? "fail"
                                                                  : "pass";
    json_object *raw = json_object_new_object();
    if (assessment) {
        f_string_add(raw, "verdict",
                     is_mode(mode, "assessment-inconclusive") ? "inconclusive"
                     : is_mode(mode, "assessment-fail")       ? "fail"
                                                              : "pass");
        f_string_add(raw, "explanation", "Assessment fixture");
    } else
        f_string_add(raw, "actual", bytes);
    json_object_object_add(raw, "measurement", json_object_new_int64((int64_t)strlen(bytes)));
    digest(raw, raw_hash);
    json_object *observations = json_object_new_array(), *observation = json_object_new_object(),
                *inventory = json_object_new_array();
    f_string_add(observation, "id", assessment ? "content-check" : "case-1");
    f_string_add(observation, "raw_sha256", raw_hash);
    json_object_object_add(observation, "raw", raw);
    json_object_array_add(observations, observation);
    json_object_array_add(inventory,
                          json_object_new_string(assessment ? "content-check" : "case-1"));
    if (is_mode(mode, "raw-tampered"))
        f_string_add(raw, "actual", "Tampered after hashing");
    if (is_mode(mode, "missing-measurement")) {
        json_object_object_del(raw, "measurement");
        digest(raw, raw_hash);
        f_string_add(observation, "raw_sha256", raw_hash);
    }
    if (is_mode(mode, "drop")) {
        json_object_put(observations);
        json_object_put(inventory);
        observations = json_object_new_array();
        inventory = json_object_new_array();
    }
    digest(observations, evidence_hash);
    if (is_mode(mode, "stale-subject"))
        memset(subject_hash, '0', 64);
    const char *validator = f_string(data, "check"), *recipe = f_string(data, "check-recipe");
    fx_require(validator && recipe, "validation context");
    if (is_mode(mode, "changed-predicate") || is_mode(mode, "assessment-stale-rubric"))
        recipe = "0000000000000000000000000000000000000000000000000000000000000000";
    json_object *record =
        f_parse("{\"obligation_id\":\"content-check\",\"validator_identity\":\"fixture-v3\","
                "\"invocation\":{\"argv\":[\"sh\",\"check.sh\"],\"exit_code\":0},\"environment\":{"
                "\"host\":\"local\",\"toolchain\":\"native-c\"},\"counts\":{\"executed\":0,"
                "\"failed\":0,\"skipped\":0},\"limitations\":[]}");
    f_string_add(record, "subject_manifest_sha256", subject_hash);
    f_string_add(record, "validator_recipe_sha256", recipe);
    json_object_object_add(fx_field(record, "invocation"), "exit_code",
                           json_object_new_int(is_mode(mode, "nonzero") ? 7 : 0));
    json_object *counts = fx_field(record, "counts");
    json_object_object_add(counts, "executed",
                           json_object_new_int64((int64_t)json_object_array_length(observations)));
    json_object_object_add(
        counts, "failed",
        json_object_new_int(is_mode(mode, "wrong-artifact") || is_mode(mode, "assessment-fail")));
    json_object_object_add(record, "case_inventory", inventory);
    json_object_object_add(record, "observations", observations);
    f_string_add(record, "raw_evidence_sha256", evidence_hash);
    if (assessment) {
        json_object *reviewer =
            f_parse("{\"rubric\":\"Assessment rubric: determine whether the report is "
                    "acceptable.\",\"source_locators\":[\"fixture\"],\"disagreement\":\"None\","
                    "\"authority\":\"fixture\"}");
        if (is_mode(mode, "assessment-missing-authority"))
            json_object_object_del(reviewer, "authority");
        json_object_object_add(record, "reviewer_decision", reviewer);
    }
    json_object *result =
        f_parse("{\"schema_version\":3,\"execution_status\":\"completed\",\"evidence_status\":"
                "\"valid\",\"requirements\":[\"content\"],\"evidence\":\"structured "
                "fixture\",\"limitations\":[],\"evidence_records\":[]}");
    f_string_add(result, "domain_verdict", domain);
    f_string_add(result, "verdict", domain);
    f_string_add(result, "subject_sha256", subject_hash);
    f_string_add(result, "validator_sha256", validator);
    json_object_array_add(fx_field(result, "evidence_records"), record);
    fx_save(output, result);
    json_object_put(result);
    json_object_put(context);
    free(bytes);
}
static void prepare(const char *path) {
    json_object *plan = fx_read(path);
    json_object_object_add(fx_field(plan, "envelope"), "timeout_seconds", json_object_new_int(40));
    json_object *steps = fx_field(plan, "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (!strcmp(f_string(step, "kind"), "exec"))
            json_object_object_add(fx_field(step, "args"), "timeout", json_object_new_int(10));
    }
    f_string_add(json_object_array_get_idx(fx_field(plan, "checks"), 0), "definition",
                 "{\"predicate\":\"equals\",\"cases\":[{\"id\":\"case-1\",\"expected\":\"Delivered "
                 "report\\n\"}]}");
    json_object_object_add(
        plan, "obligations",
        f_parse_value(
            "[{\"id\":\"content-check\",\"requirement\":\"content\",\"intent_ref\":\"objective\","
            "\"subject\":{\"deliverable\":\"report\",\"step\":\"compose\",\"output\":\"report\"},"
            "\"criterion\":\"The sealed report has the expected "
            "content\",\"evaluation\":{\"method\":\"executable\",\"check\":\"check\"},\"required_"
            "evidence\":[\"subject_sha256\",\"verdict\",\"evidence\",\"measurements\"],"
            "\"environment\":{\"hosts\":[\"local\"],\"tools\":[\"sh\"],\"effects\":[\"execute\"]},"
            "\"completion_rule\":\"verdict=pass\",\"limitations\":[\"fixture-only\"]}]"));
    fx_save(path, plan);
    json_object_put(plan);
}
static void change_mode(const char *path, const char *mode, const char *branch) {
    char *text = f_read(path, PLAN_LIMIT);
    fx_require(text != NULL, path);
    /* Replace only the fixed fixture branch token before parsing its new plan. */
    const char *token = "plan-smoke";
    size_t count = 0, old = strlen(token), replacement = strlen(branch);
    for (const char *at = text; (at = strstr(at, token)) != NULL; at += old)
        count++;
    fx_require(replacement < 256 && count < PLAN_STEPS, "fixture branch bound");
    size_t size = strlen(text) + count * replacement + 1;
    char *changed = malloc(size);
    fx_require(changed != NULL, "branch allocation");
    char *out = changed;
    const char *cursor = text, *at;
    while ((at = strstr(cursor, token)) != NULL) {
        size_t prefix = (size_t)(at - cursor);
        memcpy(out, cursor, prefix);
        out += prefix;
        memcpy(out, branch, replacement);
        out += replacement;
        cursor = at + old;
    }
    memcpy(out, cursor, strlen(cursor) + 1);
    json_object *plan = f_parse(changed);
    fx_require(plan != NULL, "changed plan");
    json_object *check = json_object_array_get_idx(fx_field(plan, "checks"), 0);
    if (is_mode(mode, "malformed-recipe"))
        f_string_add(check, "definition", "{}");
    if (!strncmp(mode, "assessment-", 11)) {
        f_string_add(check, "method", "assessment");
        f_string_add(check, "definition",
                     "Assessment rubric: determine whether the report is acceptable.");
        f_string_add(
            fx_field(json_object_array_get_idx(fx_field(plan, "obligations"), 0), "evaluation"),
            "method", "assessment");
    }
    fx_save(path, plan);
    json_object_put(plan);
    free(changed);
    free(text);
}
int fx_v3(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "v3-report")) {
        report(argv[2], argv[3], argv[4]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "v3-plan")) {
        prepare(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "v3-mode")) {
        change_mode(argv[2], argv[3], argv[4]);
        return 0;
    }
    return 1;
}
