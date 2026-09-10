#include "support.h"
static char folder[F_PATH], repo[F_PATH], hydra[F_PATH], policy[F_PATH],
    estimates[F_PATH], origin[F_PATH];
static json_object *plan;
static json_object *cli(const char *op, const char *a, const char *b,
                        const char *c, const char *d, int status) {
  char *argv[] = {hydra,     "workflow", "plan",    (char *)op, (char *)a,
                  (char *)b, (char *)c,  (char *)d, NULL};
  struct f_capture r = nt_run(argv);
  json_object *v = nt_output(&r, status);
  f_capture_free(&r);
  return v;
}
static json_object *data(json_object *v) {
  json_object *out = nt_clone(f_field(v, "data"));
  json_object_put(v);
  return out;
}
static void compile(const char *name, json_object *p, char output[F_PATH],
                    char digest[65]) {
  char source[F_PATH], file[128];
  assert(snprintf(file, sizeof file, "%s.plan.json", name) > 0);
  nt_path(source, folder, file);
  nt_json_write(source, p ? p : plan);
  assert(snprintf(file, sizeof file, "%s.compiled.json", name) > 0);
  nt_path(output, folder, file);
  json_object_put(cli("compile", source, policy, output, NULL, 0));
  json_object *v = cli("show", output, "--json", NULL, NULL, 0);
  assert(!f_copy(digest, 65, f_string(f_field(v, "data"), "sha256")));
  json_object_put(v);
}
static void estimate(const char *id, int low, int high, json_object *plans) {
  json_object *e = f_parse("{\"source\":\"Explicit test "
                           "intervals\",\"cost_unit\":\"USD\",\"steps\":{}}");
  const char *names[] = {"spawn", "compose", "verify"};
  for (size_t i = 0; i < 3; i++) {
    json_object *s = json_object_new_object();
    const char *keys[] = {"milliseconds", "cost_microunits"};
    for (size_t j = 0; j < 2; j++) {
      json_object *a = json_object_new_array();
      json_object_array_add(a, json_object_new_int(low));
      json_object_array_add(a, json_object_new_int(high));
      json_object_object_add(s, keys[j], a);
    }
    json_object_object_add(f_field(e, "steps"), names[i], s);
  }
  json_object_object_add(plans, id, e);
}
static void write_estimates(const char *a, int low, int high, const char *b,
                            int blow, int bhigh) {
  json_object *v = f_parse("{\"schema_version\":1,\"plans\":{}}");
  estimate(a, low, high, f_field(v, "plans"));
  if (b)
    estimate(b, blow, bhigh, f_field(v, "plans"));
  nt_json_write(estimates, v);
  json_object_put(v);
}
static bool contains(json_object *array, const char *text) {
  for (size_t i = 0; i < json_object_array_length(array); i++)
    if (!strcmp(json_object_get_string(json_object_array_get_idx(array, i)),
                text))
      return true;
  return false;
}
static json_object *step(json_object *p, size_t i) {
  return json_object_array_get_idx(f_field(p, "steps"), i);
}
int main(void) {
  char tmp[] = "/tmp/hydra-plan-inspection-XXXXXX", source[F_PATH],
       home[F_PATH], left[F_PATH], right[F_PATH], lid[65], rid[65];
  assert(getcwd(origin, sizeof origin));
  nt_path(hydra, origin, "bin/hydra");
  nt_path(policy, origin, "tests/fixtures/plan/policy.json");
  nt_path(source, origin, "tests/fixtures/plan/plan.json");
  plan = f_read_json(source, 1000000);
  assert(plan);
  assert(mkdtemp(tmp));
  assert(!f_copy(folder, sizeof folder, tmp));
  nt_path(repo, folder, "repo");
  nt_path(source, origin, "tests/fixtures/plan/repo");
  nt_repo(source, repo);
  nt_path(home, folder, "home");
  nt_path(estimates, folder, "estimates.json");
  assert(!setenv("HYDRA_HOME", home, 1));
  if (!getenv("HYDRA_FLEET_BIN")) {
    nt_path(source, origin, "build/hydra-fleet");
    assert(!setenv("HYDRA_FLEET_BIN", source, 1));
  }
  assert(!chdir(repo));
  compile("base", NULL, left, lid);
  char *git[] = {"git", "status", "--porcelain", NULL};
  struct f_capture before = nt_run(git);
  json_object *v = data(cli("explain", left, NULL, NULL, NULL, 0)),
              *nodes = f_field(v, "nodes");
  assert(!strcmp(f_string(v, "plan_sha256"), lid));
  assert(json_object_array_length(nodes) == 3);
  const char *names[] = {"spawn", "compose", "verify"};
  for (size_t i = 0; i < 3; i++) {
    json_object *n = json_object_array_get_idx(nodes, i);
    assert(!strcmp(f_string(n, "id"), names[i]) &&
           !strcmp(f_string(n, "outcome_link"), "reachable"));
  }
  nt_equal(
      f_field(
          json_object_array_get_idx(
              f_field(json_object_array_get_idx(nodes, 1), "dependencies"), 0),
          "reasons"),
      "[\"creates_execution_head\"]");
  nt_equal(json_object_array_get_idx(
               f_field(json_object_array_get_idx(
                           f_field(json_object_array_get_idx(nodes, 2),
                                   "dependencies"),
                           0),
                       "reasons"),
               0),
           "{\"kind\":\"artifact_input\",\"input\":\"subject\",\"output\":"
           "\"report\"}");
  assert(!f_field(f_field(v, "metrics"), "critical_path_milliseconds") &&
         !f_field(f_field(v, "metrics"), "cost_microunits"));
  nt_equal(f_field(f_field(f_field(v, "metrics"), "hard_budgets"),
                   "timeout_seconds"),
           "180");
  json_object_put(v);
  struct f_capture after = nt_run(git);
  assert(!strcmp(before.out, after.out));
  f_capture_free(&before);
  f_capture_free(&after);
  compile("left", NULL, left, lid);
  json_object *changed = nt_clone(plan);
  json_object_object_add(f_field(step(changed, 1), "args"), "argv",
                         f_parse_value("[\"sh\",\"other-compose.sh\"]"));
  compile("right", changed, right, rid);
  json_object_put(changed);
  write_estimates(lid, 10, 20, rid, 100, 200);
  v = data(cli("compare", left, right, "--estimates", estimates, 0));
  nt_equal(f_field(v, "matched_scope"), "true");
  assert(!strcmp(f_string(v, "modeled_preference"), "left"));
  nt_equal(f_field(f_field(v, "left"), "critical_path_milliseconds"),
           "[30,60]");
  json_object *again =
      data(cli("compare", left, right, "--estimates", estimates, 0));
  assert(json_object_equal(v, again));
  json_object_put(again);
  nt_equal(f_field(json_object_array_get_idx(
                       f_field(f_field(v, "invalidation"), "checks"), 0),
                   "binding_changed"),
           "true");
  assert(
      strstr(f_string(f_field(v, "invalidation"), "reuse_policy"), "no reuse"));
  json_object_put(v);
  v = data(cli("compare", left, right, NULL, NULL, 0));
  assert(!strcmp(f_string(v, "modeled_preference"), "unresolved"));
  json_object_put(v);
  write_estimates(lid, 40000, 50000, rid, 100000, 200000);
  v = data(cli("compare", left, right, "--estimates", estimates, 0));
  nt_equal(f_field(f_field(v, "left"), "estimate_budget_conflict"), "true");
  assert(!strcmp(f_string(v, "modeled_preference"), "unresolved"));
  json_object_put(v);
  changed = nt_clone(plan);
  f_string_add(json_object_array_get_idx(f_field(changed, "requirements"), 0),
               "criterion", "A materially different target");
  compile("scope", changed, right, rid);
  json_object_put(changed);
  v = data(cli("compare", left, right, NULL, NULL, 0));
  nt_equal(f_field(v, "matched_scope"), "false");
  assert(contains(f_field(v, "scope_differences"), "requirements") &&
         !strcmp(f_string(v, "modeled_preference"), "unresolved"));
  json_object_put(v);
  const char *keys[] = {"envelope", "data"};
  for (size_t i = 0; i < 2; i++) {
    changed = nt_clone(plan);
    if (!i)
      json_object_object_add(f_field(changed, "envelope"), "timeout_seconds",
                             json_object_new_int(170));
    else
      json_object_object_add(
          f_field(f_field(f_field(f_field(f_field(changed, "data"), "steps"),
                                  "compose"),
                          "outputs"),
                  "report"),
          "max_bytes", json_object_new_int(1023));
    compile(keys[i], changed, right, rid);
    json_object_put(changed);
    v = data(cli("compare", left, right, NULL, NULL, 0));
    nt_equal(f_field(v, "matched_scope"), "false");
    assert(contains(f_field(v, "scope_differences"), keys[i]));
    json_object_put(v);
  }
  compile("bad-estimate", NULL, left, lid);
  write_estimates(lid, 1, 2, NULL, 0, 0);
  json_object *original = f_read_json(estimates, 1000000);
  const char *ranges[] = {"[-1,2]",  "[2,1]",    "[0,10000000000000]",
                          "[1.5,2]", "[true,2]", "[1]"};
  for (size_t i = 0; i < 8; i++) {
    changed = nt_clone(original);
    json_object *plans = f_field(changed, "plans"),
                *entry = f_field(plans, lid);
    if (i < 6)
      json_object_object_add(f_field(f_field(entry, "steps"), "spawn"),
                             "milliseconds", f_parse_value(ranges[i]));
    else if (i == 6) {
      json_object_object_add(
          plans,
          "0000000000000000000000000000000000000000000000000000000000000000",
          nt_clone(entry));
      json_object_object_del(plans, lid);
    } else
      json_object_object_add(f_field(entry, "steps"), "unknown",
                             json_object_new_object());
    nt_json_write(estimates, changed);
    json_object_put(changed);
    json_object_put(cli("explain", left, "--estimates", estimates, NULL, 1));
  }
  json_object_put(original);
  changed = f_read_json(left, 1000000);
  json_object_object_add(step(f_field(changed, "plan"), 0), "needs",
                         f_parse_value("[\"verify\"]"));
  nt_json_write(left, changed);
  json_object_put(changed);
  json_object_put(cli("explain", left, NULL, NULL, NULL, 1));
  changed = nt_clone(plan);
  json_object_array_add(f_field(step(changed, 2), "needs"),
                        json_object_new_string("spawn"));
  compile("partial", changed, left, lid);
  json_object_put(changed);
  write_estimates(lid, 5, 7, NULL, 0, 0);
  changed = f_read_json(estimates, 1000000);
  json_object_object_del(
      f_field(f_field(f_field(f_field(changed, "plans"), lid), "steps"),
              "compose"),
      "milliseconds");
  nt_json_write(estimates, changed);
  json_object_put(changed);
  v = data(cli("explain", left, "--estimates", estimates, NULL, 0));
  assert(!f_field(f_field(v, "metrics"), "critical_path_milliseconds"));
  nt_equal(f_field(f_field(v, "metrics"), "cost_microunits"), "[15,21]");
  nt_equal(f_field(f_field(v, "metrics"), "edges"), "3");
  json_object_put(v);
  json_object_put(plan);
  assert(!chdir(origin));
  nt_finish(folder, "Plan inspection: all six original case groups passed");
  return 0;
}
