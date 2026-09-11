#include "outcome_support.h"
static struct outcome_fixture f;
static char subject[F_PATH], assessment[F_PATH];
static json_object *check(int status) {
  outcome_check("research-check", status);
  json_object *v = f_read_json(assessment, 1000000);
  assert(v);
  assert(!strcmp(f_string(v, "verdict"), status ? "fail" : "pass"));
  return v;
}
static void mutations(json_object *original) {
  for (size_t i = 0; i < 7; i++) {
    json_object *v = nt_clone(original), *claims = f_field(v, "claims"),
                *provenance = f_field(v, "provenance");
    if (i == 0)
      f_string_add(claims, "recommendation", "SJF is generally best");
    if (i == 1)
      f_string_add(v, "question", "different");
    if (i == 2)
      f_string_add(claims, "question", "different");
    if (i == 3)
      f_string_add(provenance, "method", "hand edited");
    if (i == 4)
      f_string_add(provenance, "source", "jobs.csv");
    if (i == 5)
      json_object_object_add(v, "claim_locations",
                             f_parse_value("[\"/claims/recommendation\"]"));
    if (i == 6)
      json_object_object_add(claims, "competing_explanations",
                             f_parse_value("[\"one\",\"two\"]"));
    nt_json_write(subject, v);
    json_object_put(v);
    json_object_put(check(1));
  }
}
int main(void) {
  outcome_setup(&f, "research");
  char jobs[F_PATH], path[F_PATH];
  nt_path(jobs, f.inputs, "jobs");
  nt_copy_tree("jobs.csv", jobs);
  nt_path(subject, f.inputs, "subject");
  nt_path(assessment, f.outputs, "assessment");
  nt_path(path, f.folder, "validation.json");
  nt_write(
      path,
      "{\"data\":{\"assessment\":"
      "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
      "\"assessment-recipe\":"
      "\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}");
  outcome_check("research-report", 0);
  nt_path(path, f.outputs, "report.json");
  nt_copy_tree(path, subject);
  json_object *original = f_read_json(subject, 1000000), *v = check(0);
  assert(!strcmp(
      f_string(v, "validator_sha256"),
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  json_object *rec =
      json_object_array_get_idx(f_field(v, "evidence_records"), 0);
  assert(!strcmp(
      f_string(rec, "validator_recipe_sha256"),
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  nt_equal(f_field(rec, "case_inventory"),
           "[\"fcfs\",\"sjf\",\"recommendation\",\"scope\",\"provenance\"]");
  json_object_put(v);
  mutations(original);
  nt_write(subject, "{");
  json_object_put(check(1));
  nt_json_write(subject, original);
  json_object_put(original);
  nt_write(jobs, "id,arrival,duration\nA,0,nope\n");
  v = check(1);
  assert(!strcmp(f_string(v, "evidence_status"), "invalid") &&
         !strcmp(f_string(v, "domain_verdict"), "fail"));
  json_object_put(v);
  outcome_finish(&f, "Research outcome: all five original case groups passed");
  return 0;
}
