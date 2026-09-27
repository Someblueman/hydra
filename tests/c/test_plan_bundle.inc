/* Plans that carry their own inputs: inline agent prompts and proposal assets.
 * Included by test_plan.c, which provides fixture() and expect(). */
#include <sys/stat.h>

static const char *bundle_prompt = "Write the report.\nDo not commit.";
static json_object *agent_plan(void) {
    json_object *plan = fixture(), *compose = json_object_array_get_idx(f_field(plan, "steps"), 1);
    json_object *outputs = f_field(f_field(f_field(f_field(plan, "data"), "steps"), "compose"), "outputs");
    json_object *args = json_object_new_object();
    f_string_add(args, "head", "plan-smoke"); f_string_add(args, "profile", "codex"); f_string_add(args, "prompt", bundle_prompt);
    f_string_add(args, "result_file", "report"); json_object_object_add(args, "timeout", json_object_new_int(30));
    json_object_object_add(compose, "args", args);
    json_object_object_add(outputs, "report", f_parse_value("{\"type\":\"file\",\"path\":\"report\",\"max_bytes\":1024}"));
    json_object_object_add(f_field(plan, "envelope"), "tools", f_parse_value("[\"sh\",\"profile:codex\"]"));
    return plan;
}
static json_object *agent_policy(void) {
    json_object *policy = plan_read("tests/fixtures/plan/policy.json");
    json_object_object_add(f_field(policy, "envelope"), "tools", f_parse_value("[\"sh\",\"profile:codex\"]"));
    return policy;
}
static json_object *compose_args(json_object *plan) { return f_field(json_object_array_get_idx(f_field(plan, "steps"), 1), "args"); }
/* Adds the asset input "verifier" and runs it from the verify step as @input/verifier. */
static void add_verifier(json_object *plan, const char *argument) {
    json_object *data = f_field(plan, "data"), *verify = json_object_array_get_idx(f_field(plan, "steps"), 2), *argv = json_object_new_array();
    json_object_object_add(f_field(data, "inputs"), "verifier", f_parse_value("{\"asset\":\"verifier\",\"type\":\"file\",\"max_bytes\":64}"));
    json_object_object_add(f_field(f_field(f_field(data, "steps"), "verify"), "inputs"), "verifier", f_parse_value("{\"input\":\"verifier\"}"));
    json_object_array_add(argv, json_object_new_string("sh")); json_object_array_add(argv, json_object_new_string(argument));
    json_object_object_add(f_field(verify, "args"), "argv", argv);
}
static void inline_prompt_cases(void) {
    json_object *plan = agent_plan(), *policy = agent_policy(), *args = compose_args(plan);
    char *large = malloc(PLAN_PROMPT_LIMIT + 2);
    assert(plan && policy && large); expect(plan, policy, NULL);
    memset(large, 'a', PLAN_PROMPT_LIMIT + 1); large[PLAN_PROMPT_LIMIT] = '\0';
    f_string_add(args, "prompt", large); expect(plan, policy, NULL);
    large[PLAN_PROMPT_LIMIT] = 'a'; large[PLAN_PROMPT_LIMIT + 1] = '\0';
    f_string_add(args, "prompt", large); expect(plan, policy, "invalid_step");
    f_string_add(args, "prompt", ""); expect(plan, policy, "invalid_step");
    json_object_object_add(args, "prompt", json_object_new_string_len("a\0b", 3)); expect(plan, policy, "invalid_step");
    json_object_object_add(args, "prompt", json_object_new_int(1)); expect(plan, policy, "invalid_step");
    f_string_add(args, "prompt", bundle_prompt); f_string_add(args, "prompt_input", "expected"); expect(plan, policy, "invalid_step");
    json_object_object_del(args, "prompt"); json_object_object_del(args, "prompt_input"); expect(plan, policy, "invalid_step");
    f_string_add(args, "prompt", bundle_prompt); json_object_object_add(args, "argv", f_parse_value("[\"sh\"]")); expect(plan, policy, "invalid_step");
    free(large); json_object_put(plan); json_object_put(policy);
}
static void expect_expand(json_object *plan, json_object *assets, const char *code) {
    json_object *files = json_object_new_object(), *errors = json_object_new_array(), *expanded = plan_expand(plan, assets, files, errors);
    assert(!expanded && json_object_array_length(errors));
    if (strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), code)) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
    assert(!strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), code));
    json_object_put(files); json_object_put(errors);
}
static void expanded_shape(json_object *expanded, json_object *files) {
    json_object *inputs = f_field(f_field(expanded, "data"), "inputs"), *asset = f_field(inputs, "verifier"), *prompt = f_field(inputs, "prompt-compose");
    assert(!f_field(asset, "asset") && !strcmp(f_string(asset, "path"), "assets/verifier") && !strcmp(f_string(asset, "source"), "bundle"));
    assert(!strcmp(f_string(prompt, "path"), "prompts/compose") && !strcmp(f_string(prompt, "source"), "bundle"));
    assert(json_object_get_int64(f_field(prompt, "max_bytes")) == (int64_t)strlen(bundle_prompt));
    assert(!f_field(compose_args(expanded), "prompt") && !strcmp(f_string(compose_args(expanded), "prompt_input"), "prompt-compose"));
    assert(f_field(f_field(f_field(f_field(f_field(expanded, "data"), "steps"), "compose"), "inputs"), "prompt-compose"));
    assert(!strcmp(f_string(files, "assets/verifier"), "echo ok\n") && !strcmp(f_string(files, "prompts/compose"), bundle_prompt));
}
static void expand_cases(void) {
    json_object *plan = agent_plan(), *files = json_object_new_object(), *errors = json_object_new_array();
    json_object *assets = f_parse("{\"verifier\":\"echo ok\\n\"}"), *empty = json_object_new_object(), *expanded;
    add_verifier(plan, "@input/verifier");
    expanded = plan_expand(plan, assets, files, errors);
    assert(expanded && !json_object_array_length(errors)); expanded_shape(expanded, files);
    assert(!strcmp(f_string(compose_args(plan), "prompt"), bundle_prompt));
    expect_expand(plan, empty, "missing_asset");
    add_verifier(plan, "@input/absent"); expect_expand(plan, assets, "invalid_input_reference");
    add_verifier(plan, "@input/verifier");
    json_object_object_add(f_field(f_field(plan, "data"), "inputs"), "prompt-compose", f_parse_value("{\"path\":\"expected.txt\",\"type\":\"file\",\"max_bytes\":128}"));
    expect_expand(plan, assets, "prompt_conflict");
    json_object_object_del(f_field(f_field(plan, "data"), "inputs"), "prompt-compose");
    json_object_object_add(f_field(f_field(plan, "data"), "inputs"), "verifier", f_parse_value("{\"asset\":\"verifier\",\"path\":\"x\",\"type\":\"file\",\"max_bytes\":64}"));
    expect_expand(plan, assets, "invalid_asset");
    json_object_put(expanded); json_object_put(files); json_object_put(errors); json_object_put(assets); json_object_put(empty); json_object_put(plan);
}
static void expect_asset_load(json_object *plan, const char *directory, const char *code) {
    json_object *errors = json_object_new_array(), *assets = plan_assets_load(plan, directory, errors);
    if (code) assert(json_object_array_length(errors) && !strcmp(f_string(json_object_array_get_idx(errors, 0), "code"), code) && !f_field(assets, "verifier"));
    else assert(!json_object_array_length(errors) && !strcmp(f_string(assets, "verifier"), "echo ok\n"));
    json_object_put(errors); json_object_put(assets);
}
static void expect_proposal_asset(const char *name, const char *file, const char *root, bool accepted) {
    json_object *result = plan_proposal_asset(name, file, root);
    assert(accepted ? !f_field(result, "error") : f_field(result, "error") != NULL);
    json_object_put(result);
}
/* Absent, valid, symlinked, non-UTF-8, oversized and maximum-size asset files. */
static void asset_directory_cases(json_object *plan, const char *directory, const char *path, const char *real, const char *root) {
    json_object *errors = json_object_new_array(), *assets; char *large = calloc(1, PLAN_ASSET_LIMIT + 1);
    assert(large);
    assets = plan_assets_load(plan, directory, errors); assert(!json_object_array_length(errors) && !f_field(assets, "verifier")); json_object_put(assets);
    assert(!f_write(path, "echo ok\n", 8, true)); expect_asset_load(plan, directory, NULL);
    assert(!unlink(path) && !f_write(real, "echo ok\n", 8, true) && !symlink(real, path)); expect_asset_load(plan, directory, "invalid_asset");
    expect_proposal_asset("verifier", path, root, false);
    assert(!unlink(path) && !f_write(path, "\xff\xfe", 2, true)); expect_asset_load(plan, directory, "invalid_asset");
    memset(large, 'a', PLAN_ASSET_LIMIT); assert(!f_write(path, large, PLAN_ASSET_LIMIT + 1, true)); expect_asset_load(plan, directory, "invalid_asset");
    assert(!f_write(path, large, PLAN_ASSET_LIMIT, true)); assets = plan_assets_load(plan, directory, errors);
    assert(!json_object_array_length(errors) && json_object_get_string_len(f_field(assets, "verifier")) == (int)PLAN_ASSET_LIMIT);
    json_object_put(assets); json_object_put(errors); free(large);
}
static void asset_file_cases(const char *root) {
    char directory[F_PATH], path[F_PATH], real[F_PATH]; json_object *plan = agent_plan();
    assert(!f_path(directory, sizeof(directory), root, "assets") && !mkdir(directory, 0700));
    assert(!f_path(path, sizeof(path), directory, "verifier") && !f_path(real, sizeof(real), root, "real-verifier"));
    add_verifier(plan, "@input/verifier");
    asset_directory_cases(plan, directory, path, real, root);
    expect_proposal_asset("Bad Name", real, root, false);
    expect_proposal_asset("copied", real, root, true);
    expect_proposal_asset("copied", real, root, false);
    json_object_put(plan);
}
static void bundle_repository(const char *root, char repo[F_PATH]) {
    char path[F_PATH], command[2 * F_PATH];
    assert(!f_path(repo, F_PATH, root, "repo") && !mkdir(repo, 0700) && !f_path(path, sizeof(path), repo, "expected.txt"));
    assert(!f_write(path, "Delivered report\n", 17, false));
    snprintf(command, sizeof(command), "cd '%s' && git init -q && git add expected.txt && "
        "git -c user.name=Test -c user.email=test@example.invalid -c commit.gpgSign=false commit -qm fixture", repo);
    assert(!system(command));
}
static json_object *bundle_compile(json_object *plan, json_object *policy, const char *repo, json_object *assets, char digest[65]) {
    json_object *errors = json_object_new_array(), *compiled = plan_compile(plan, policy, repo, assets, errors);
    if (!compiled) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
    assert(compiled && !plan_digest(compiled, digest));
    json_object_put(errors); return compiled;
}
static void materialized_bundle(json_object *compiled, const char *root) {
    char directory[F_PATH], path[F_PATH]; char *text;
    assert(!f_path(directory, sizeof(directory), root, "stage") && !mkdir(directory, 0700) && !plan_materialize(compiled, directory));
    assert(!f_path(path, sizeof(path), directory, "bundle/prompts/compose") && (text = f_read(path, 1024)) && !strcmp(text, bundle_prompt)); free(text);
    assert(!f_path(path, sizeof(path), directory, "bundle/assets/verifier") && (text = f_read(path, 1024)) && !strcmp(text, "echo one\n")); free(text);
}
static void digest_cases(const char *root) {
    char repo[F_PATH], one[65], again[65], two[65], prompt[65], legacy_digest[65];
    json_object *plan = agent_plan(), *policy = agent_policy(), *first = f_parse("{\"verifier\":\"echo one\\n\"}"), *second = f_parse("{\"verifier\":\"echo two\\n\"}");
    json_object *a, *b, *c, *d, *legacy, *legacy_plan = fixture(), *legacy_policy = plan_read("tests/fixtures/plan/policy.json");
    bundle_repository(root, repo); add_verifier(plan, "@input/verifier");
    a = bundle_compile(plan, policy, repo, first, one); b = bundle_compile(plan, policy, repo, first, again);
    assert(!strcmp(one, again) && !strcmp(f_string(f_field(a, "assets"), "verifier"), "echo one\n"));
    c = bundle_compile(plan, policy, repo, second, two); assert(strcmp(one, two));
    f_string_add(compose_args(plan), "prompt", "Write a different report.");
    d = bundle_compile(plan, policy, repo, first, prompt); assert(strcmp(one, prompt) && strcmp(two, prompt));
    assert(!plan_admit(a, repo, one) && plan_admit(c, repo, one) && !plan_admit(c, repo, two));
    materialized_bundle(a, root);
    legacy = bundle_compile(legacy_plan, legacy_policy, repo, NULL, legacy_digest); assert(!f_field(legacy, "assets"));
    json_object_put(a); json_object_put(b); json_object_put(c); json_object_put(d); json_object_put(legacy);
    json_object_put(first); json_object_put(second); json_object_put(plan); json_object_put(policy); json_object_put(legacy_plan); json_object_put(legacy_policy);
}
static void bundle_cases(const char *root) {
    assert(plan_utf8("caf\xc3\xa9", 5) && !plan_utf8("\xc3", 1) && !plan_utf8("a\0b", 3) && !plan_utf8("\xed\xa0\x80", 3));
    inline_prompt_cases(); expand_cases(); asset_file_cases(root); digest_cases(root);
}
