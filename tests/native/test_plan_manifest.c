#include "outcome_support.h"
static struct outcome_fixture f;
static json_object *manifest(const char *items) {
  json_object *v = f_parse("{\"schema_version\":1,\"items\":[]}");
  json_object_object_add(v, "items", f_parse_value(items));
  return v;
}
static json_object *precompile(json_object *v, int status) {
  nt_json_write("manifest.json", v);
  char *add[] = {"git", "add", "manifest.json", NULL};
  char *commit[] = {"git", "-c", "user.name=Test", "-c",
                    "user.email=test@example.invalid", "-c", "commit.gpgSign=false",
                    "commit", "--allow-empty", "-qm", "manifest update", NULL};
  outcome_status(add, 0);
  outcome_status(commit, 0);
  char *argv[] = {f.precompiler, "manifest", "manifest.json", "plan.json",
                  NULL};
  outcome_status(argv, status);
  return status ? NULL : f_read_json("plan.json", 1000000);
}
static void payload(json_object *plan, const char *shell) {
  json_object *steps = f_field(plan, "steps");
  for (size_t i = 0; i < json_object_array_length(steps); i++) {
    json_object *s = json_object_array_get_idx(steps, i);
    const char *id = f_string(s, "id");
    if (strncmp(id, "work-item-", 10) && strcmp(id, "compose"))
      continue;
    json_object *a = f_field(f_field(s, "args"), "argv");
    size_t n = json_object_array_length(a);
    assert(n < 100);
    char *argv[101] = {0};
    argv[0] = (char *)shell;
    for (size_t j = 1; j < n; j++)
      argv[j] = (char *)json_object_get_string(json_object_array_get_idx(a, j));
    outcome_status(argv, 0);
    if (!strncmp(id, "work-item-", 10)) {
      char name[100], source[F_PATH], dest[F_PATH];
      assert(snprintf(name, sizeof name, "result-%s.json", id + 10) > 0);
      nt_path(source, f.outputs, name);
      assert(snprintf(name, sizeof name, "member-%s", id + 10) > 0);
      nt_path(dest, f.inputs, name);
      nt_copy_tree(source, dest);
    }
  }
}
static void valid_cases(void) {
  const char *fixed[] = {"[]",
                         "[{\"id\":\"only\",\"value\":7,\"enabled\":false}]",
                         ("[{\"id\":\"a\",\"value\":-2,\"enabled\":true},{"
                         "\"id\":\"b\",\"value\":3,\"enabled\":false}]")};
  for (size_t k = 0; k < 6; k++) {
    json_object *v = manifest(k < 3 ? fixed[k] : "[]"),
                *items = f_field(v, "items"),
                *expected = f_parse("{\"schema_version\":1,\"members\":[]}");
    const char *reserved[] = {"compose", "check", "manifest", "enabled",
                              "skipped"};
    if (k >= 3)
      for (size_t i = 0; i < (k == 3 ? 5 : 8); i++) {
        char id[40];
        if (k == 3)
          assert(!f_copy(id, sizeof id, reserved[i]));
        else
          assert(
              snprintf(id, sizeof id,
                       k == 4 ? "i%zu" : "i%zuxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                       i) > 0);
        json_object *item = json_object_new_object();
        f_string_add(item, "id", id);
        json_object_object_add(item, "value",
                               json_object_new_int(k == 3   ? 1
                                                   : k == 4 ? (int)i - 4
                                                   : i % 2  ? -10
                                                            : 10));
        json_object_object_add(item, "enabled",
                               json_object_new_boolean(k != 5 || i % 3 != 0));
        json_object_array_add(items, item);
      }
    size_t enabled = 0;
    for (size_t i = 0; i < json_object_array_length(items); i++) {
      json_object *item = json_object_array_get_idx(items, i),
                  *member = nt_clone(item);
      bool on = json_object_get_boolean(f_field(item, "enabled"));
      json_object_object_del(member, "enabled");
      if (on) {
        int value = json_object_get_int(f_field(item, "value"));
        json_object_object_add(member, "square",
                               json_object_new_int(value * value));
        enabled++;
      } else
        f_string_add(member, "status", "skipped");
      json_object_array_add(f_field(expected, "members"), member);
    }
    json_object *plan = precompile(v, 0);
    assert(plan);
    size_t workers = 0;
    json_object *steps = f_field(plan, "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++)
      if (!strncmp(f_string(json_object_array_get_idx(steps, i), "id"), "work-",
                   5))
        workers++;
    assert(workers == enabled);
    char compiled[40];
    assert(snprintf(compiled, sizeof compiled, "compiled-%zu.json", k) > 0);
    json_object_put(outcome_cli(&f, "compile", "plan.json", "policy.json",
                                compiled, 0));
    const char *shells[] = {"sh", "dash"};
    for (size_t s = 0; s < 2; s++) {
      payload(plan, shells[s]);
      char path[F_PATH];
      nt_path(path, f.outputs, "report.json");
      json_object *report = f_read_json(path, 1000000);
      assert(json_object_equal(report, expected));
      json_object_put(report);
    }
    json_object_put(plan);
    json_object_put(v);
    json_object_put(expected);
  }
}
static void missing_worker(void) {
  json_object *v = manifest("[{\"id\":\"a\",\"value\":2,\"enabled\":true}]"),
              *plan = precompile(v, 0);
  payload(plan, "sh");
  char path[F_PATH];
  nt_path(path, f.inputs, "member-a");
  assert(!unlink(path));
  json_object *steps = f_field(plan, "steps");
  for (size_t i = 0; i < json_object_array_length(steps); i++) {
    json_object *s = json_object_array_get_idx(steps, i);
    if (strcmp(f_string(s, "id"), "compose"))
      continue;
    json_object *a = f_field(f_field(s, "args"), "argv");
    size_t n = json_object_array_length(a);
    assert(n < 100);
    char *argv[101] = {0};
    for (size_t j = 0; j < n; j++)
      argv[j] = (char *)json_object_get_string(json_object_array_get_idx(a, j));
    struct f_capture c = nt_run(argv);
    assert(c.status);
    f_capture_free(&c);
  }
  json_object_put(v);
  json_object_put(plan);
}
static void invalid_lowering(void) {
  const char *cases[] = {
      "{\"schema_version\":true,\"items\":[]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":1,\"enabled\":"
      "true},{\"id\":\"a\",\"value\":1,\"enabled\":true}]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":1,\"enabled\":"
      "true,\"extra\":0}]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":1.5,"
      "\"enabled\":true}]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":true,"
      "\"enabled\":true}]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"../"
      "a\",\"value\":1,\"enabled\":true}]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":99,\"enabled\":"
      "true}]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":1,\"enabled\":"
      "1}]}",
      "{\"schema_version\":1,\"items\":[],\"unknown\":1}"};
  for (size_t i = 0; i < 9; i++) {
    json_object *v = f_parse(cases[i]);
    json_object_put(precompile(v, 1));
    json_object_put(v);
  }
  json_object *v = manifest("[]");
  for (int i = 0; i < 9; i++)
    json_object_array_add(
        f_field(v, "items"),
        f_parse("{\"id\":\"a\",\"value\":1,\"enabled\":true}"));
  json_object_put(precompile(v, 1));
  json_object_put(v);
  nt_write("existing.json", "preserve prior output");
  char large[4098];
  memset(large, ' ', 4097);
  large[4097] = 0;
  const char *raw[] = {
      "{\"schema_version\":1,\"schema_version\":1,\"items\":[]}",
      "{\"schema_version\":1,\"items\":[{\"id\":\"a\",\"value\":1,\"value\":2,"
      "\"enabled\":true}]}",
      large, "{\"schema_version\":1,\"items\":"};
  for (size_t i = 0; i < 4; i++) {
    nt_write("manifest.json", raw[i]);
    char *argv[] = {f.precompiler, "manifest", "manifest.json", "existing.json",
                    NULL};
    outcome_status(argv, 1);
    char *text = f_read("existing.json", 100);
    assert(text && !strcmp(text, "preserve prior output"));
    free(text);
  }
  nt_write("different.json", "{\"schema_version\":1,\"items\":[]}");
  char *argv[] = {f.precompiler, "manifest", "different.json", "existing.json",
                  NULL};
  outcome_status(argv, 1);
}
static void check_report(json_object *m, json_object *r, int status) {
  char path[F_PATH];
  nt_path(path, f.inputs, "manifest");
  nt_json_write(path, m);
  nt_path(path, f.inputs, "subject");
  nt_json_write(path, r);
  outcome_check("manifest-check", status);
  if (status == 2)
    return;
  nt_path(path, f.outputs, "check.json");
  json_object *e = f_read_json(path, 1000000);
  assert(e);
  nt_equal(f_field(e, "schema_version"), "3");
  assert(!strcmp(f_string(e, "verdict"), status ? "fail" : "pass"));
  assert(!strcmp(
      f_string(e, "validator_sha256"),
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  json_object *rec =
      json_object_array_get_idx(f_field(e, "evidence_records"), 0);
  assert(!strcmp(
      f_string(rec, "validator_recipe_sha256"),
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  nt_equal(f_field(rec, "counts"),
           status ? "{\"executed\":1,\"failed\":1,\"skipped\":0}"
                  : "{\"executed\":1,\"failed\":0,\"skipped\":0}");
  json_object_put(e);
}
static void checker_cases(void) {
  json_object
      *m = manifest("[{\"id\":\"a\",\"value\":2,\"enabled\":true},{\"id\":"
                    "\"b\",\"value\":3,\"enabled\":false}]"),
      *valid = f_parse(
          "{\"schema_version\":1,\"members\":[{\"id\":\"a\",\"value\":2,"
          "\"square\":4},{\"id\":\"b\",\"value\":3,\"status\":\"skipped\"}]}");
  for (size_t i = 0; i < 5; i++) {
    json_object *r = nt_clone(valid), *members = f_field(r, "members"),
                *a = json_object_array_get_idx(members, 0);
    if (i == 1)
      assert(!json_object_array_del_idx(members, 1, 1));
    if (i == 2)
      json_object_object_add(a, "square", json_object_new_int(9));
    if (i == 3) {
      json_object_object_del(a, "square");
      f_string_add(a, "status", "skipped");
    }
    if (i == 4)
      json_object_array_add(members,
                            f_parse("{\"id\":\"x\",\"value\":0,\"square\":0}"));
    check_report(m, r, i ? 1 : 0);
    json_object_put(r);
  }
  json_object_put(m);
  json_object_put(valid);
  m = manifest("[{\"id\":\"a\",\"value\":1,\"enabled\":true}]");
  valid = f_parse("{\"schema_version\":1,\"members\":[{\"id\":\"a\",\"value\":"
                  "1,\"square\":1}]}");
  check_report(m, valid, 0);
  for (size_t i = 0; i < 7; i++) {
    json_object *r = i == 5   ? NULL
                     : i == 6 ? json_object_new_array()
                              : nt_clone(valid);
    if (i < 5) {
      json_object *a = json_object_array_get_idx(f_field(r, "members"), 0);
      if (i == 0)
        json_object_object_add(r, "schema_version",
                               json_object_new_boolean(true));
      if (i == 1)
        json_object_object_add(r, "unknown", json_object_new_int(1));
      if (i == 2 || i == 3)
        json_object_object_add(a, i == 2 ? "value" : "square",
                               json_object_new_boolean(true));
      if (i == 4)
        json_object_object_add(a, "extra", json_object_new_int(0));
    }
    check_report(m, r, 1);
    json_object_put(r);
  }
  json_object_put(m);
  json_object_put(valid);
}
static void invalid_checker_manifest(void) {
  for (size_t i = 0; i < 5; i++) {
    json_object *m = manifest("[]"), *items = f_field(m, "items"),
                *r = f_parse("{\"schema_version\":1,\"members\":[]}");
    if (i == 0)
      json_object_object_add(m, "schema_version",
                             json_object_new_boolean(true));
    else
      for (size_t j = 0; j < (i == 1 ? 9 : i == 2 ? 2 : 1); j++) {
        json_object *a =
            f_parse("{\"id\":\"a\",\"value\":1,\"enabled\":false}");
        if (i == 1) {
          char id[16];
          assert(snprintf(id, sizeof id, "i%zu", j) > 0);
          f_string_add(a, "id", id);
        }
        if (i == 3)
          json_object_object_add(a, "value", json_object_new_boolean(true));
        if (i == 4)
          json_object_object_add(a, "enabled", json_object_new_int(0));
        json_object_array_add(items, a);
      }
    check_report(m, r, 2);
    json_object_put(m);
    json_object_put(r);
  }
}
int main(void) {
  outcome_setup(&f, "manifest-map");
  valid_cases();
  missing_worker();
  invalid_lowering();
  checker_cases();
  invalid_checker_manifest();
  outcome_finish(&f, "Manifest: all seven original case groups passed");
  return 0;
}
