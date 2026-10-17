#include "support.h"
/* Public CLI cases for the three-concern evaluation, recorded-duration
 * estimates and replayable decision records. */
static char folder[F_PATH], repo[F_PATH], hydra[F_PATH], fleet[F_PATH],
    policy[F_PATH], estimates[F_PATH], origin[F_PATH];
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
/* Decision operations are head-scoped in the shell; the native operations
 * take an explicit private directory, which this test owns. */
static json_object *decision(const char *op, const char *a, const char *b,
                             const char *c, const char *d, const char *e,
                             const char *f, const char *g, int status) {
  char *argv[] = {fleet,     "workflow-plan", (char *)op, (char *)a,
                  (char *)b, (char *)c,       (char *)d,  (char *)e,
                  (char *)f, (char *)g,       NULL};
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
  json_object *e = f_parse(
      "{\"source\":\"Explicit test intervals\",\"cost_unit\":\"USD\","
      "\"steps\":{}}");
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
/* Adds the unconnected step's row to one plan's estimates. */
static void with_extra(const char *id, int low, int high) {
  json_object *v = f_read_json(estimates, 1000000), *row = json_object_new_object();
  const char *keys[] = {"milliseconds", "cost_microunits"};
  assert(v);
  for (size_t j = 0; j < 2; j++) {
    json_object *a = json_object_new_array();
    json_object_array_add(a, json_object_new_int(low));
    json_object_array_add(a, json_object_new_int(high));
    json_object_object_add(row, keys[j], a);
  }
  json_object_object_add(f_field(f_field(f_field(v, "plans"), id), "steps"),
                         "extra", row);
  nt_json_write(estimates, v);
  json_object_put(v);
}
/* The fixture plus an exec step that reaches no deliverable or check. */
static json_object *with_unconnected_step(void) {
  json_object *changed = nt_clone(plan);
  json_object *extra = f_parse(
      "{\"id\":\"extra\",\"role\":\"work\",\"kind\":\"exec\",\"needs\":["
      "\"spawn\"],\"writes\":[],\"args\":{\"head\":\"plan-smoke\",\"argv\":["
      "\"sh\",\"compose.sh\"],\"timeout\":30}}");
  json_object_array_add(f_field(changed, "steps"), extra);
  return changed;
}
static void concerns(const char *left, const char *lid, const char *right,
                     const char *rid) {
  json_object *v = data(cli("evaluate", left, right, NULL, NULL, 0));
  json_object *d = f_field(v, "decomposition"), *s = f_field(v, "scheduling");
  nt_equal(f_field(v, "matched_scope"), "true");
  assert(!strcmp(f_string(v, "analysis"), "hydra-plan-evaluate-1"));
  nt_equal(f_field(f_field(d, "left"), "unconnected_nodes"), "0");
  nt_equal(f_field(f_field(d, "right"), "unconnected_nodes"), "1");
  nt_equal(f_field(f_field(d, "right"), "required_nodes"), "3");
  assert(!strcmp(f_string(d, "modeled_preference"), "left"));
  nt_equal(f_field(f_field(f_field(d, "left"), "transfers"), "artifact_inputs"),
           "1");
  nt_equal(f_field(f_field(f_field(d, "left"), "rework_risk"), "probability"),
           "null");
  nt_equal(f_field(f_field(s, "left"), "depth_nodes"), "3");
  nt_equal(f_field(f_field(s, "right"), "max_level_width"), "2");
  assert(!f_field(f_field(s, "left"), "critical_path_milliseconds") &&
         !f_field(f_field(s, "left"), "time_to_verified_result_milliseconds"));
  assert(!strcmp(f_string(s, "modeled_preference"), "unresolved"));
  assert(!strcmp(f_string(f_field(v, "placement"), "modeled_preference"),
                 "not_assessed"));
  json_object *again = data(cli("evaluate", left, right, NULL, NULL, 0));
  assert(json_object_equal(v, again));
  json_object_put(again);
  json_object_put(v);
  /* A partial estimates file leaves the other alternative unknown; the
   * verified-result path needs only required nodes. */
  write_estimates(rid, 10, 20, NULL, 0, 0);
  v = data(cli("evaluate", left, right, "--estimates", estimates, 0));
  s = f_field(v, "scheduling");
  assert(!f_field(f_field(s, "left"), "critical_path_milliseconds"));
  assert(!f_field(f_field(s, "right"), "critical_path_milliseconds"));
  nt_equal(f_field(f_field(s, "right"), "time_to_verified_result_milliseconds"),
           "[30,60]");
  nt_equal(f_field(f_field(s, "right"), "cost_to_verified_result_microunits"),
           "[30,60]");
  nt_equal(f_field(f_field(f_field(f_field(v, "decomposition"), "right"),
                           "validation_effort"),
                   "verify_milliseconds"),
           "[10,20]");
  assert(!strcmp(f_string(s, "modeled_preference"), "unresolved"));
  json_object_put(v);
  write_estimates(lid, 10, 20, rid, 100, 200);
  v = data(cli("evaluate", left, right, "--estimates", estimates, 0));
  s = f_field(v, "scheduling");
  nt_equal(f_field(f_field(s, "left"), "critical_path_milliseconds"), "[30,60]");
  assert(!f_field(f_field(s, "right"), "critical_path_milliseconds"));
  nt_equal(f_field(f_field(s, "right"), "time_to_verified_result_milliseconds"),
           "[300,600]");
  assert(!strcmp(f_string(s, "modeled_preference"), "unresolved"));
  json_object_put(v);
  with_extra(rid, 100, 200);
  v = data(cli("evaluate", left, right, "--estimates", estimates, 0));
  s = f_field(v, "scheduling");
  nt_equal(f_field(f_field(s, "right"), "critical_path_milliseconds"),
           "[300,600]");
  assert(!strcmp(f_string(s, "modeled_preference"), "left"));
  json_object_put(v);
  /* Estimates above the timeout mark a conflict and never change the budget. */
  write_estimates(lid, 200000, 300000, rid, 10, 20);
  v = data(cli("evaluate", left, right, "--estimates", estimates, 0));
  s = f_field(v, "scheduling");
  nt_equal(f_field(f_field(s, "left"), "estimate_budget_conflict"), "true");
  nt_equal(f_field(f_field(f_field(s, "left"), "hard_budgets"),
                   "timeout_seconds"),
           "180");
  assert(!strcmp(f_string(s, "modeled_preference"), "unresolved"));
  json_object_put(v);
}
static void scalar(const char *dir, const char *name, const char *value) {
  char path[F_PATH];
  nt_path(path, dir, name);
  nt_write(path, value);
}
static void observed(const char *left, const char *lid) {
  char run[F_PATH], steps[F_PATH], step[F_PATH], attempt[F_PATH], path[F_PATH];
  nt_path(run, folder, "run");
  nt_path(steps, run, "steps");
  nt_path(step, steps, "compose");
  nt_path(attempt, step, "attempt-1");
  assert(!mkdir(run, 0700) && !mkdir(steps, 0700) && !mkdir(step, 0700) &&
         !mkdir(attempt, 0700));
  nt_path(path, run, "compiled.json");
  nt_copy_tree(left, path);
  scalar(run, "run-id", "run_0123456789abcdef0123\n");
  char *argv[] = {fleet, "workflow-plan", "observed", run, NULL};
  struct f_capture r = nt_run(argv);
  assert(r.status == 1);
  f_capture_free(&r);
  scalar(step, "attempts", "1\n");
  scalar(step, "started-at", "100\n");
  scalar(attempt, "completed-at", "103\n");
  nt_path(step, steps, "verify");
  assert(!mkdir(step, 0700));
  scalar(step, "attempts", "1\n");
  scalar(step, "started-at", "200\n");
  r = nt_run(argv);
  assert(r.status == 0);
  json_object *v = f_parse(r.out);
  f_capture_free(&r);
  assert(v);
  json_object *entry = f_field(f_field(v, "plans"), lid);
  nt_equal(f_field(f_field(f_field(entry, "steps"), "compose"), "milliseconds"),
           "[2000,4000]");
  assert(!f_field(f_field(entry, "steps"), "verify") &&
         !f_field(f_field(entry, "steps"), "spawn"));
  assert(strstr(f_string(entry, "source"), "1 of 3 steps"));
  nt_json_write(estimates, v);
  json_object_put(v);
  v = data(cli("explain", left, "--estimates", estimates, NULL, 0));
  assert(!f_field(f_field(v, "metrics"), "critical_path_milliseconds") &&
         !f_field(f_field(v, "metrics"), "cost_microunits"));
  json_object_put(v);
}
static void decisions(const char *left, const char *lid, const char *right,
                      const char *rid) {
  char dir[F_PATH], inputs[F_PATH], record[F_PATH], listing[F_PATH], id[17];
  nt_path(listing, folder, "decisions");
  nt_path(dir, listing, "new");
  nt_path(inputs, dir, "inputs");
  assert(!mkdir(listing, 0700) && !mkdir(dir, 0700) && !mkdir(inputs, 0700));
  json_object *v = decision("decision-record", dir, "1700000000", "-", left,
                            "selected reason", right, "rejected reason", 0);
  assert(!f_copy(id, sizeof id, f_string(f_field(v, "data"), "decision_id")));
  nt_equal(f_field(f_field(v, "data"), "rejected"), "1");
  json_object_put(v);
  v = data(decision("decision-show", dir, id, NULL, NULL, NULL, NULL, NULL, 0));
  json_object *rec = f_field(v, "record");
  assert(!strcmp(f_string(f_field(v, "replay"), "status"), "reproduced"));
  assert(!strcmp(f_string(rec, "compiler"), "hydra-plan-1") &&
         !strcmp(f_string(rec, "analysis"), "hydra-plan-evaluate-1"));
  assert(!strcmp(f_string(f_field(rec, "selected"), "sha256"), lid));
  nt_equal(f_field(f_field(rec, "hard_budgets"), "timeout_seconds"), "180");
  nt_equal(f_field(f_field(rec, "estimates"), "sha256"), "null");
  json_object *row = json_object_array_get_idx(f_field(rec, "rejected"), 0);
  assert(!strcmp(f_string(row, "sha256"), rid));
  assert(!strcmp(f_string(f_field(f_field(row, "evaluation"), "decomposition"),
                          "modeled_preference"),
                 "left"));
  json_object_put(v);
  /* The listing replays every record it finds under a valid ID. */
  char moved[F_PATH];
  nt_path(moved, listing, id);
  assert(!rename(dir, moved));
  v = data(decision("decision-list", listing, NULL, NULL, NULL, NULL, NULL,
                    NULL, 0));
  assert(json_object_array_length(f_field(v, "decisions")) == 1);
  assert(!strcmp(f_string(json_object_array_get_idx(f_field(v, "decisions"), 0),
                          "replay"),
                 "reproduced"));
  json_object_put(v);
  /* A changed comparison or input no longer replays. */
  nt_path(record, moved, "decision.json");
  char *text = f_read(record, 1000000), *changed;
  assert(text);
  changed = nt_replace(text, "\"unconnected_nodes\":1", "\"unconnected_nodes\":0");
  nt_write(record, changed);
  json_object_put(decision("decision-show", moved, id, NULL, NULL, NULL, NULL,
                           NULL, 1));
  nt_write(record, text);
  json_object_put(decision("decision-show", moved, id, NULL, NULL, NULL, NULL,
                           NULL, 0));
  char input[F_PATH], name[80];
  assert(snprintf(name, sizeof name, "inputs/%s.json", rid) > 0);
  nt_path(input, moved, name);
  char *stored = f_read(input, 1000000);
  assert(stored);
  changed = nt_replace(stored, "\"timeout\":30", "\"timeout\":31");
  nt_write(input, changed);
  v = data(decision("decision-show", moved, id, NULL, NULL, NULL, NULL, NULL, 0));
  assert(!strcmp(f_string(f_field(v, "replay"), "status"), "diverged"));
  json_object_put(v);
  nt_write(input, stored);
  free(stored);
  free(text);
  /* Selecting an alternative whose estimate exceeds its hard budget is
   * refused: estimates never relax budgets. Duplicates are refused too. */
  nt_path(dir, listing, "again");
  nt_path(inputs, dir, "inputs");
  assert(!mkdir(dir, 0700) && !mkdir(inputs, 0700));
  write_estimates(lid, 200000, 300000, rid, 10, 20);
  v = decision("decision-record", dir, "1700000001", estimates, left, "s",
               right, "r", 1);
  assert(!strcmp(f_string(f_field(v, "error"), "code"),
                 "estimate_budget_conflict"));
  json_object_put(v);
  v = decision("decision-record", dir, "1700000001", "-", left, "s", left, "r",
               1);
  assert(!strcmp(f_string(f_field(v, "error"), "code"),
                 "duplicate_alternative"));
  json_object_put(v);
  write_estimates(lid, 10, 20, rid, 100, 200);
  v = decision("decision-record", dir, "1700000001", estimates, left, "s",
               right, "r", 0);
  json_object_put(v);
  json_object_put(decision("decision-show", dir, NULL, NULL, NULL, NULL, NULL,
                           NULL, 1));
}
int main(void) {
  char tmp[] = "/tmp/hydra-plan-evaluate-XXXXXX", source[F_PATH], home[F_PATH],
       left[F_PATH], right[F_PATH], lid[65], rid[65];
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
  assert(!f_copy(fleet, sizeof fleet, getenv("HYDRA_FLEET_BIN")));
  assert(!chdir(repo));
  compile("left", NULL, left, lid);
  json_object *changed = with_unconnected_step();
  compile("right", changed, right, rid);
  json_object_put(changed);
  concerns(left, lid, right, rid);
  observed(left, lid);
  decisions(left, lid, right, rid);
  json_object_put(plan);
  assert(!chdir(origin));
  nt_finish(folder, "Plan evaluation: concerns, observed durations and decisions passed");
  return 0;
}
