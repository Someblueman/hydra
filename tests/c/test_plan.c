#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *f_home, *f_hydra;
static json_object *fixture(void) { return plan_read("tests/fixtures/plan/plan.json"); }
static void expect(json_object *plan, json_object *policy, const char *code) {
    json_object *errors = json_object_new_array(); size_t i; bool found = false;
    int status = plan_validate(plan, policy, errors);
    if (!code) assert(status == 0 && json_object_array_length(errors) == 0);
    else {
        assert(status != 0);
        for (i = 0; i < json_object_array_length(errors); i++) if (!strcmp(f_string(json_object_array_get_idx(errors, i), "code"), code)) found = true;
        if (!found) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
        assert(found);
    }
    json_object_put(errors);
}
static void terminal_cases(void) {
    json_object *plan = fixture(), *policy = plan_read("tests/fixtures/plan/policy.json");
    json_object *args = f_field(json_object_array_get_idx(f_field(plan, "steps"), 0), "args");
    f_string_add(args, "terminal_mode", "headless"); expect(plan, policy, NULL);
    f_string_add(args, "terminal_mode", "interactive"); expect(plan, policy, NULL);
    f_string_add(args, "terminal_mode", "auto"); expect(plan, policy, "invalid_step");
    json_object_object_add(args, "terminal_mode", json_object_new_boolean(true)); expect(plan, policy, "invalid_step");
    json_object_object_add(args, "terminal_mode", NULL); expect(plan, policy, "invalid_step");
    json_object_object_del(args, "terminal_mode"); expect(plan, policy, NULL);
    json_object_put(plan); json_object_put(policy);
}
static void graph_cases(void) {
    json_object *plan = fixture(), *policy = plan_read("tests/fixtures/plan/policy.json"), *steps, *compose, *worker;
    assert(plan && policy); expect(plan, policy, NULL);
    json_object_object_add(f_field(policy, "envelope"), "disk_mb", json_object_new_int(2));
    expect(plan, policy, "unauthorized");
    json_object_object_add(f_field(plan, "envelope"), "disk_mb", json_object_new_int(3));
    expect(plan, policy, NULL);
    json_object_object_add(f_field(policy, "envelope"), "disk_mb", json_object_new_int(1));
    steps = f_field(plan, "steps");
    json_object_object_add(json_object_array_get_idx(steps, 0), "needs", f_parse_value("[\"verify\"]"));
    expect(plan, policy, "cycle"); json_object_put(plan); plan = fixture(); steps = f_field(plan, "steps");
    compose = json_object_array_get_idx(steps, 1); worker = plan_canonical(compose);
    f_string_add(worker, "id", "parallel"); f_string_add(worker, "role", "work");
    json_object_object_add(compose, "writes", f_parse_value("[\"plan-smoke:shared\"]"));
    json_object_object_add(worker, "writes", f_parse_value("[\"plan-smoke:shared/subdir\"]"));
    json_object_object_add(f_field(plan, "envelope"), "writes", f_parse_value("[\"plan-smoke:*\"]"));
    json_object_object_add(f_field(policy, "envelope"), "writes", f_parse_value("[\"plan-smoke:*\"]"));
    json_object_array_add(steps, worker); expect(plan, policy, "write_conflict");
    json_object_object_add(compose, "needs", f_parse_value("[\"spawn\",\"parallel\"]")); expect(plan, policy, NULL);
    json_object_object_add(f_field(policy, "envelope"), "writes", f_parse_value("[\"other-head:*\"]")); expect(plan, policy, "unauthorized");
    json_object_put(plan); json_object_put(policy);
}
static void parse_cases(const char *root) {
    const char *bad[] = {"{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}", "{\"nested\":{\"a\":0,\"a\":0}}", "{", "{\"a\"", "{\"a\":", "{\"a\":[]", "{\"a\":1} trailing", "{\"a\":NaN}", "{\"a\":1e999}", "{\"a\":\"\\u0000\"}", NULL};
    char path[F_PATH], a[65], b[65]; json_object *first, *second; size_t i;
    assert(!f_path(path, sizeof(path), root, "input.json"));
    for (i = 0; bad[i]; i++) {
        assert(!f_write(path, bad[i], strlen(bad[i]), true)); first = plan_read(path);
        /* NUL is valid JSON string data but never a valid contract string. */
        if (i == 10) { assert(first && !plan_text(f_field(first, "a"))); json_object_put(first); }
        else assert(!first);
    }
    assert(!f_write(path, "{}\0junk", 7, true)); assert(!plan_read(path));
    first = f_parse("{\"b\":{\"z\":1,\"a\":2},\"a\":[1,2]}"); second = f_parse("{\"a\":[1,2],\"b\":{\"a\":2,\"z\":1}}");
    assert(!plan_digest(first, a) && !plan_digest(second, b) && !strcmp(a, b));
    json_object_put(first); json_object_put(second);
    {
        char *large = malloc(PLAN_LIMIT + 2); assert(large); memset(large, ' ', PLAN_LIMIT + 1); large[0] = '{'; large[1] = '}';
        assert(!f_write(path, large, PLAN_LIMIT + 1, true)); assert(!plan_read(path)); free(large);
    }
}
static void report_cases(void) {
    const char *subject = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char digest[65], changed[65];
    json_object *compiled = json_object_new_object(), *plan = fixture(), *check, *report;
    json_object_object_add(compiled, "plan", plan);
    check = json_object_array_get_idx(f_field(plan, "checks"), 0);
    assert(!plan_check_digest(compiled, "check", digest));
    report = f_parse("{\"schema_version\":2,\"verdict\":\"pass\",\"requirements\":[\"content\"],\"evidence\":\"Checked exact bytes\"}");
    f_string_add(report, "subject_sha256", subject); f_string_add(report, "validator_sha256", digest);
    assert(plan_report(compiled, check, report, subject) == PLAN_PASS);
    f_string_add(report, "verdict", "fail"); assert(plan_report(compiled, check, report, subject) == PLAN_FAIL);
    f_string_add(report, "verdict", "inconclusive"); assert(plan_report(compiled, check, report, subject) == PLAN_INCONCLUSIVE);
    f_string_add(report, "verdict", "pass");
    f_string_add(check, "definition", "Changed acceptance rubric");
    assert(!plan_check_digest(compiled, "check", changed) && strcmp(digest, changed));
    assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    f_string_add(report, "validator_sha256", changed);
    assert(plan_report(compiled, check, report, subject) == PLAN_PASS);
    assert(plan_report(compiled, check, report, changed) == PLAN_INVALID);
    json_object_object_add(report, "requirements", f_parse_value("[\"content\",\"content\"]"));
    assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    json_object_object_add(report, "requirements", f_parse_value("[1]"));
    assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    json_object_object_add(report, "requirements", f_parse_value("[\"content\"]"));
    json_object_object_add(report, "schema_version", json_object_new_int(1));
    assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    json_object_object_del(report, "validator_sha256");
    assert(plan_report(compiled, check, report, subject) == PLAN_PASS);
    f_string_add(report, "verdict", "inconclusive");
    assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    assert(plan_report(compiled, check, NULL, subject) == PLAN_INVALID);
    json_object_put(report); json_object_put(compiled);
}
static void structured_report_cases(void) {
    const char *subject = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char validator[65], recipe[65], raw_hash[65], evidence_hash[65];
    json_object *compiled = json_object_new_object(), *plan = fixture(), *check, *report, *record, *observation, *raw, *observations;
    assert(plan); json_object_object_add(compiled, "plan", plan); check = json_object_array_get_idx(f_field(plan, "checks"), 0);
    f_string_add(check, "definition", "{\"predicate\":\"equals\",\"cases\":[{\"id\":\"case-1\",\"expected\":1}]}" );
    json_object_object_add(plan, "obligations", f_parse_value("[{\"id\":\"obligation\",\"evaluation\":{\"check\":\"check\"},\"required_evidence\":[\"measurements\"]}]"));
    assert(!plan_check_digest(compiled, "check", validator) && !plan_recipe_digest(compiled, "check", recipe));
    raw = f_parse("{\"measurement\":1}"); assert(!plan_digest(raw, raw_hash));
    observation = f_parse("{\"id\":\"case-1\",\"predicate\":\"equals\",\"expected\":1,\"actual\":1,\"raw\":{\"measurement\":1},\"raw_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}");
    json_object_object_del(observation, "predicate"); json_object_object_del(observation, "expected"); json_object_object_del(observation, "actual"); json_object_object_add(f_field(observation, "raw"), "actual", json_object_new_int(1)); assert(!plan_digest(f_field(observation, "raw"), raw_hash)); f_string_add(observation, "raw_sha256", raw_hash); observations = json_object_new_array(); json_object_array_add(observations, observation); assert(!plan_digest(observations, evidence_hash));
    record = f_parse("{\"obligation_id\":\"obligation\"}"); f_string_add(record, "subject_manifest_sha256", subject); f_string_add(record, "validator_identity", "fixture"); f_string_add(record, "validator_recipe_sha256", recipe);
    json_object_object_add(record, "invocation", f_parse("{\"argv\":[\"fixture\"],\"exit_code\":0}")); json_object_object_add(record, "environment", f_parse("{\"host\":\"local\",\"toolchain\":\"fixture\"}"));
    json_object_object_add(record, "case_inventory", f_parse_value("[\"case-1\"]")); json_object_object_add(record, "observations", observations); f_string_add(record, "raw_evidence_sha256", evidence_hash); json_object_object_add(record, "counts", f_parse("{\"executed\":1,\"failed\":0,\"skipped\":0}")); json_object_object_add(record, "limitations", json_object_new_array());
    report = f_parse("{\"schema_version\":3,\"execution_status\":\"completed\",\"evidence_status\":\"valid\",\"domain_verdict\":\"pass\",\"verdict\":\"pass\",\"subject_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"validator_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"evidence_records\":[],\"limitations\":[],\"requirements\":[\"content\"],\"evidence\":\"fixture\"}");
    f_string_add(report, "validator_sha256", validator); { json_object *records = json_object_new_array(); json_object_array_add(records, record); json_object_object_add(report, "evidence_records", records); }
    assert(plan_report(compiled, check, report, subject) == PLAN_PASS); record = json_object_array_get_idx(f_field(report, "evidence_records"), 0);
    {
        json_object *record_raw = f_field(json_object_array_get_idx(f_field(record, "observations"), 0), "raw");
        json_object *record_observations = f_field(record, "observations");
        const char *bad_measurements[] = {"looks good", "{}", NULL};
        for (size_t i = 0; bad_measurements[i]; i++) {
            json_object_object_add(record_raw, "measurement", f_parse_value(bad_measurements[i][0] == '{' ? bad_measurements[i] : "\"looks good\""));
            assert(!plan_digest(record_raw, raw_hash)); f_string_add(json_object_array_get_idx(record_observations, 0), "raw_sha256", raw_hash);
            assert(!plan_digest(record_observations, evidence_hash)); f_string_add(record, "raw_evidence_sha256", evidence_hash);
            assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
            json_object_object_add(record_raw, "measurement", json_object_new_int(1)); assert(!plan_digest(record_raw, raw_hash));
            f_string_add(json_object_array_get_idx(record_observations, 0), "raw_sha256", raw_hash); assert(!plan_digest(record_observations, evidence_hash)); f_string_add(record, "raw_evidence_sha256", evidence_hash);
        }
        json_object_object_add(record_raw, "measurement", json_object_new_double(NAN));
        assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
        json_object_object_add(record_raw, "measurement", json_object_new_int(1)); assert(!plan_digest(record_raw, raw_hash));
        f_string_add(json_object_array_get_idx(record_observations, 0), "raw_sha256", raw_hash); assert(!plan_digest(record_observations, evidence_hash)); f_string_add(record, "raw_evidence_sha256", evidence_hash);
    }
    f_string_add(check, "definition", "{\"predicate\":\"equals\",\"cases\":[{\"id\":\"case-1\",\"expected\":1},{\"id\":\"case-1\",\"expected\":1}]}" );
    assert(!plan_recipe_digest(compiled, "check", recipe)); f_string_add(record, "validator_recipe_sha256", recipe); assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    f_string_add(check, "definition", "{}"); assert(!plan_recipe_digest(compiled, "check", recipe)); f_string_add(record, "validator_recipe_sha256", recipe); assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    f_string_add(check, "definition", "{\"predicate\":\"equals\",\"cases\":[{\"id\":\"case-1\",\"expected\":1}]}" ); assert(!plan_recipe_digest(compiled, "check", recipe)); f_string_add(record, "validator_recipe_sha256", recipe);
    json_object_object_del(f_field(record, "invocation"), "exit_code"); json_object_object_add(f_field(record, "invocation"), "exit_code", json_object_new_int(1)); assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    json_object_object_del(f_field(record, "invocation"), "exit_code"); json_object_object_add(f_field(record, "invocation"), "exit_code", json_object_new_int(0)); json_object_object_del(record, "case_inventory"); assert(plan_report(compiled, check, report, subject) == PLAN_INVALID);
    json_object_put(report); json_object_put(raw); json_object_put(compiled);
}
static void distributed_graph_cases(void) {
    json_object *plan = plan_read("tests/fixtures/plan-task/plan.json"), *policy = plan_read("tests/fixtures/plan-task/policy.json");
    assert(plan && policy); expect(plan, policy, NULL);
    json_object *compose = json_object_array_get_idx(f_field(plan, "steps"), 2);
    json_object_object_add(compose, "needs", f_parse_value("[\"produce\"]"));
    expect(plan, policy, "missing_evidence_join");
    json_object_object_add(compose, "needs", f_parse_value("[\"produce\",\"inspect\"]"));
    f_string_add(f_field(compose, "args"), "source_step", "produce"); expect(plan, policy, NULL);
    json_object *validator = json_object_array_get_idx(f_field(plan, "steps"), 1);
    f_string_add(f_field(validator, "args"), "source_step", "produce"); expect(plan, policy, "invalid_step");
    json_object_object_del(f_field(validator, "args"), "source_step");
    json_object *inputs = f_field(f_field(f_field(f_field(plan, "data"), "steps"), "inspect"), "inputs");
    json_object_object_del(inputs, "validation"); expect(plan, policy, "missing_validation_context");
    json_object_object_add(inputs, "validation", f_parse_value("{\"validation\":\"plan\"}"));
    json_object_object_del(json_object_array_get_idx(f_field(plan, "checks"), 0), "step");
    expect(plan, policy, "invalid_verification");
    json_object_put(plan); json_object_put(policy);
}
static void repair_policy_cases(void) {
    json_object *plan = plan_read("tests/fixtures/plan-task/plan.json"), *policy = plan_read("tests/fixtures/plan-task/policy.json");
    json_object_object_add(f_field(plan, "envelope"), "repair_budget", json_object_new_int(1)); expect(plan, policy, "over_budget");
    json_object_object_add(f_field(policy, "envelope"), "repair_budget", json_object_new_int(1)); expect(plan, policy, "missing_repair_context");
    const char *producers[] = {"produce", "compose"};
    for (size_t i = 0; i < 2; i++) {
        json_object *inputs = f_field(f_field(f_field(f_field(plan, "data"), "steps"), producers[i]), "inputs");
        json_object_object_add(inputs, "repair", f_parse("{\"repair\":\"plan\"}"));
    }
    expect(plan, policy, NULL);
    json_object_object_add(f_field(plan, "envelope"), "repair_budget", json_object_new_int(11)); expect(plan, policy, "invalid_policy");
    json_object_put(plan); plan = fixture();
    json_object_object_add(f_field(plan, "envelope"), "repair_budget", json_object_new_int(1)); expect(plan, policy, "unsupported_repair");
    json_object_put(plan); json_object_put(policy);
}
static void check_ownership_cases(void) {
    const char *plans[] = {"tests/fixtures/plan/plan.json", "tests/fixtures/plan-task/plan.json"};
    const char *policies[] = {"tests/fixtures/plan/policy.json", "tests/fixtures/plan-task/policy.json"};
    for (size_t i = 0; i < 2; i++) {
        json_object *plan = plan_read(plans[i]), *policy = plan_read(policies[i]);
        json_object *requirements = f_field(plan, "requirements"), *checks = f_field(plan, "checks");
        json_object *extra = plan_canonical(json_object_array_get_idx(requirements, 0));
        f_string_add(extra, "id", "extra"); json_object_array_add(requirements, extra);
        expect(plan, policy, NULL); /* A check may own several requirements. */
        json_object *orphan = plan_canonical(json_object_array_get_idx(checks, 0));
        f_string_add(orphan, "id", "orphan"); json_object_array_add(checks, orphan);
        expect(plan, policy, "orphan_check");
        json_object_put(plan); json_object_put(policy);
    }
}
static json_object *obligation_set(void) {
    return f_parse_value("[{\"id\":\"content-behavior\",\"requirement\":\"content\",\"intent_ref\":\"objective\",\"subject\":{\"deliverable\":\"report\",\"step\":\"compose\",\"output\":\"report\"},\"criterion\":\"Report has the expected contents\",\"evaluation\":{\"method\":\"executable\",\"check\":\"check\"},\"required_evidence\":[\"subject_sha256\",\"verdict\",\"evidence\"],\"environment\":{\"hosts\":[\"local\"],\"tools\":[\"sh\"],\"effects\":[\"execute\"]},\"completion_rule\":\"verdict=pass\",\"limitations\":[\"fixture-only\"]},{\"id\":\"content-failure\",\"requirement\":\"content\",\"intent_ref\":\"objective\",\"subject\":{\"deliverable\":\"report\",\"step\":\"compose\",\"output\":\"report\"},\"criterion\":\"Incorrect content is rejected\",\"evaluation\":{\"method\":\"executable\",\"check\":\"check\"},\"required_evidence\":[\"subject_sha256\",\"verdict\",\"evidence\"],\"environment\":{\"hosts\":[\"local\"],\"tools\":[\"sh\"],\"effects\":[\"execute\"]},\"completion_rule\":\"verdict=pass\",\"limitations\":[\"fixture-only\"]}]");
}
static void obligation_cases(void) {
    json_object *plan = fixture(), *policy = plan_read("tests/fixtures/plan/policy.json"), *obligations;
    assert(plan && policy); obligations = obligation_set(); assert(obligations);
    json_object_object_add(plan, "obligations", obligations); expect(plan, policy, NULL);
    json_object_object_del(plan, "obligations"); json_object_object_add(plan, "obligations", obligation_set());
    json_object_object_add(json_object_array_get_idx(f_field(plan, "obligations"), 0), "subject", f_parse_value("{\"deliverable\":\"report\",\"step\":\"spawn\",\"output\":\"report\"}"));
    expect(plan, policy, "impossible_evaluation"); json_object_put(plan); json_object_put(policy);

    plan = fixture(); policy = plan_read("tests/fixtures/plan/policy.json"); obligations = obligation_set();
    json_object_object_add(plan, "obligations", obligations);
    json_object_object_add(json_object_array_get_idx(f_field(plan, "obligations"), 0), "required_evidence", f_parse_value("[\"subject_sha256\",\"verdict\"]"));
    expect(plan, policy, "missing_required_evidence"); json_object_put(plan); json_object_put(policy);

    plan = fixture(); policy = plan_read("tests/fixtures/plan/policy.json"); obligations = obligation_set();
    json_object_object_add(plan, "obligations", obligations);
    json_object_object_add(json_object_array_get_idx(f_field(plan, "obligations"), 0), "required_evidence", f_parse_value("[\"subject_sha256\",\"verdict\",\"evidence\",\"content-failure\"]"));
    json_object_object_add(json_object_array_get_idx(f_field(plan, "obligations"), 1), "required_evidence", f_parse_value("[\"subject_sha256\",\"verdict\",\"evidence\",\"content-behavior\"]"));
    expect(plan, policy, "circular_evidence"); json_object_put(plan); json_object_put(policy);

    plan = fixture(); policy = plan_read("tests/fixtures/plan/policy.json"); obligations = obligation_set();
    json_object_object_add(plan, "obligations", obligations); f_string_add(plan, "objective", "Improve performance while preserving text content");
    obligations = plan_obligations_reviews(plan); assert(json_object_array_length(obligations) == 2); json_object_put(obligations); json_object_put(plan); json_object_put(policy);
}
int main(void) {
    char root[] = "/tmp/hydra-plan-unit.XXXXXX";
    assert(mkdtemp(root)); f_home = root; f_hydra = "hydra";
    terminal_cases(); graph_cases(); distributed_graph_cases(); repair_policy_cases(); check_ownership_cases(); obligation_cases(); parse_cases(root); report_cases(); structured_report_cases(); assert(!f_remove_tree(root));
    puts("planning graph, policy, canonical JSON and parser checks passed"); return 0;
}
