#include "performance_fixture.h"
static struct outcome_fixture f;
static char subject[F_PATH], raw[F_PATH];
static json_object *compose(json_object *manifest, json_object *rows) {
  char path[F_PATH];
  nt_path(path, f.inputs, "manifest");
  nt_json_write(path, manifest);
  measurement_csv(raw, rows, false, false);
  outcome_check("performance-analyze", 0);
  nt_path(path, f.outputs, "report.json");
  nt_copy_tree(path, subject);
  json_object *r = f_read_json(subject, 1000000);
  assert(r);
  return r;
}
static void check(bool success) {
  outcome_check("performance-check", success ? 0 : 1);
  char path[F_PATH];
  nt_path(path, f.outputs, "assessment");
  json_object *r = f_read_json(path, 1000000);
  assert(r);
  nt_equal(f_field(r, "schema_version"), "3");
  assert(!strcmp(f_string(r, "domain_verdict"), success ? "pass" : "fail"));
  json_object *records = f_field(r, "evidence_records");
  assert(json_object_array_length(records) == 5);
  for (size_t i = 0; i < 5; i++) {
    json_object *rec = json_object_array_get_idx(records, i);
    nt_equal(f_field(rec, "counts"),
             success ? "{\"executed\":1,\"failed\":0,\"skipped\":0}"
                     : "{\"executed\":1,\"failed\":1,\"skipped\":0}");
    assert(json_object_is_type(
        f_field(
            f_field(json_object_array_get_idx(f_field(rec, "observations"), 0),
                    "raw"),
            "measurement"),
        json_type_int));
  }
  json_object_put(r);
}
static void positive(json_object *m, json_object *rows) {
  json_object *r = compose(m, rows);
  assert(!strcmp(f_string(r, "outcome"), "target established"));
  json_object_put(r);
  check(true);
  json_object *same = measurement_rows(1.0);
  r = compose(m, same);
  assert(!strcmp(f_string(r, "outcome"), "target not established"));
  assert(json_object_get_double(
             f_field(f_field(r, "paired_uncertainty"), "estimate")) == 0);
  json_object_put(r);
  json_object_put(same);
  check(true);
}
static void invalid_rows(json_object *m, json_object *rows) {
  for (size_t i = 0; i < 7; i++) {
    json_object *changed = nt_clone(rows),
                *first = json_object_array_get_idx(changed, 0);
    if (i == 0)
      assert(!json_object_array_del_idx(changed, 19, 1));
    if (i == 1)
      assert(!json_object_array_put_idx(changed, 1, nt_clone(first)));
    if (i == 2)
      f_string_add(first, "count", "4002");
    if (i == 3)
      f_string_add(first, "returncode", "7");
    if (i == 4)
      f_string_add(first, "elapsed_ns", "0");
    if (i == 5)
      f_string_add(first, "order", "1");
    if (i == 6)
      f_string_add(first, "elapsed_ns", "9223372036854775807");
    json_object *r = compose(m, changed);
    assert(!strcmp(f_string(r, "outcome"), "invalid/insufficient measurement"));
    json_object_put(r);
    json_object_put(changed);
    check(false);
  }
}
static void manifest_faults(json_object *m, json_object *rows) {
  for (size_t i = 0; i < 8; i++) {
    json_object *v = nt_clone(m), *env = f_field(v, "environment"),
                *warmups = f_field(v, "warmups");
    if (i == 0)
      f_string_add(
          f_field(f_field(v, "sources"), "baseline"), "sha256",
          "0000000000000000000000000000000000000000000000000000000000000000");
    if (i == 1)
      f_string_add(f_field(env, "source_git"), "tree",
                   "0000000000000000000000000000000000000000");
    if (i == 2) {
      json_object *w = f_field(v, "workload");
      json_object_object_add(
          w, "bytes",
          json_object_new_int64(json_object_get_int64(f_field(w, "bytes")) +
                                1));
    }
    if (i == 3)
      f_string_add(env, "units", "milliseconds");
    if (i == 4)
      json_object_object_del(f_field(v, "protocol"), "stopping_rule");
    if (i == 5)
      json_object_object_add(v, "warmups", json_object_new_array());
    if (i == 6)
      json_object_object_add(json_object_array_get_idx(warmups, 0),
                             "returncode", json_object_new_int(7));
    if (i == 7)
      json_object_object_add(
          v, "failures",
          f_parse_value("[{\"phase\":\"warmup\",\"returncode\":7}]"));
    json_object_put(compose(v, rows));
    json_object_put(v);
    check(false);
  }
}
static void subject_faults(json_object *m, json_object *rows) {
  for (size_t i = 0; i < 6; i++) {
    json_object *r = compose(m, rows);
    if (i == 0)
      assert(!json_object_array_put_idx(
          f_field(f_field(f_field(r, "manifest"), "commands"), "baseline"), 0,
          json_object_new_string("/different/tool")));
    if (i == 1) {
      json_object *a = f_field(r, "raw_samples");
      const char *s = json_object_get_string(json_object_array_get_idx(a, 1));
      size_t n = strlen(s);
      char *changed = malloc(n + 2);
      assert(changed);
      memcpy(changed, s, n);
      changed[n] = '0';
      changed[n + 1] = 0;
      assert(!json_object_array_put_idx(a, 1, json_object_new_string(changed)));
      free(changed);
    }
    if (i == 2)
      f_string_add(r, "outcome", "target not established");
    if (i == 3)
      json_object_object_add(f_field(r, "paired_uncertainty"), "upper",
                             json_object_new_int(-100));
    if (i == 4)
      json_object_object_add(
          r, "limits", f_parse_value("[\"General speedup established\"]"));
    if (i == 5)
      json_object_object_add(r, "hidden_exclusion", json_object_new_int(1));
    nt_json_write(subject, r);
    json_object_put(r);
    check(false);
  }
}
static void csv_boundaries(json_object *m, json_object *rows) {
  json_object_put(compose(m, rows));
  char output[F_PATH];
  nt_path(output, f.outputs, "report.json");
  for (size_t i = 0; i < 2; i++) {
    measurement_csv(raw, rows, i == 0, true);
    outcome_check("performance-analyze", 0);
    nt_copy_tree(output, subject);
    check(i != 0);
  }
  const char *bad[] = {
      "bad,header\n1,2\n",
      "sample_id,implementation,trial,order,elapsed_ns,status,count,"
      "returncode\n\"unterminated,baseline,1,0,1000,ok,4003,0\n"};
  for (size_t i = 0; i < 2; i++) {
    nt_write(raw, bad[i]);
    outcome_check("performance-analyze", 0);
    nt_copy_tree(output, subject);
    json_object *r = f_read_json(subject, 1000000);
    assert(!strcmp(f_string(r, "outcome"), "invalid/insufficient measurement"));
    json_object_put(r);
    check(false);
  }
}
static void measurement_gate(void) {
  assert(!unsetenv("HYDRA_PERFORMANCE_EXCLUSIVE"));
  char *argv[] = {"./plan-example", "performance-measure", NULL};
  struct f_capture c = nt_run(argv);
  assert(c.status && strstr(c.err, "coordinated measurement window"));
  f_capture_free(&c);
  char path[F_PATH];
  nt_path(path, f.outputs, "raw.csv");
  assert(access(path, F_OK));
}
static size_t nul_samples(const char *bytes, size_t size) {
  size_t count = 0;
  for (size_t i = 0; i < size; i++) {
    if (bytes[i] != '\0')
      continue;
    assert(i >= 4 && size - i >= 4);
    assert(!memcmp(bytes + i - 4, "4003\0bad", 8));
    count++;
  }
  return count;
}
static void nul_output(void) {
  /* Deliberately invalid stdout exercises capture, not a speedup experiment. */
  nt_write("baseline.sh", "#!/bin/sh\nprintf '4003\\000bad\\n'\n");
  nt_write("candidate.sh", "#!/bin/sh\nprintf '4003\\n'\n");
  outcome_commit();
  assert(!setenv("HYDRA_PERFORMANCE_EXCLUSIVE", "1", 1));
  outcome_check("performance-measure", 0);
  assert(!unsetenv("HYDRA_PERFORMANCE_EXCLUSIVE"));
  char path[F_PATH], input[F_PATH];
  nt_path(path, f.outputs, "manifest.json");
  json_object *manifest = f_read_json(path, 1000000);
  json_object *failures = f_field(manifest, "failures");
  assert(json_object_array_length(failures) == 12);
  for (size_t i = 0; i < 12; i++) {
    json_object *failure = json_object_array_get_idx(failures, i);
    assert(!strcmp(f_string(failure, "implementation"), "baseline"));
    json_object *count = f_field(failure, "count");
    assert(json_object_get_string_len(count) == 8);
    assert(!memcmp(json_object_get_string(count), "4003\0bad", 8));
  }
  json_object_put(manifest);
  nt_path(input, f.inputs, "manifest");
  nt_copy_tree(path, input);
  nt_path(path, f.outputs, "raw.csv");
  struct stat st;
  assert(!stat(path, &st));
  char *bytes = f_read(path, 1000000);
  assert(bytes && nul_samples(bytes, (size_t)st.st_size) == 10);
  free(bytes);
  nt_copy_tree(path, raw);
  outcome_check("performance-analyze", 0);
  nt_path(path, f.outputs, "report.json");
  nt_copy_tree(path, subject);
  json_object *report = f_read_json(subject, 1000000);
  assert(
      !strcmp(f_string(report, "outcome"), "invalid/insufficient measurement"));
  json_object *lines = f_field(report, "raw_samples");
  assert(json_object_array_length(lines) == 21);
  size_t preserved = 0;
  for (size_t i = 0; i < 21; i++) {
    json_object *line = json_object_array_get_idx(lines, i);
    preserved += nul_samples(json_object_get_string(line),
                             (size_t)json_object_get_string_len(line));
  }
  assert(preserved == 10);
  json_object_put(report);
  check(false);
}
int main(void) {
  outcome_setup(&f, "performance");
  assert(!setenv("HYDRA_WORKFLOW_REPO_ROOT", f.repo, 1));
  char path[F_PATH];
  nt_path(path, f.inputs, "records");
  nt_copy_tree("records.txt", path);
  nt_path(subject, f.inputs, "subject");
  nt_path(raw, f.inputs, "raw");
  nt_path(path, f.folder, "validation.json");
  nt_write(
      path,
      "{\"data\":{\"performance-check\":"
      "\"1111111111111111111111111111111111111111111111111111111111111111\","
      "\"performance-check-recipe\":"
      "\"2222222222222222222222222222222222222222222222222222222222222222\"}}");
  json_object *m = measurement_manifest(&f), *rows = measurement_rows(.6);
  positive(m, rows);
  invalid_rows(m, rows);
  manifest_faults(m, rows);
  subject_faults(m, rows);
  csv_boundaries(m, rows);
  measurement_gate();
  nul_output();
  json_object_put(m);
  json_object_put(rows);
  outcome_finish(&f,
                 "Performance: five original groups plus CSV, integer-range "
                 "and NUL-output regressions passed");
  return 0;
}
