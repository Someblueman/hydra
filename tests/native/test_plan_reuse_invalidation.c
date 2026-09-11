#include "accepted_fixture.h"
static char hydra[F_PATH], source[F_PATH], fixture[F_PATH];
static const char *fleet;
static void path(char *out, const char *run, const char *name) {
  nt_path(out, run, name);
}
static json_object *read_at(const char *run, const char *name) {
  char p[F_PATH];
  path(p, run, name);
  json_object *v = f_read_json(p, 8000000);
  assert(v);
  return v;
}
static void write_at(const char *run, const char *name, json_object *v) {
  char p[F_PATH];
  path(p, run, name);
  nt_json_write(p, v);
}
static void rehash_record(const char *run, const char *name) {
  char p[F_PATH], digest[65];
  json_object *v = read_at(run, "repair-2.json"), *e = f_field(v, "evidence"),
              *proof =
                  f_field(f_field(f_field(e, "reuse"), "steps"), "produce");
  char relative[F_PATH];
  assert(snprintf(relative, sizeof relative, "steps/produce/attempt-1/%s",
                  name) > 0);
  path(p, run, relative);
  assert(!f_hash(p, digest));
  f_string_add(proof, name, digest);
  af_hash_json(e, digest, true);
  f_string_add(v, "sha256", digest);
  write_at(run, "repair-2.json", v);
  json_object_put(v);
}
static void run_result(const char *home, bool pass, bool changed_path) {
  char run[F_PATH];
  af_run_path(run, home);
  af_environment(run, home, fleet);
  if (changed_path) {
    const char *old = getenv("PATH");
    assert(old);
    char *s = malloc(strlen(old) + 40);
    assert(s);
    int n =
        snprintf(s, strlen(old) + 40, "%s:/hydra-undeclared-tool-path", old);
    assert(n > 0);
    assert(!setenv("PATH", s, 1));
    free(s);
  }
  glob_t before, after;
  af_glob(&before, home, "fleet/tasks/task_*/acceptance.json");
  const char *id = strrchr(run, '/');
  assert(id);
  char *args[] = {hydra, "workflow", "plan", "result", (char *)id + 1, NULL};
  struct f_capture c = nt_run(args);
  if (pass) {
    json_object *v = nt_output(&c, 0);
    nt_equal(f_field(v, "ok"), "true");
    json_object_put(v);
  } else {
    if (!c.status)
      fprintf(stderr, "corrupted copied fixture accepted: %s\n", c.out);
    assert(c.status);
  }
  f_capture_free(&c);
  af_glob(&after, home, "fleet/tasks/task_*/acceptance.json");
  assert(before.gl_pathc == after.gl_pathc);
  for (size_t i = 0; i < before.gl_pathc; i++)
    assert(!strcmp(before.gl_pathv[i], after.gl_pathv[i]));
  globfree(&before);
  globfree(&after);
}
static void mutation(const char *run, size_t index) {
  char p[F_PATH], digest[65];
  json_object *v, *e;
  if (index == 0 || index == 2) {
    path(p, run,
         index == 0 ? "steps/produce/attempt-1/inputs/environment"
                    : "steps/produce/attempt-1/inputs/recipe");
    FILE *f = fopen(p, "a");
    assert(f && fputc(' ', f) != EOF && !fclose(f));
    return;
  }
  if (index == 1) {
    v = read_at(run, "steps/produce/attempt-1/inputs/environment");
    f_string_add(v, "scope", "tampered");
    write_at(run, "steps/produce/attempt-1/inputs/environment", v);
    json_object_put(v);
    return;
  }
  if (index == 3) {
    path(p, run, "plan-accepted");
    nt_write(
        p,
        "0000000000000000000000000000000000000000000000000000000000000000\n");
    return;
  }
  if (index == 4) {
    v = read_at(run, "steps/produce/attempt-1/remote/receipt.json");
    f_string_add(v, "submission_key", "tampered");
    write_at(run, "steps/produce/attempt-1/remote/receipt.json", v);
    json_object_put(v);
    rehash_record(run, "remote/receipt.json");
    return;
  }
  if (index == 5 || (index >= 7 && index <= 10)) {
    v = read_at(run, "steps/produce/attempt-1/remote/result.json");
    json_object *result = f_field(v, "result"),
                *receipt = f_field(result, "receipt");
    af_hash_json(result, digest, false);
    assert(!strcmp(digest, f_string(v, "result_sha256")));
    if (index == 5)
      f_string_add(
          receipt, "task_id",
          "task_"
          "0000000000000000000000000000000000000000000000000000000000000000");
    else {
      const char *states[] = {"outcome_unknown", "failed", "cancelled",
                              "succeeded"};
      f_string_add(f_field(receipt, "runtime"), "state", states[index - 7]);
      if (index == 10)
        json_object_object_add(f_field(receipt, "runtime"), "exit_status",
                               json_object_new_int(7));
    }
    af_hash_json(result, digest, false);
    f_string_add(v, "result_sha256", digest);
    write_at(run, "steps/produce/attempt-1/remote/result.json", v);
    json_object_put(v);
    rehash_record(run, "remote/result.json");
    return;
  }
  if (index == 11) {
    path(p, run, "steps/produce/state");
    nt_write(p, "outcome_unknown\n");
    return;
  }
  if (index == 12 || index == 13) {
    v = read_at(run, "compiled.json");
    if (index == 12) {
      e = f_field(v, "plan");
      const char *old = f_string(e, "objective");
      char *s = malloc(strlen(old) + 10);
      assert(s);
      assert(snprintf(s, strlen(old) + 10, "%s tampered", old) > 0);
      f_string_add(e, "objective", s);
      free(s);
    } else
      f_string_add(
          f_field(v, "source"), "sha256",
          "0000000000000000000000000000000000000000000000000000000000000000");
    write_at(run, "compiled.json", v);
    json_object_put(v);
    return;
  }
  v = read_at(run, "repair-2.json");
  e = f_field(v, "evidence");
  if (index == 6)
    json_object_object_add(e, "failures", json_object_new_array());
  else if (index == 14)
    json_object_object_add(f_field(f_field(e, "reuse"), "steps"), "unknown",
                           json_object_new_object());
  else {
    assert(index == 15);
    json_object_object_del(f_field(f_field(e, "reuse"), "steps"), "inspect");
  }
  af_hash_json(e, digest, true);
  f_string_add(v, "sha256", digest);
  write_at(run, "repair-2.json", v);
  json_object_put(v);
}
static void policy_controls(const char *home) {
  char p[F_PATH], run[F_PATH], policy[F_PATH],
      temp[] = "/tmp/hydra-reuse-policy-XXXXXX";
  af_run_path(run, home);
  af_environment(run, home, fleet);
  path(p, fixture, "plan.json");
  json_object *original = f_read_json(p, 8000000);
  assert(original);
  path(policy, fixture, "policy.json");
  assert(mkdtemp(temp));
  path(p, temp, "plan.json");
  const char *keys[][6] = {
      {"reuse_policy", "schema_version", NULL},
      {"reuse_policy", "mode", NULL},
      {"reuse_policy", "extra", NULL},
      {"reuse_policy", "steps", "unknown", NULL},
      {"reuse_policy", "steps", "produce", "dependencies", NULL},
      {"reuse_policy", "steps", "produce", "effects", NULL},
      {"reuse_policy", "steps", "produce", "environment_input", NULL},
      {"data", "steps", "produce", "inputs", "repair", NULL},
      {"data", "steps", "produce", "inputs", "history", NULL}};
  const char *values[] = {
      "2",           "\"external_cache\"", "true",
      "{}",          "\"unknown\"",        "\"external\"",
      "\"missing\"", "{\"repair\":true}",  "{\"provenance\":\"produce\"}"};
  for (int i = -1; i < 9; i++) {
    json_object *v = nt_clone(original);
    if (i >= 0) {
      json_object *target = v;
      size_t k = 0;
      while (keys[i][k + 1]) {
        target = f_field(target, keys[i][k]);
        assert(target);
        k++;
      }
      json_object_object_add(target, keys[i][k], f_parse_value(values[i]));
    }
    nt_json_write(p, v);
    json_object_put(v);
    char *args[] = {hydra, "workflow", "plan", "validate", p, policy, NULL};
    struct f_capture c = nt_run(args);
    v = f_parse(c.out);
    assert(v);
    if (i < 0) {
      assert(!c.status);
      nt_equal(f_field(v, "ok"), "true");
    } else {
      assert(c.status);
      nt_equal(f_field(v, "ok"), "false");
      json_object *rows = f_field(f_field(v, "data"), "diagnostics");
      bool found = false;
      for (size_t j = 0; j < json_object_array_length(rows); j++)
        if (!strcmp(f_string(json_object_array_get_idx(rows, j), "code"),
                    "unsupported_reuse"))
          found = true;
      assert(found);
    }
    json_object_put(v);
    f_capture_free(&c);
  }
  json_object_put(original);
  assert(!f_remove_tree(temp));
  puts("policy: baseline accepted, 9 unsupported declarations rejected");
}
int main(int argc, char **argv) {
  char root[F_PATH], home[F_PATH], run[F_PATH], binary[F_PATH],
      scratch[] = "/tmp/hydra-plan-reuse-native-XXXXXX";
  assert(argc == 2 || argc == 4);
  assert(getcwd(root, sizeof root));
  path(hydra, root, "bin/hydra");
  if (argv[1][0] == '/')
    assert(!f_copy(fixture, sizeof fixture, argv[1]));
  else
    path(fixture, root, argv[1]);
  fleet = getenv("HYDRA_FLEET_BIN");
  if (argc == 4) {
    assert(!strcmp(argv[2], "--fleet-bin"));
    fleet = argv[3];
  }
  if (!fleet) {
    path(binary, root, "build/hydra-fleet");
    fleet = binary;
  }
  path(home, fixture, "home");
  path(source, fixture, "source");
  af_run_path(run, home);
  json_object *v = read_at(run, "repair-2.json");
  char digest[65];
  af_hash_json(f_field(v, "evidence"), digest, true);
  assert(!strcmp(digest, f_string(v, "sha256")));
  json_object_put(v);
  assert(!chdir(source));
  run_result(home, true, false);
  run_result(home, false, true);
  puts("changed-current-environment: rejected without new acceptance");
  policy_controls(home);
  assert(mkdtemp(scratch));
  const char *names[] = {"input",
                         "environment",
                         "recipe",
                         "acceptance",
                         "receipt-rehashed",
                         "result-rehashed",
                         "malformed-failures",
                         "unknown-result-rehashed",
                         "failed-result-rehashed",
                         "cancelled-result-rehashed",
                         "nonzero-success-result",
                         "unknown-step-outcome",
                         "compiled-contract",
                         "source-binding",
                         "unknown-proof-step",
                         "dependency-removal"};
  for (size_t i = 0; i < 16; i++) {
    char copy[F_PATH];
    path(copy, scratch, names[i]);
    nt_copy_tree(home, copy);
    af_run_path(run, copy);
    mutation(run, i);
    run_result(copy, false, false);
    printf("%s: rejected\n", names[i]);
  }
  assert(!chdir(root));
  nt_finish(scratch, "baseline: accepted");
  return 0;
}
