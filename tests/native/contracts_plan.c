#include "contracts.h"
void hc_bind(json_object *p) {
  json_object *d = f_field(p, "data"), *steps = f_field(d, "steps");
  json_object_object_foreach(steps, key, step) {
    (void)key;
    json_object *inputs = f_field(step, "inputs");
    if (!inputs)
      continue;
    json_object_object_foreach(inputs, name, ref) {
      (void)name;
      if (f_field(ref, "provenance"))
        continue;
      json_object *decl =
          f_field(ref, "input")
              ? f_field(f_field(d, "inputs"), f_string(ref, "input"))
              : f_field(f_field(hc_step(d, f_string(ref, "step")), "outputs"),
                        f_string(ref, "output"));
      assert(decl);
      json_object_object_add(ref, "contract",
                             nt_clone(f_field(decl, "contract")));
    }
  }
}
static void add_contracts(json_object *declarations) {
  json_object_object_foreach(declarations, key, decl) {
    (void)key;
    bool file = !strcmp(f_string(decl, "type"), "file");
    json_object *fields =
        file ? json_object_new_object()
             : f_parse("{\"schema_version\":{\"type\":\"integer\",\"equals\":1}"
                       ",\"verdict\":{\"type\":\"string\"},\"subject_sha256\":{"
                       "\"type\":\"string\"},\"requirements\":{\"type\":"
                       "\"strings\",\"equals\":[\"content\"]},\"evidence\":{"
                       "\"type\":\"string\"}}");
    json_object_object_add(decl, "contract",
                           hc_contract(NULL, f_string(decl, "type"),
                                       file ? "bytes" : "report", fields));
  }
}
void hc_plan(json_object **p, json_object **policy) {
  char source[F_PATH];
  nt_path(source, hc.origin, "tests/fixtures/plan/repo/.");
  char *cp[] = {"cp", "-R", source, hc.repo, NULL};
  struct f_capture c = hc_command(cp, true);
  f_capture_free(&c);
  char *add[] = {"git", "add", ".", NULL},
       *commit[] = {"git",     "-c", "commit.gpgSign=false", "commit", "-qm",
                    "fixture", NULL};
  c = hc_command(add, true);
  f_capture_free(&c);
  c = hc_command(commit, true);
  f_capture_free(&c);
  *p = hc_read(hc.origin, "tests/fixtures/plan/plan.json");
  *policy = hc_read(hc.origin, "tests/fixtures/plan/policy.json");
  json_object *d = f_field(*p, "data");
  json_object_object_add(d, "schema_version", json_object_new_int(2));
  add_contracts(f_field(d, "inputs"));
  json_object_object_foreach(f_field(d, "steps"), key, step) {
    (void)key;
    if (f_field(step, "outputs"))
      add_contracts(f_field(step, "outputs"));
  }
  hc_bind(*p);
}
json_object *hc_compile(json_object *p, json_object *policy, bool ok) {
  char pp[F_PATH], pol[F_PATH], out[F_PATH];
  nt_path(pp, hc.root, "plan.json");
  nt_path(pol, hc.root, "policy.json");
  nt_path(out, hc.root, "compiled.json");
  nt_json_write(pp, p);
  nt_json_write(pol, policy);
  char *args[] = {hc.hydra, "workflow", "plan", "compile", pp, pol, out, NULL};
  struct f_capture c = hc_command(args, ok);
  json_object *v = f_parse(c.out);
  assert(v);
  if (!ok)
    assert(!strcmp(f_string(f_field(v, "error"), "code"), "invalid_plan"));
  f_capture_free(&c);
  return v;
}
static void compatible(void) {
  json_object *p, *policy;
  hc_plan(&p, &policy);
  json_object_put(hc_compile(p, policy, true));
  char *args[] = {hc.hydra, "workflow", "plan", "schema", NULL};
  struct f_capture c = hc_command(args, true);
  json_object *schema = f_parse(c.out);
  assert(schema && f_field(f_field(schema, "$defs"), "data2"));
  assert(f_field(f_field(json_object_array_get_idx(f_field(schema, "allOf"), 0),
                         "properties"),
                 "relations"));
  json_object *types = f_field(
      f_field(
          f_field(f_field(f_field(f_field(f_field(schema, "$defs"), "contract"),
                                  "properties"),
                          "fields"),
                  "additionalProperties"),
          "properties"),
      "type");
  bool found = false;
  json_object *values = f_field(types, "enum");
  for (size_t i = 0; i < json_object_array_length(values); i++)
    if (!strcmp(json_object_get_string(json_object_array_get_idx(values, i)),
                "strings"))
      found = true;
  assert(found);
  json_object_put(schema);
  f_capture_free(&c);
  json_object *compiled = hc_read(hc.root, "compiled.json");
  assert(json_object_equal(f_field(f_field(compiled, "plan"), "data"),
                           f_field(p, "data")));
  json_object_put(compiled);
  json_object_put(p);
  json_object_put(policy);
}
static void incompatible(void) {
  json_object *p, *policy;
  hc_plan(&p, &policy);
  json_object_object_add(
      f_field(f_field(f_field(hc_step(f_field(p, "data"), "verify"), "inputs"),
                      "subject"),
              "contract"),
      "version", json_object_new_int(2));
  json_object_put(hc_compile(p, policy, false));
  json_object_put(p);
  json_object_put(policy);
}
static void relations(void) {
  json_object *p, *policy;
  hc_plan(&p, &policy);
  json_object *relations = json_object_new_array();
  const char *types[] = {"data",   "evidence", "order",
                         "effect", "resource", "provenance"};
  for (size_t i = 0; i < 6; i++) {
    json_object *r = json_object_new_object();
    f_string_add(r, "type", types[i]);
    f_string_add(r, "from", i < 4 ? "compose" : "verify");
    f_string_add(r, "to", i < 4 ? "verify" : "spawn");
    f_string_add(r, "enforcement", i < 4 ? "dependency" : "descriptive");
    json_object_array_add(relations, r);
  }
  json_object_object_add(p, "relations", relations);
  json_object_put(hc_compile(p, policy, true));
  char out[F_PATH];
  nt_path(out, hc.root, "compiled.json");
  assert(!unlink(out));
  f_string_add(json_object_array_get_idx(relations, 4), "enforcement", "mutex");
  json_object_put(hc_compile(p, policy, false));
  json_object_put(p);
  json_object_put(policy);
}
static void candidate_plan(json_object **p, json_object **policy) {
  hc_plan(p, policy);
  json_object
      *d = f_field(*p, "data"),
      *fields = f_parse(
          "{\"source_commit\":{\"type\":\"string\"},\"source_sha256\":{"
          "\"type\":\"string\"},\"config_sha256\":{\"type\":\"string\"}}");
  json_object_object_add(
      f_field(hc_step(d, "compose"), "outputs"), "report",
      hc_declaration(hc_contract(NULL, NULL, "candidate", fields),
                     "report.txt"));
  json_object_object_add(f_field(hc_step(d, "verify"), "inputs"), "source",
                         f_parse("{\"provenance\":\"plan\"}"));
  json_object_object_add(
      hc_step(d, "verify"), "candidates",
      f_parse_value("[{\"phase\":\"before\",\"manifest\":\"subject\","
                    "\"source\":\"source\",\"bindings\":{\"config_sha256\":{"
                    "\"kind\":\"configuration\",\"input\":\"expected\"}}}]"));
  json_object_object_add(f_field(*p, "envelope"), "artifact_bytes",
                         json_object_new_int(8192));
  json_object_object_add(f_field(*policy, "envelope"), "artifact_bytes",
                         json_object_new_int(8192));
  hc_bind(*p);
}
static json_object *accept_run(json_object *p, json_object *policy,
                               bool simple) {
  json_object *response = hc_compile(p, policy, true),
              *compiled = hc_read(hc.root, "compiled.json");
  hc_json(hc.run, "compiled.json", compiled);
  hc_write(hc.run, "plan-accepted",
           f_string(f_field(response, "data"), "sha256"));
  json_object_put(response);
  FILE *f = fopen(hc.graph, "w");
  assert(f);
  if (simple)
    assert(fputs("step\tcompose\texec\t-\nstep\tverify\texec\tcompose\n", f) >=
           0);
  else {
    json_object *steps = f_field(p, "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
      json_object *step = json_object_array_get_idx(steps, i),
                  *needs = f_field(step, "needs");
      assert(fprintf(f, "step\t%s\t%s\t", f_string(step, "id"),
                     f_string(step, "kind")) > 0);
      if (!json_object_array_length(needs))
        assert(fputc('-', f) != EOF);
      for (size_t j = 0; j < json_object_array_length(needs); j++)
        assert(fprintf(f, "%s%s", j ? "," : "",
                       json_object_get_string(
                           json_object_array_get_idx(needs, j))) > 0);
      assert(fputc('\n', f) != EOF);
    }
  }
  assert(!fclose(f));
  char *rows = f_read(hc.graph, 1000000);
  assert(rows);
  hc_write(hc.run, "graph.tsv", rows);
  free(rows);
  hc_initialize(f_field(compiled, "data"));
  return compiled;
}
static void candidate(bool stale) {
  json_object *p, *policy;
  candidate_plan(&p, &policy);
  json_object *compiled = accept_run(p, policy, false);
  char producer[F_PATH], consumer[F_PATH], path[F_PATH], digest[65];
  hc_prepare("compose", producer, true);
  json_object *source = f_field(compiled, "source"),
              *payload = json_object_new_object();
  f_string_add(payload, "source_commit",
               stale ? "0000000000000000000000000000000000000000"
                     : f_string(source, "commit"));
  f_string_add(payload, "source_sha256", f_string(source, "sha256"));
  nt_path(path, hc.repo, "expected.txt");
  assert(!f_hash(path, digest));
  f_string_add(payload, "config_sha256", digest);
  hc_json(producer, "outputs/report.txt", payload);
  json_object_put(payload);
  hc_seal("compose", producer, true);
  hc_prepare("verify", consumer, !stale);
  json_object_put(compiled);
  json_object_put(p);
  json_object_put(policy);
}
static void bound_candidate(void) { candidate(false); }
static void stale_candidate(void) { candidate(true); }
static void missing_coverage(void) {
  json_object *p, *policy;
  candidate_plan(&p, &policy);
  json_object_object_del(
      f_field(
          json_object_array_get_idx(
              f_field(hc_step(f_field(p, "data"), "verify"), "candidates"), 0),
          "bindings"),
      "config_sha256");
  json_object_put(hc_compile(p, policy, false));
  json_object_put(p);
  json_object_put(policy);
}
static void composed(void) {
  json_object *p, *policy;
  candidate_plan(&p, &policy);
  json_object *d = f_field(p, "data"), *produce = hc_step(d, "compose"),
              *consume = hc_step(d, "verify");
  json_object_object_add(produce, "inputs",
                         f_parse("{\"source\":{\"provenance\":\"plan\"},"
                                 "\"expected\":{\"input\":\"expected\"}}"));
  json_object_object_add(f_field(produce, "outputs"), "program",
                         hc_declaration(hc_contract(NULL, "file", "bytes",
                                                    json_object_new_object()),
                                        "program.bin"));
  json_object_object_add(
      f_field(
          f_field(f_field(f_field(produce, "outputs"), "report"), "contract"),
          "fields"),
      "program_sha256", f_parse("{\"type\":\"string\"}"));
  json_object_object_add(
      produce, "candidates",
      f_parse_value(
          "[{\"phase\":\"after\",\"manifest\":\"report\",\"source\":\"source\","
          "\"bindings\":{\"config_sha256\":{\"kind\":\"configuration\","
          "\"input\":\"expected\"},\"program_sha256\":{\"kind\":\"artifact\","
          "\"output\":\"program\"}}}]"));
  json_object_object_add(
      f_field(consume, "inputs"), "program",
      f_parse("{\"step\":\"compose\",\"output\":\"program\"}"));
  json_object_object_add(
      f_field(json_object_array_get_idx(f_field(consume, "candidates"), 0),
              "bindings"),
      "program_sha256",
      f_parse("{\"kind\":\"artifact\",\"input\":\"program\"}"));
  json_object_object_add(f_field(p, "envelope"), "artifact_bytes",
                         json_object_new_int(16384));
  json_object_object_add(f_field(policy, "envelope"), "artifact_bytes",
                         json_object_new_int(16384));
  hc_bind(p);
  json_object *compiled = accept_run(p, policy, true);
  char attempt[F_PATH], consumer[F_PATH], path[F_PATH], digest[65];
  hc_prepare("compose", attempt, true);
  json_object *source = hc_read(attempt, "inputs/source");
  hc_write(attempt, "outputs/program.bin", "candidate program");
  nt_path(path, attempt, "inputs/expected");
  assert(!f_hash(path, digest));
  f_string_add(source, "config_sha256", digest);
  nt_path(path, attempt, "outputs/program.bin");
  assert(!f_hash(path, digest));
  f_string_add(source, "program_sha256", digest);
  hc_json(attempt, "outputs/report.txt", source);
  json_object_put(source);
  hc_seal("compose", attempt, true);
  hc_prepare("verify", consumer, true);
  json_object_put(compiled);
  json_object_put(p);
  json_object_put(policy);
}
void hc_plan_cases(void) {
  void (*cases[])(void) = {compatible,      incompatible,    relations,
                           bound_candidate, stale_candidate, missing_coverage,
                           composed};
  for (size_t i = 0; i < 7; i++) {
    hc_setup();
    cases[i]();
    hc_cleanup();
    printf("Handoff plan case %zu passed\n", i + 1);
  }
}
