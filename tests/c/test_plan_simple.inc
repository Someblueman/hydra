/* The short plan form: lowering is exact and every malformed draft is named. */
static json_object *simple_draft(void) { return plan_read("tests/fixtures/plan-simple/draft.json"); }
static json_object *simple_policy(void) { return plan_read("tests/fixtures/plan-simple/policy.json"); }
static void expect_simple(json_object *draft, json_object *policy, const char *code) {
    json_object *errors = json_object_new_array(), *lowered = plan_simple_lower(draft, policy, errors);
    bool found = false;
    assert(!lowered);
    for (size_t i = 0; i < json_object_array_length(errors); i++)
        if (!strcmp(f_string(json_object_array_get_idx(errors, i), "code"), code)) found = true;
    if (!found) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
    assert(found);
    json_object_put(errors); json_object_put(draft);
}
static void simple_step_set(json_object *draft, size_t index, const char *key, const char *json) {
    json_object *step = json_object_array_get_idx(f_field(draft, "steps"), index);
    if (json) json_object_object_add(step, key, f_parse_value(json));
    else json_object_object_del(step, key);
}
static void simple_lowering_cases(void) {
    json_object *draft = simple_draft(), *policy = simple_policy(), *expected = plan_read("tests/fixtures/plan-simple/lowered.json");
    json_object *errors = json_object_new_array(), *lowered, *two;
    assert(draft && policy && expected && plan_simple(draft) && !plan_simple(expected));
    lowered = plan_simple_lower(draft, policy, errors);
    if (!lowered) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
    /* The lowered plan equals the hand-written schema 1 plan and validates under the guided policy. */
    assert(lowered && json_object_equal(lowered, expected) && !plan_validate(lowered, policy, errors));
    json_object_put(lowered);
    /* plan_lowered passes schema 1 through unchanged. */
    lowered = plan_lowered(expected, policy, errors);
    assert(lowered == expected); json_object_put(lowered);
    json_object_put(errors); json_object_put(expected);

    draft = simple_draft(); json_object_object_add(draft, "envelope", json_object_new_object()); expect_simple(draft, policy, "invalid_plan");
    draft = simple_draft(); json_object_object_del(draft, "done_when"); expect_simple(draft, policy, "invalid_collection");
    draft = simple_draft(); simple_step_set(draft, 0, "run", "[\"true\"]"); expect_simple(draft, policy, "invalid_step");
    draft = simple_draft(); simple_step_set(draft, 1, "id", "\"spawn-worker\""); expect_simple(draft, policy, "invalid_step");
    draft = simple_draft(); simple_step_set(draft, 1, "id", "\"implement\""); expect_simple(draft, policy, "invalid_step");
    draft = simple_draft(); simple_step_set(draft, 0, "needs", "[\"check\"]"); expect_simple(draft, policy, "missing_dependency");
    draft = simple_draft(); simple_step_set(draft, 1, "timeout", "0"); expect_simple(draft, policy, "invalid_step");
    draft = simple_draft(); simple_step_set(draft, 1, "profile", "\"codex\""); expect_simple(draft, policy, "invalid_step");
    /* A check must follow an agent step, since it checks that step's commit. */
    draft = simple_draft(); simple_step_set(draft, 1, "needs", "[]"); expect_simple(draft, policy, "invalid_verification");
    /* done_when names a run step. */
    draft = simple_draft();
    json_object_object_add(json_object_array_get_idx(f_field(draft, "done_when"), 0), "check", json_object_new_string("implement"));
    expect_simple(draft, policy, "invalid_record");
    /* Without one authorized profile the agent step must name its own. */
    two = simple_policy();
    json_object_array_add(f_field(f_field(two, "envelope"), "tools"), json_object_new_string("profile:claude"));
    draft = simple_draft(); expect_simple(draft, two, "unsupported_profile");
    draft = simple_draft(); simple_step_set(draft, 0, "profile", "\"claude\"");
    errors = json_object_new_array(); lowered = plan_simple_lower(draft, two, errors);
    assert(lowered && !strcmp(f_string(f_field(json_object_array_get_idx(f_field(lowered, "steps"), 1), "args"), "profile"), "claude"));
    json_object_put(lowered); json_object_put(errors); json_object_put(draft); json_object_put(two);
    /* A tool the policy lacks is reported by validation of the lowered plan. */
    draft = simple_draft(); simple_step_set(draft, 1, "run", "[\"npm\",\"test\"]");
    errors = json_object_new_array(); lowered = plan_simple_lower(draft, policy, errors);
    assert(lowered && plan_validate(lowered, policy, errors));
    json_object_put(lowered); json_object_put(errors); json_object_put(draft);
    /* An explicit id and worker are kept; the shape check needs no policy. */
    draft = simple_draft();
    json_object_object_add(draft, "id", json_object_new_string("greeting"));
    json_object_object_add(draft, "worker", json_object_new_string("greeting-worker"));
    errors = json_object_new_array(); lowered = plan_simple_lower(draft, NULL, errors);
    assert(lowered && !strcmp(f_string(lowered, "id"), "greeting") &&
           !strcmp(f_string(f_field(json_object_array_get_idx(f_field(lowered, "steps"), 0), "args"), "branch"), "greeting-worker"));
    json_object_put(lowered); json_object_put(errors); json_object_put(draft);
    json_object_put(policy);
}
/* A command check is schema 1 only and consumes an agent step's commit on its own head. */
static void command_check_cases(void) {
    json_object *policy = simple_policy(), *plan = plan_read("tests/fixtures/plan-simple/lowered.json"), *check;
    assert(plan && policy);
    expect(plan, policy, NULL);
    check = json_object_array_get_idx(f_field(plan, "checks"), 0);
    json_object_object_add(f_field(f_field(f_field(f_field(plan, "data"), "steps"), "check"), "inputs"), "subject",
                           f_parse_value("{\"step\":\"implement\",\"output\":\"summary\"}"));
    json_object_object_add(json_object_array_get_idx(f_field(plan, "deliverables"), 0), "output", json_object_new_string("summary"));
    expect(plan, policy, "invalid_verification");
    f_string_add(check, "method", "executable");
    expect(plan, policy, NULL);
    json_object_put(plan); json_object_put(policy);
}
/* The in-process SHA-256 behind plan digests and the run guard (FIPS 180-4 vectors). */
static void sha256_cases(void) {
    char digest[65], *million = malloc(1000000);
    struct f_sha256 sha;
    f_sha256_text("", 0, digest); assert(!strcmp(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    f_sha256_text("abc", 3, digest); assert(!strcmp(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    f_sha256_text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, digest);
    assert(!strcmp(digest, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    assert(million); memset(million, 'a', 1000000);
    f_sha256_init(&sha); f_sha256_update(&sha, million, 1); f_sha256_update(&sha, million + 1, 999999); f_sha256_final(&sha, digest);
    assert(!strcmp(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    free(million);
}
