#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan_expansion.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/task/task.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Admission of bounded expansions over fabricated run records: the library is
 * exercised directly so every evidence state (passing, failed, unknown,
 * tampered, unsupported bytes) is produced without executing work. */
const char *f_home, *f_hydra;
static char root[F_PATH], repo[F_PATH], runs[F_PATH], expansions[F_PATH], base_run[F_PATH], base_sha[65];
static json_object *base_compiled;

static void write_text(const char *directory, const char *name, const char *text) {
    char path[F_PATH];
    assert(!f_path(path, sizeof(path), directory, name)); unlink(path);
    assert(!f_write(path, text, strlen(text), false));
}
static void write_canonical(const char *directory, const char *name, json_object *value) {
    json_object *canonical = plan_canonical(value);
    write_text(directory, name, json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN));
    json_object_put(canonical);
}
static void make_dir(const char *directory, const char *name, char out[F_PATH]) {
    assert(!f_path(out, F_PATH, directory, name));
    if (mkdir(out, 0700)) assert(errno == EEXIST);
}
/* Seals one artifact of a step's first attempt the way the runtime does. */
static void seal(json_object *compiled, const char *run, const char *step, const char *name, const char *bytes, size_t length, char digest[65]) {
    char steps[F_PATH], directory[F_PATH], attempt[F_PATH], artifacts[F_PATH], path[F_PATH]; json_object *file, *receipt;
    make_dir(run, "steps", steps); make_dir(steps, step, directory); make_dir(directory, "attempt-1", attempt); make_dir(attempt, "artifacts", artifacts);
    write_text(directory, "state", "succeeded\n"); write_text(directory, "attempts", "1\n");
    assert(!f_path(path, sizeof(path), artifacts, name)); unlink(path); assert(!f_write(path, bytes, length, false));
    file = wd_file(path, f_field(f_field(f_field(f_field(f_field(compiled, "data"), "steps"), step), "outputs"), name));
    assert(file && !f_copy(digest, 65, f_string(file, "sha256")));
    receipt = f_parse("{\"schema_version\":1,\"files\":{}}"); json_object_object_add(f_field(receipt, "files"), name, file);
    write_canonical(attempt, "outputs.json", receipt); json_object_put(receipt);
}
static void seal_check(json_object *compiled, const char *run, const char *step, const char *name, const char *subject, const char *requirement, const char *verdict) {
    char report[512], digest[65];
    snprintf(report, sizeof(report), "{\"schema_version\":1,\"verdict\":\"%s\",\"subject_sha256\":\"%s\",\"requirements\":[\"%s\"],\"evidence\":\"Compared exact expected text\"}\n", verdict, subject, requirement);
    seal(compiled, run, step, name, report, strlen(report), digest);
}
/* A succeeded schema 1 run with sealed report and passing check. */
static void fabricate_run(json_object *compiled, const char *id, const char *compose, const char *verify, const char *requirement, const char *report_bytes, const char *verdict, char run[F_PATH]) {
    char digest[65], accepted[66], steps[F_PATH], spawn[F_PATH];
    make_dir(runs, id, run);
    write_canonical(run, "compiled.json", compiled);
    assert(!plan_digest(compiled, digest)); snprintf(accepted, sizeof(accepted), "%s\n", digest); write_text(run, "plan-accepted", accepted);
    write_text(run, "run-id", id); write_text(run, "state", "succeeded\n");
    make_dir(run, "steps", steps); make_dir(steps, "spawn", spawn); write_text(spawn, "state", "succeeded\n"); write_text(spawn, "attempts", "1\n");
    seal(compiled, run, compose, "report", report_bytes, strlen(report_bytes), digest);
    seal_check(compiled, run, verify, "check", digest, requirement, verdict);
}
static json_object *compile(json_object *plan, json_object *policy, json_object *assets) {
    json_object *errors = json_object_new_array(), *compiled = plan_compile(plan, policy, repo, assets, errors);
    if (!compiled) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
    assert(compiled); json_object_put(errors); return compiled;
}
static json_object *policy_with_bounds(void) {
    json_object *policy = plan_read("tests/fixtures/plan/policy.json");
    json_object_object_add(policy, "expansion", f_parse("{\"max_nodes\":9,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}"));
    return policy;
}
static void policy_cases(void) {
    json_object *plan = plan_read("tests/fixtures/plan/plan.json"), *policy = policy_with_bounds(), *errors = json_object_new_array();
    assert(!plan_validate(plan, policy, errors) && !json_object_array_length(errors));
    json_object_object_del(f_field(policy, "expansion"), "max_rounds");
    assert(plan_validate(plan, policy, errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), "invalid_policy"));
    json_object_put(errors); errors = json_object_new_array();
    json_object_object_add(f_field(policy, "expansion"), "max_rounds", json_object_new_int(17));
    assert(plan_validate(plan, policy, errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), "invalid_policy"));
    json_object_put(errors); json_object_put(plan); json_object_put(policy);
}
static const char *proposal_text =
    "{\"schema_version\":1,\"expansion\":\"hydra-plan-expansion-1\",\"base_sha256\":\"%s\","
    "\"claims\":[{\"id\":\"second\",\"kind\":\"new\"}],\"dependencies\":[{\"input\":\"base\",\"deliverable\":\"report\"}],\"joins\":[\"compose2\"],"
    "\"steps\":[{\"id\":\"spawn2\",\"role\":\"work\",\"kind\":\"spawn\",\"needs\":[],\"writes\":[],\"args\":{\"branch\":\"plan-expansion\"}},"
    "{\"id\":\"compose2\",\"role\":\"compose\",\"kind\":\"exec\",\"needs\":[\"spawn2\"],\"writes\":[],\"args\":{\"head\":\"plan-expansion\",\"argv\":[\"sh\",\"compose.sh\"],\"timeout\":30}},"
    "{\"id\":\"verify2\",\"role\":\"verify\",\"kind\":\"exec\",\"needs\":[\"compose2\"],\"writes\":[],\"args\":{\"head\":\"plan-expansion\",\"argv\":[\"sh\",\"check.sh\"],\"timeout\":30}}],"
    "\"deliverables\":[{\"id\":\"report2\",\"description\":\"Second report\",\"step\":\"compose2\",\"output\":\"report\",\"destination\":\"run-artifact\"}],"
    "\"requirements\":[{\"id\":\"second\",\"criterion\":\"Second report equals the base report\",\"deliverable\":\"report2\",\"check\":\"check2\"}],"
    "\"checks\":[{\"id\":\"check2\",\"method\":\"executable\",\"definition\":\"Compare with base report\",\"step\":\"verify2\",\"input\":\"subject\",\"report\":\"check\",\"deliverable\":\"report2\"}],"
    "\"data\":{\"steps\":{\"compose2\":{\"inputs\":{\"base\":{\"input\":\"base\"}},\"outputs\":{\"report\":{\"type\":\"file\",\"path\":\"report.txt\",\"max_bytes\":1024}}},"
    "\"verify2\":{\"inputs\":{\"subject\":{\"step\":\"compose2\",\"output\":\"report\"},\"expected\":{\"input\":\"base\"}},\"outputs\":{\"check\":{\"type\":\"object\",\"path\":\"check.json\",\"max_bytes\":2048}}}}}}";
static json_object *proposal(const char *base) {
    char text[4096]; snprintf(text, sizeof(text), proposal_text, base); return f_parse(text);
}
static void schema_cases(void) {
    json_object *p = proposal(base_sha), *errors = json_object_new_array();
    assert(px_proposal_validate(p, errors) && !json_object_array_length(errors));
    json_object_object_add(p, "steps", json_object_new_array());
    assert(!px_proposal_validate(p, errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), "empty_expansion"));
    json_object_put(p); json_object_put(errors); p = proposal(base_sha); errors = json_object_new_array();
    json_object_object_add(p, "joins", f_parse_value("[\"absent\"]"));
    assert(!px_proposal_validate(p, errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), "invalid_joins"));
    json_object_put(p); json_object_put(errors); p = proposal(base_sha); errors = json_object_new_array();
    json_object_object_add(p, "claims", f_parse_value("[{\"id\":\"other\",\"kind\":\"new\"}]"));
    assert(!px_proposal_validate(p, errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), "unclaimed_requirement"));
    json_object_put(p); json_object_put(errors); p = proposal(base_sha); errors = json_object_new_array();
    json_object_object_add(p, "assets", json_object_new_object());
    assert(!px_proposal_validate(p, errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), "invalid_expansion"));
    json_object_put(p); json_object_put(errors);
}
static void expect_code(json_object *result, const char *code) {
    if (!f_field(result, "error") || strcmp(f_string(f_field(result, "error"), "code"), code)) fprintf(stderr, "expected %s: %s\n", code, json_object_to_json_string(result));
    assert(f_field(result, "error") && !strcmp(f_string(f_field(result, "error"), "code"), code));
}
/* Admits into a fresh scratch directory and publishes it as round-N on success. */
static json_object *admit(json_object *p, const char *expect, const char *detail_key, const char *detail) {
    struct px_chain chain; char scratch[F_PATH], name[32], target[F_PATH]; json_object *result; int round;
    px_chain_load(&chain, expansions, base_run, runs); round = (int)chain.count;
    snprintf(name, sizeof(name), ".round.%d", round); make_dir(expansions, name, scratch);
    result = px_admit(scratch, expansions, &chain, p, repo, 1700000000L);
    px_chain_free(&chain);
    if (expect) {
        expect_code(result, expect);
        if (detail_key) assert(!strcmp(f_string(f_field(result, "data"), detail_key), detail));
        assert(!f_remove_tree(scratch));
        return result;
    }
    if (f_field(result, "error")) fprintf(stderr, "%s\n", json_object_to_json_string(result));
    assert(!f_field(result, "error") && json_object_get_int(f_field(f_field(result, "data"), "round")) == round);
    snprintf(name, sizeof(name), "round-%d", round); assert(!f_path(target, sizeof(target), expansions, name) && !rename(scratch, target));
    return result;
}
static void show_text(char **text) {
    struct px_chain chain; json_object *data, *canonical;
    px_chain_load(&chain, expansions, base_run, runs); data = px_show(&chain); canonical = plan_canonical(data);
    *text = strdup(json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN));
    json_object_put(canonical); json_object_put(data); px_chain_free(&chain);
}
static json_object *bind(int round, const char *accepted, const char *expect) {
    char name[32], directory[F_PATH]; json_object *result;
    snprintf(name, sizeof(name), "round-%d", round); assert(!f_path(directory, sizeof(directory), expansions, name));
    result = px_bind(directory, accepted, runs);
    if (expect) expect_code(result, expect); else assert(!f_field(result, "error"));
    return result;
}
static void set_run_state(const char *run, const char *state) { write_text(run, "state", state); }
#include "test_plan_expansion_cases.inc"
int main(void) {
    char tmp[] = "/tmp/hydra-plan-expansion.XXXXXX", command[4 * F_PATH]; json_object *plan, *policy;
    assert(mkdtemp(tmp) && realpath(tmp, root)); f_home = root; f_hydra = "hydra";
    policy_cases();
    make_dir(root, "repo", repo); make_dir(root, "runs", runs); make_dir(root, "expansions", expansions);
    snprintf(command, sizeof(command), "cp tests/fixtures/plan/repo/* '%s' && cd '%s' && git init -q && git add . && "
        "git -c user.name=Test -c user.email=test@example.invalid -c commit.gpgSign=false commit -qm fixture", repo, repo);
    assert(!system(command));
    plan = plan_read("tests/fixtures/plan/plan.json"); policy = policy_with_bounds();
    base_compiled = compile(plan, policy, NULL); assert(!plan_digest(base_compiled, base_sha));
    fabricate_run(base_compiled, "run_0123456789abcdef0123", "compose", "verify", "content", "Delivered report\n", "pass", base_run);
    schema_cases();
    chain_cases(); admission_cases(); evidence_cases(); bound_cases(); integrity_cases(); source_cases();
    json_object_put(plan); json_object_put(policy); json_object_put(base_compiled);
    assert(!f_remove_tree(root));
    puts("plan expansion admission, bounds, evidence and integrity checks passed"); return 0;
}
