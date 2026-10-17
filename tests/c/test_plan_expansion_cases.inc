/* Cases for test_plan_expansion.c: each group works on the chain selected by use_chain. */
static unsigned chains;
/* Compiles the fixture plan under the given expansion bounds, fabricates its
 * succeeded base run and selects an empty expansions directory for it. */
static void use_chain(const char *name, const char *bounds) {
    json_object *plan = plan_read("tests/fixtures/plan/plan.json"), *policy = plan_read("tests/fixtures/plan/policy.json");
    char id[64], directory[F_PATH];
    if (bounds) json_object_object_add(policy, "expansion", f_parse(bounds));
    json_object_put(base_compiled); base_compiled = compile(plan, policy, NULL); assert(!plan_digest(base_compiled, base_sha));
    snprintf(id, sizeof(id), "run_%020x", chains += 16);
    fabricate_run(base_compiled, id, "compose", "verify", "content", "Delivered report\n", "pass", base_run);
    snprintf(directory, sizeof(directory), "expansions-%s", name); make_dir(root, directory, expansions);
    json_object_put(plan); json_object_put(policy);
}
static json_object *variant(const char *base, const char *old, const char *replacement) {
    char text[4096], out[4096]; const char *from = text; size_t used = 0, n = strlen(old);
    snprintf(text, sizeof(text), proposal_text, base);
    while (*from) {
        if (!strncmp(from, old, n)) { used += (size_t)snprintf(out + used, sizeof(out) - used, "%s", replacement); from += n; }
        else out[used++] = *from++;
        assert(used < sizeof(out) - 1);
    }
    out[used] = '\0'; return f_parse(out);
}
/* Borrows one member of a temporary document and releases the rest. */
static json_object *take(json_object *parent, const char *key) {
    json_object *out = json_object_get(f_field(parent, key)); json_object_put(parent); return out;
}
static const char *latest_sha(void) {
    static char sha[65]; struct px_chain chain;
    px_chain_load(&chain, expansions, base_run, runs); memcpy(sha, chain.versions[chain.count - 1].sha256, 65); px_chain_free(&chain); return sha;
}
static void chain_errors(size_t count, const char *code) {
    struct px_chain chain; bool found = false;
    px_chain_load(&chain, expansions, base_run, runs);
    assert(chain.count == count);
    for (size_t i = 0; i < json_object_array_length(chain.errors); i++) if (!strcmp(f_string(json_object_array_get_idx(chain.errors, i), "code"), code ? code : "")) found = true;
    if (code && !found) fprintf(stderr, "expected %s: %s\n", code, json_object_to_json_string(chain.errors));
    assert(code ? found : json_object_array_length(chain.errors) == 0);
    px_chain_free(&chain);
}
static void chain_cases(void) {
    struct px_chain chain; json_object *text = NULL, *declaration = NULL; char leftover[F_PATH], other[F_PATH];
    use_chain("main", "{\"max_nodes\":9,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    make_dir(expansions, ".round.leftover", leftover); write_text(leftover, "proposal.json", "{}");
    px_chain_load(&chain, expansions, base_run, runs);
    assert(chain.count == 1 && !json_object_array_length(chain.errors) && !strcmp(chain.versions[0].run_state, "succeeded"));
    assert(!strcmp(chain.base_run_id, "run_00000000000000000010") && !strcmp(chain.versions[0].sha256, base_sha));
    assert(!strcmp(px_deliverable_status(&chain.versions[0], "report", &text, &declaration), "pass"));
    assert(text && !strcmp(json_object_get_string(text), "Delivered report\n") && !strcmp(f_string(declaration, "type"), "file"));
    json_object_put(text);
    assert(!strcmp(px_deliverable_status(&chain.versions[0], "absent", NULL, NULL), "unknown_deliverable"));
    assert(!strcmp(px_deliverable_effects(f_field(chain.versions[0].compiled, "plan"), "report"), "artifact_only"));
    assert(px_work_seconds(f_field(chain.versions[0].compiled, "plan")) == 60);
    px_chain_free(&chain);
    /* A base run whose policy declares no bounds authorizes nothing. */
    { json_object *plan = plan_read("tests/fixtures/plan/plan.json"), *policy = plan_read("tests/fixtures/plan/policy.json"), *compiled = compile(plan, policy, NULL);
      char run[F_PATH], none[F_PATH];
      fabricate_run(compiled, "run_00000000000000000002", "compose", "verify", "content", "Delivered report\n", "pass", run);
      make_dir(root, "expansions-unauthorized", none);
      px_chain_load(&chain, none, run, runs);
      assert(chain.count == 1 && !strcmp(f_string(json_object_array_get_idx(chain.errors, 0), "code"), "expansion_not_authorized"));
      px_chain_free(&chain); json_object_put(plan); json_object_put(policy); json_object_put(compiled); }
    assert(!f_path(other, sizeof(other), expansions, ".round.leftover"));
}
/* A fresh claim "third" with its own records, depending on the base report. */
static json_object *third_proposal(const char *sha, const char *criterion) {
    json_object *p = variant(sha, "report2", "report3"), *q;
    json_object_object_add(p, "requirements", take(variant(sha, "\"second\"", "\"third\""), "requirements"));
    json_object_object_add(p, "checks", take(variant(sha, "check2", "check3"), "checks"));
    json_object_object_add(p, "claims", f_parse_value("[{\"id\":\"third\",\"kind\":\"new\"}]"));
    q = json_object_array_get_idx(f_field(p, "requirements"), 0);
    f_string_add(q, "criterion", criterion); f_string_add(q, "deliverable", "report3"); f_string_add(q, "check", "check3");
    f_string_add(json_object_array_get_idx(f_field(p, "checks"), 0), "deliverable", "report3");
    return p;
}
static void admission_cases(void) {
    json_object *p = proposal(base_sha), *result = admit(p, NULL, NULL, NULL), *record = f_field(f_field(result, "data"), "record");
    char path[F_PATH], *first, *second; const char *sha;
    assert(json_object_get_int(f_field(record, "round")) == 1 && !strcmp(f_string(json_object_array_get_idx(f_field(record, "claims"), 0), "id"), "second"));
    assert(!strcmp(f_string(json_object_array_get_idx(f_field(record, "dependencies"), 0), "effects"), "artifact_only"));
    assert(json_object_get_int(f_field(json_object_array_get_idx(f_field(record, "dependencies"), 0), "version")) == 0);
    assert(json_object_get_int(f_field(f_field(f_field(record, "bounds"), "max_nodes"), "used")) == 6);
    assert(json_object_array_length(f_field(f_field(record, "joins"), "compose2")) == 1);
    assert(f_field(f_field(f_field(f_field(result, "data"), "resolved"), "assets"), "base"));
    assert(!f_path(path, sizeof(path), expansions, "round-1/expansion.json") && !access(path, F_OK));
    sha = f_string(f_field(result, "data"), "compiled_sha256"); assert(task_hex(sha, 64) && !strcmp(sha, latest_sha()));
    chain_errors(2, NULL);
    show_text(&first); show_text(&second); assert(!strcmp(first, second) && strstr(first, "\"integrity\":\"intact\"")); free(first); free(second);
    json_object_put(bind(1, sha, NULL)); json_object_put(bind(1, base_sha, "acceptance_mismatch"));
    json_object_put(p); json_object_put(result);
    /* The same proposal again names a superseded base. */
    p = proposal(base_sha); json_object_put(admit(p, "stale_base", NULL, NULL)); json_object_put(p);
    sha = latest_sha();
    p = proposal(sha); json_object_put(admit(p, "duplicate_id", "id", "report2")); json_object_put(p);
    p = variant(sha, "check2", "check3");
    json_object_object_add(p, "deliverables", take(variant(sha, "report2", "report3"), "deliverables"));
    json_object_put(admit(p, "duplicate_id", "id", "second")); json_object_put(p);
    /* Restating the base claim as new is refused; so is a changed retry; so is retrying verified work. */
    p = third_proposal(sha, "Report has the expected contents");
    json_object_put(admit(p, "claim_restated", "earlier_requirement", "content"));
    json_object_object_add(p, "claims", f_parse_value("[{\"id\":\"third\",\"kind\":\"retry\",\"of\":\"content\"}]"));
    json_object_put(admit(p, "claim_changed", "claim", "content"));
    json_object_object_add(json_object_array_get_idx(f_field(p, "checks"), 0), "definition", json_object_new_string("Compare expected text"));
    json_object_put(admit(p, "duplicate_work", "claim", "content"));
    json_object_object_add(p, "claims", f_parse_value("[{\"id\":\"third\",\"kind\":\"retry\",\"of\":\"absent\"}]"));
    json_object_put(admit(p, "unknown_claim", "claim", "absent"));
    json_object_put(p);
}
/* A retry of the round-1 claim: renamed records, same criterion and acceptance rule. */
static json_object *retry_proposal(const char *sha, const char *suffix) {
    char deliverable[80], check[80], claims[256]; json_object *p, *q;
    snprintf(deliverable, sizeof(deliverable), "report-%s", suffix); snprintf(check, sizeof(check), "check-%s", suffix);
    p = variant(sha, "report2", deliverable);
    json_object_object_add(p, "checks", take(variant(sha, "check2", check), "checks"));
    f_string_add(json_object_array_get_idx(f_field(p, "checks"), 0), "deliverable", deliverable);
    q = json_object_array_get_idx(f_field(p, "requirements"), 0);
    f_string_add(q, "check", check); f_string_add(q, "id", suffix);
    snprintf(claims, sizeof(claims), "[{\"id\":\"%s\",\"kind\":\"retry\",\"of\":\"second\"}]", suffix);
    json_object_object_add(p, "claims", f_parse_value(claims));
    return p;
}
/* Fabricates the failed run of round 1 and links it. */
static void fabricate_failed_round(void) {
    char path[F_PATH], run[F_PATH], round[F_PATH], steps[F_PATH], step[F_PATH], digest[65]; json_object *compiled, *record;
    assert(!f_path(round, sizeof(round), expansions, "round-1") && !f_path(path, sizeof(path), round, "compiled.json") && (compiled = plan_read(path)));
    assert(!f_path(path, sizeof(path), round, "expansion.json") && (record = plan_read(path)));
    fabricate_run(compiled, "run_00000000000000000099", "compose2", "verify2", "second", "Different\n", "fail", run);
    make_dir(run, "steps", steps); make_dir(steps, "spawn2", step); write_text(step, "state", "succeeded\n"); write_text(step, "attempts", "1\n");
    write_canonical(run, "plan-expansion.json", record); write_text(run, "state", "failed\n");
    write_text(round, "run-id", "run_00000000000000000099\n");
    assert(!plan_digest(compiled, digest));
    json_object_put(compiled); json_object_put(record);
}
static void evidence_cases(void) {
    json_object *p, *result; char round[F_PATH], digest[65], one[65]; const char *sha;
    memcpy(one, latest_sha(), 65); sha = one;
    set_run_state(base_run, "running\n");
    p = third_proposal(sha, "Third question");
    json_object_put(admit(p, "unknown_outcome", "deliverable", "report"));
    set_run_state(base_run, "succeeded\n");
    /* Round 1 executed and failed: its claim may be retried once, its deliverable is no input. */
    fabricate_failed_round(); chain_errors(2, NULL);
    { struct px_chain chain; px_chain_load(&chain, expansions, base_run, runs);
      assert(!strcmp(chain.versions[1].run_state, "failed") && !strcmp(px_deliverable_status(&chain.versions[1], "report2", NULL, NULL), "fail")); px_chain_free(&chain); }
    json_object_put(bind(1, sha, "round_executed"));
    json_object_object_add(json_object_array_get_idx(f_field(p, "dependencies"), 0), "deliverable", json_object_new_string("report2"));
    json_object_put(admit(p, "dependency_failed", "deliverable", "report2"));
    json_object_object_add(json_object_array_get_idx(f_field(p, "dependencies"), 0), "deliverable", json_object_new_string("missing"));
    json_object_put(admit(p, "unknown_dependency", "deliverable", "missing")); json_object_put(p);
    p = retry_proposal(sha, "second-retry"); result = admit(p, NULL, NULL, NULL);
    assert(json_object_get_int(f_field(json_object_array_get_idx(f_field(f_field(f_field(result, "data"), "record"), "claims"), 0), "repair_attempt")) == 1);
    json_object_put(result); json_object_put(p);
    sha = latest_sha(); json_object_put(bind(2, sha, NULL));
    p = retry_proposal(sha, "second-again"); json_object_put(admit(p, "expansion_bound_exceeded", "bound", "max_repairs_per_claim")); json_object_put(p);
    p = retry_proposal(sha, "second-again"); json_object_object_add(p, "claims", f_parse_value("[{\"id\":\"second-again\",\"kind\":\"retry\",\"of\":\"second-retry\"}]"));
    json_object_put(admit(p, "retry_of_retry", "claim", "second-retry")); json_object_put(p);
    /* A link to a run that was never published means no work started. */
    assert(!f_path(round, sizeof(round), expansions, "round-1"));
    write_text(round, "run-id", "run_00000000000000000077\n");
    { struct px_chain chain; px_chain_load(&chain, expansions, base_run, runs); assert(!strcmp(chain.versions[1].run_state, "absent")); px_chain_free(&chain); }
    result = bind(1, one, NULL); assert(!strcmp(f_string(f_field(result, "data"), "previous_run"), "run_00000000000000000077")); json_object_put(result);
    write_text(round, "run-id", "run_00000000000000000010\n"); chain_errors(3, "run_mismatch");
    write_text(round, "run-id", "run_00000000000000000099\n"); chain_errors(3, NULL);
    /* Bytes that cannot travel as a bundle input make the deliverable ineligible. */
    seal(base_compiled, base_run, "compose", "report", "bad\xff\n", 5, digest);
    seal_check(base_compiled, base_run, "verify", "check", digest, "content", "pass");
    p = third_proposal(latest_sha(), "Third question");
    json_object_put(admit(p, "unsupported_artifact", "deliverable", "report")); json_object_put(p);
    seal(base_compiled, base_run, "compose", "report", "Delivered report\n", 17, digest);
    seal_check(base_compiled, base_run, "verify", "check", digest, "content", "pass");
}
static void bound_cases(void) {
    json_object *p; const char *sha;
    use_chain("work", "{\"max_nodes\":64,\"max_work_seconds\":100,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    p = proposal(base_sha); json_object_put(admit(p, "expansion_bound_exceeded", "bound", "max_work_seconds")); json_object_put(p);
    use_chain("bytes", "{\"max_nodes\":64,\"max_work_seconds\":300,\"max_artifact_bytes\":4096,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    p = proposal(base_sha); json_object_put(admit(p, "expansion_bound_exceeded", "bound", "max_artifact_bytes")); json_object_put(p);
    use_chain("nodes", "{\"max_nodes\":5,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    p = proposal(base_sha); json_object_put(admit(p, "expansion_bound_exceeded", "bound", "max_nodes")); json_object_put(p);
    use_chain("rounds", "{\"max_nodes\":64,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":1,\"max_repairs_per_claim\":0}");
    p = proposal(base_sha); json_object_put(admit(p, NULL, NULL, NULL)); json_object_put(p);
    sha = latest_sha();
    p = third_proposal(sha, "Third question");
    json_object_put(admit(p, "expansion_bound_exceeded", "bound", "max_rounds")); json_object_put(p);
    /* Authority never enlarges: scopes, tools and budgets only shrink. */
    use_chain("authority", "{\"max_nodes\":64,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    p = proposal(base_sha); json_object_object_add(p, "envelope", plan_canonical(f_field(f_field(base_compiled, "plan"), "envelope")));
    json_object_object_add(f_field(p, "envelope"), "tools", f_parse_value("[\"sh\",\"make\"]"));
    json_object_put(admit(p, "authority_enlarged", "path", "tools"));
    json_object_object_add(f_field(p, "envelope"), "tools", f_parse_value("[\"sh\"]")); json_object_object_add(f_field(p, "envelope"), "timeout_seconds", json_object_new_int(181));
    json_object_put(admit(p, "authority_enlarged", "path", "timeout_seconds"));
    json_object_object_add(f_field(p, "envelope"), "timeout_seconds", json_object_new_int(180)); json_object_object_add(f_field(p, "envelope"), "writes", f_parse_value("[\"plan-smoke:*\"]"));
    json_object_put(admit(p, "authority_enlarged", "path", "writes"));
    json_object_object_add(f_field(p, "envelope"), "writes", f_parse_value("[\"plan-expansion:*\"]"));
    json_object_put(admit(p, "invalid_plan", NULL, NULL)); /* the base policy itself allows no write scope */
    json_object_object_add(f_field(p, "envelope"), "writes", json_object_new_array()); json_object_object_add(f_field(p, "envelope"), "disk_mb", json_object_new_int(0));
    json_object_put(admit(p, "authority_enlarged", "path", "disk_mb")); json_object_put(p);
    /* Joins must have members and collect every new node. */
    p = proposal(base_sha); json_object_object_add(p, "joins", f_parse_value("[\"spawn2\"]")); json_object_put(admit(p, "invalid_joins", NULL, NULL)); json_object_put(p);
    p = proposal(base_sha); json_object_array_add(f_field(p, "steps"), f_parse("{\"id\":\"spawn3\",\"role\":\"work\",\"kind\":\"spawn\",\"needs\":[],\"writes\":[],\"args\":{\"branch\":\"plan-other\"}}"));
    json_object_put(admit(p, "invalid_joins", NULL, NULL)); json_object_put(p);
    p = proposal(base_sha); json_object_object_add(json_object_array_get_idx(f_field(p, "steps"), 2), "needs", f_parse_value("[\"absent\"]"));
    json_object_put(admit(p, "invalid_joins", NULL, NULL)); json_object_put(p);
    /* The one compiler refuses a step outside the inherited envelope with its own diagnostics. */
    p = proposal(base_sha); json_object_object_add(f_field(json_object_array_get_idx(f_field(p, "steps"), 2), "args"), "argv", f_parse_value("[\"make\",\"check\"]"));
    { json_object *result = admit(p, "invalid_plan", NULL, NULL);
      assert(!strcmp(f_string(json_object_array_get_idx(f_field(f_field(result, "data"), "diagnostics"), 0), "code"), "unauthorized")); json_object_put(result); }
    json_object_put(p);
}
static void integrity_cases(void) {
    char path[F_PATH], *text, *changed, *first; json_object *p; const char *sha;
    use_chain("integrity", "{\"max_nodes\":64,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    p = proposal(base_sha); json_object_put(admit(p, NULL, NULL, NULL)); json_object_put(p); sha = latest_sha();
    assert(!f_path(path, sizeof(path), expansions, "round-1/compiled.json") && (text = f_read(path, PLAN_LIMIT)));
    changed = malloc(strlen(text) + 16); assert(changed);
    { const char *at = strstr(text, "Compare with base report"); assert(at);
      memcpy(changed, text, (size_t)(at - text)); strcpy(changed + (at - text), "Changed acceptance rule"); strcat(changed, at + strlen("Compare with base report")); }
    unlink(path); assert(!f_write(path, changed, strlen(changed), false));
    chain_errors(2, "expansion_changed");
    show_text(&first); assert(strstr(first, "\"integrity\":\"invalid\"")); free(first);
    json_object_put(bind(1, sha, "invalid_chain"));
    p = proposal(sha); json_object_put(admit(p, "invalid_chain", NULL, NULL)); json_object_put(p);
    unlink(path); assert(!f_write(path, text, strlen(text), false)); free(text); free(changed);
    chain_errors(2, NULL); json_object_put(bind(1, sha, NULL));
    /* Changed dependency bytes invalidate exactly the round that consumed them. */
    { char digest[65];
      seal(base_compiled, base_run, "compose", "report", "Changed report\n", 15, digest);
      seal_check(base_compiled, base_run, "verify", "check", digest, "content", "pass"); }
    show_text(&first); assert(strstr(first, "\"status\":\"changed\"")); free(first);
    json_object_put(bind(1, sha, "dependency_changed"));
    { char digest[65];
      seal(base_compiled, base_run, "compose", "report", "Delivered report\n", 17, digest);
      seal_check(base_compiled, base_run, "verify", "check", digest, "content", "pass"); }
    json_object_put(bind(1, sha, NULL));
}
static void source_cases(void) {
    char command[4 * F_PATH]; json_object *p;
    use_chain("source", "{\"max_nodes\":64,\"max_work_seconds\":300,\"max_artifact_bytes\":16384,\"max_rounds\":3,\"max_repairs_per_claim\":1}");
    snprintf(command, sizeof(command), "cd '%s' && printf 'changed\\n' >> expected.txt", repo); assert(!system(command));
    p = proposal(base_sha); json_object_put(admit(p, "invalid_plan", NULL, NULL)); json_object_put(p);
    snprintf(command, sizeof(command), "cd '%s' && git -c user.name=Test -c user.email=test@example.invalid -c commit.gpgSign=false commit -qam changed", repo); assert(!system(command));
    p = proposal(base_sha); json_object_put(admit(p, "source_changed", NULL, NULL)); json_object_put(p);
}
