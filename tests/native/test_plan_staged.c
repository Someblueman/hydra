#include "outcome_support.h"
static struct outcome_fixture f;
static char public_file[F_PATH], finding[F_PATH], planpath[F_PATH];
static void bind_result(void) {
  char hash[65];
  assert(!f_hash(finding, hash));
  json_object *v = f_parse(
      "{\"ok\":true,\"data\":{\"schema_version\":1,\"plan_sha256\":"
      "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
      "\"verdict\":\"pass\",\"deliverables\":{\"report\":{\"type\":\"file\"}},"
      "\"checks\":{\"check\":{\"verdict\":\"pass\"}}}}");
  json_object *report =
      f_field(f_field(f_field(v, "data"), "deliverables"), "report");
  f_string_add(report, "path", finding);
  f_string_add(report, "sha256", hash);
  nt_json_write(public_file, v);
  json_object_put(v);
}
static void prepare(json_object *items) {
  if (items) {
    json_object *v = f_parse("{\"schema_version\":1}");
    json_object_object_add(v, "items", nt_clone(items));
    nt_json_write("manifest.json", v);
    json_object_put(v);
  }
  char path[F_PATH];
  nt_path(path, f.inputs, "manifest");
  nt_copy_tree("manifest.json", path);
  assert(!setenv("HYDRA_WORKFLOW_OUTPUTS_DIR", f.repo, 1));
  outcome_check("staged-produce", 0);
  assert(!setenv("HYDRA_WORKFLOW_OUTPUTS_DIR", f.outputs, 1));
  bind_result();
}
static json_object *precompile(const char *error) {
  nt_write(planpath, "prior plan");
  char *argv[] = {f.precompiler, "staged", "manifest.json",
                  "run_fixture", planpath, NULL};
  struct f_capture c = nt_run(argv);
  if (error) {
    if (c.status != 1 || !strstr(c.err, error))
      fprintf(stderr, "expected %s: exit%d %s\n", error, c.status, c.err);
    assert(c.status == 1 && strstr(c.err, error));
    char *old = f_read(planpath, 100);
    assert(old && !strcmp(old, "prior plan"));
    free(old);
  } else {
    if (c.status)
      fprintf(stderr, "%s\n%s", c.out, c.err);
    assert(!c.status);
  }
  f_capture_free(&c);
  return error ? NULL : f_read_json(planpath, 1000000);
}
static void valid_maps(void) {
  for (size_t k = 0; k < 4; k++) {
    json_object *items = json_object_new_array(),
                *selected = json_object_new_array(),
                *expected_inputs = json_object_new_object();
    const char *names[] = {"finding", "manifest", "compose",
                           "check",   "enabled",  "skipped"};
    for (size_t i = 0; i < (k == 0 ? 0 : k == 1 ? 1 : k == 2 ? 6 : 8); i++) {
      char id[40];
      if (k == 1)
        assert(!f_copy(id, sizeof id, "only"));
      else if (k == 2)
        assert(!f_copy(id, sizeof id, names[i]));
      else
        assert(snprintf(id, sizeof id, "i%zu", i) > 0);
      json_object *a = json_object_new_object();
      f_string_add(a, "id", id);
      json_object_object_add(a, "value",
                             json_object_new_int(k == 3 ? (int)i : 2));
      json_object_object_add(a, "enabled", json_object_new_boolean(k != 1));
      json_object_array_add(items, a);
      if (k != 1) {
        json_object_array_add(selected, json_object_new_string(id));
        char name[100], path[F_PATH];
        assert(snprintf(name, sizeof name, "member-%s", id) > 0);
        json_object_object_add(expected_inputs, name,
                               json_object_new_boolean(true));
        nt_path(path, f.inputs, name);
        json_object *member = nt_clone(a);
        json_object_object_del(member, "enabled");
        int val = json_object_get_int(f_field(a, "value"));
        json_object_object_add(member, "square",
                               json_object_new_int(val * val));
        nt_json_write(path, member);
        json_object_put(member);
      }
    }
    prepare(items);
    json_object *plan = precompile(NULL), *steps = f_field(plan, "steps"),
                *actual = json_object_new_array();
    json_object *compose = NULL;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
      json_object *s = json_object_array_get_idx(steps, i);
      const char *id = f_string(s, "id");
      if (!strncmp(id, "work-item-", 10))
        json_object_array_add(actual, json_object_new_string(id + 10));
      if (!strcmp(id, "compose"))
        compose = f_field(f_field(s, "args"), "argv");
    }
    assert(json_object_equal(actual, selected));
    json_object_put(actual);
    json_object *data = f_field(f_field(plan, "data"), "steps"),
                *join = f_field(f_field(data, "compose"), "inputs");
    assert(json_object_object_length(join) ==
           json_object_object_length(expected_inputs));
    json_object_object_foreach(expected_inputs, key, val) {
      (void)val;
      assert(f_field(join, key));
    }
    json_object *check = f_field(f_field(data, "check"), "inputs");
    assert(json_object_object_length(check) == 3 && f_field(check, "finding") &&
           f_field(check, "manifest") && f_field(check, "subject"));
    assert(compose);
    size_t count = json_object_array_length(compose);
    assert(count < 100);
    char *argv[101] = {0};
    for (size_t j = 1; j < count; j++)
      argv[j] =
          (char *)json_object_get_string(json_object_array_get_idx(compose, j));
    json_object *found = f_read_json(finding, 1000000),
                *expected = f_parse("{\"schema_version\":1}");
    json_object_object_add(expected, "selected_ids", nt_clone(selected));
    json_object_object_add(expected, "members",
                           nt_clone(f_field(found, "results")));
    const char *shells[] = {"sh", "dash"};
    for (size_t s = 0; s < 2; s++) {
      argv[0] = (char *)shells[s];
      outcome_status(argv, 0);
      char path[F_PATH];
      nt_path(path, f.outputs, "report.json");
      json_object *report = f_read_json(path, 1000000);
      assert(json_object_equal(report, expected));
      json_object_put(report);
    }
    json_object_put(expected);
    json_object_put(found);
    json_object_put(plan);
    json_object_put(items);
    json_object_put(selected);
    json_object_put(expected_inputs);
  }
}
static void check_finding(void) {
  json_object *original = f_read_json(finding, 1000000);
  char subject[F_PATH], report[F_PATH];
  nt_path(subject, f.inputs, "subject");
  nt_path(report, f.outputs, "check.json");
  for (size_t i = 0; i < 7; i++) {
    json_object *v = nt_clone(original);
    if (i == 1)
      json_object_object_add(v, "selected_ids", json_object_new_array());
    if (i == 2 || i == 4)
      f_string_add(
          v, i == 2 ? "source_sha256" : "result_sha256",
          "0000000000000000000000000000000000000000000000000000000000000000");
    if (i == 3) {
      json_object *a = json_object_array_get_idx(f_field(v, "results"), 0);
      json_object_object_add(
          a, "square",
          json_object_new_int(json_object_get_int(f_field(a, "square")) + 1));
    }
    if (i == 5)
      json_object_object_add(v, "schema_version",
                             json_object_new_boolean(true));
    if (i == 6)
      json_object_object_add(v, "extra", json_object_new_int(1));
    nt_json_write(subject, v);
    json_object_put(v);
    char *argv[] = {"./plan-example", "staged-check", "stage1", NULL};
    outcome_status(argv, i ? 1 : 0);
    v = f_read_json(report, 1000000);
    assert(!strcmp(f_string(v, "verdict"), i ? "fail" : "pass"));
    nt_equal(f_field(f_field(json_object_array_get_idx(
                                 f_field(v, "evidence_records"), 0),
                             "invocation"),
                     "argv"),
             "[\"./plan-example\",\"staged-check\",\"stage1\"]");
    json_object_put(v);
  }
  json_object_put(original);
}
static void input_boundaries(void) {
  char *original = f_read("manifest.json", 1000000);
  const char *raw[] = {
      "{\"schema_version\":1,\"items\":{}}",
      "{\"schema_version\":1,\"schema_version\":1,\"items\":[]}",
      ("{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":true,"
       "\"enabled\":true}]}")};
  const char *errors[] = {"manifest cardinality", "duplicate",
                          "manifest member"};
  for (size_t i = 0; i < 3; i++) {
    nt_write("manifest.json", raw[i]);
    json_object_put(precompile(errors[i]));
  }
  json_object *v = f_parse("{\"schema_version\":1,\"items\":[]}");
  for (int i = 0; i < 9; i++) {
    json_object *a = f_parse("{\"value\":1,\"enabled\":false}");
    char id[20];
    assert(snprintf(id, sizeof id, "i%d", i) > 0);
    f_string_add(a, "id", id);
    json_object_array_add(f_field(v, "items"), a);
  }
  nt_json_write("manifest.json", v);
  json_object_put(v);
  json_object_put(precompile("manifest cardinality"));
  nt_write("manifest.json", original);
  free(original);
}
static void public_gates(void) {
  json_object *v = f_read_json(public_file, 1000000);
  f_string_add(f_field(v, "data"), "verdict", "fail");
  nt_json_write(public_file, v);
  json_object_put(v);
  json_object_put(precompile("not an accepted passing delivery"));
  bind_result();
  char *raw = f_read(finding, 1000000);
  FILE *file = fopen(finding, "a");
  assert(file);
  assert(fputc(' ', file) != EOF && !fclose(file));
  json_object_put(precompile("finding hash mismatch"));
  nt_write(finding, raw);
  free(raw);
  bind_result();
}
static void semantic_forgery(void) {
  json_object *original = f_read_json(finding, 1000000);
  const char *fields[] = {"source_sha256", "selected_ids", "results",
                          "result_sha256", "status"},
             *errors[] = {
                 "stale or changed finding source",
                 "finding selection mismatch", "finding results mismatch",
                 "finding result hash mismatch", "finding status/schema"};
  for (size_t i = 0; i < 5; i++) {
    json_object *v = nt_clone(original);
    json_object_object_add(
        v, fields[i],
        i == 1 || i == 2 ? json_object_new_array()
                         : json_object_new_string(
                               i == 0 ? "cccccccccccccccccccccccccccccccccccccc"
                                        "cccccccccccccccccccccccccc"
                               : i == 3 ? "dddddddddddddddddddddddddddddddddddd"
                                          "dddddddddddddddddddddddddddd"
                                        : "inconclusive"));
    nt_json_write(finding, v);
    json_object_put(v);
    bind_result();
    json_object_put(precompile(errors[i]));
  }
  json_object_put(original);
  json_object *items =
      f_parse_value("[{\"id\":\"one\",\"value\":1,\"enabled\":true}]");
  prepare(items);
  json_object_put(items);
  json_object *v = f_read_json(finding, 1000000);
  json_object_object_add(json_object_array_get_idx(f_field(v, "results"), 0),
                         "square", json_object_new_boolean(true));
  nt_json_write(finding, v);
  json_object_put(v);
  bind_result();
  json_object_put(precompile("finding results mismatch"));
  prepare(NULL);
}
static void check_stage2(void) {
  char path[F_PATH];
  nt_path(path, f.inputs, "finding");
  nt_copy_tree(finding, path);
  json_object *found = f_read_json(finding, 1000000),
              *valid = f_parse("{\"schema_version\":1}");
  json_object_object_add(valid, "selected_ids",
                         nt_clone(f_field(found, "selected_ids")));
  json_object_object_add(valid, "members", nt_clone(f_field(found, "results")));
  json_object_put(found);
  for (size_t i = 0; i < 4; i++) {
    json_object *v = nt_clone(valid);
    if (i == 1)
      json_object_object_add(v, "members", json_object_new_array());
    if (i == 2) {
      json_object *a = json_object_array_get_idx(f_field(v, "members"), 0);
      json_object_object_add(
          a, "square",
          json_object_new_int(json_object_get_int(f_field(a, "square")) + 1));
    }
    if (i == 3)
      json_object_object_add(v, "schema_version",
                             json_object_new_boolean(true));
    nt_path(path, f.inputs, "subject");
    nt_json_write(path, v);
    json_object_put(v);
    outcome_check("staged-check", i ? 1 : 0);
    nt_path(path, f.outputs, "check.json");
    v = f_read_json(path, 1000000);
    assert(!strcmp(f_string(v, "verdict"), i ? "fail" : "pass"));
    json_object_put(v);
  }
  json_object_put(valid);
}
static void null_subjects(void) {
  char path[F_PATH];
  nt_path(path, f.inputs, "subject");
  nt_write(path, "null");
  char *stage1[] = {"./plan-example", "staged-check", "stage1", NULL};
  char *stage2[] = {"./plan-example", "staged-check", NULL};
  char **commands[] = {stage1, stage2};
  for (size_t i = 0; i < 2; i++) {
    outcome_status(commands[i], 1);
    nt_path(path, f.outputs, "check.json");
    json_object *report = f_read_json(path, 1000000);
    nt_equal(f_field(report, "schema_version"), "3");
    assert(!strcmp(f_string(report, "verdict"), "fail"));
    json_object_put(report);
  }
}
int main(void) {
  outcome_setup(&f, "staged");
  nt_path(public_file, f.folder, "result.json");
  nt_path(finding, f.repo, "finding.json");
  nt_path(planpath, f.folder, "plan.json");
  char wrapper[F_PATH];
  nt_path(wrapper, f.folder, "hydra");
  nt_write(wrapper, "#!/bin/sh\ncat \"$(dirname \"$0\")/result.json\"\n");
  assert(!chmod(wrapper, 0700) && !setenv("HYDRA_BIN", wrapper, 1));
  prepare(NULL);
  valid_maps();
  check_finding();
  input_boundaries();
  public_gates();
  semantic_forgery();
  check_stage2();
  null_subjects();
  assert(!unsetenv("HYDRA_BIN"));
  outcome_finish(&f, "Staged gates: all six original groups passed (fixed "
                     "public result double)");
  return 0;
}
