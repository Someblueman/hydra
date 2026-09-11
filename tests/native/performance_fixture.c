#define _XOPEN_SOURCE 700
#include "performance_fixture.h"
#include <sys/utsname.h>
static json_object *source_info(const char *path, const char *label) {
  char hash[65];
  struct stat st;
  assert(!stat(path, &st) && !f_hash(path, hash));
  json_object *v = json_object_new_object();
  f_string_add(v, "path", label);
  f_string_add(v, "sha256", hash);
  json_object_object_add(v, "bytes", json_object_new_int64(st.st_size));
  return v;
}
static char *tool(const char *name) {
  char *argv[] = {"sh",      "-c",         "command -v \"$1\"",
                  "fixture", (char *)name, NULL};
  struct f_capture c = nt_run(argv);
  assert(!c.status);
  c.out[strcspn(c.out, "\r\n")] = 0;
  char *p = realpath(c.out, NULL);
  assert(p);
  f_capture_free(&c);
  return p;
}
static json_object *environment(struct outcome_fixture *f) {
  struct utsname u;
  assert(!uname(&u));
  char platform[F_PATH], native[F_PATH];
  assert(snprintf(platform, sizeof platform, "%s %s", u.sysname, u.release) >
         0);
  json_object *e =
      f_parse("{\"timer\":\"clock_gettime(CLOCK_MONOTONIC)\",\"units\":"
              "\"nanoseconds\",\"load_before\":[0.0,0.0,0.0],\"load_after\":[0."
              "0,0.0,0.0],\"source_git\":{},\"tools\":{}}");
  f_string_add(e, "toolchain", "C99/" __VERSION__);
  f_string_add(e, "platform", platform);
  f_string_add(e, "machine", u.machine);
  f_string_add(e, "cwd", f->repo);
  const char *refs[] = {"HEAD", "HEAD^{tree}"}, *keys[] = {"commit", "tree"};
  for (size_t i = 0; i < 2; i++) {
    char *argv[] = {"git", "rev-parse", (char *)refs[i], NULL};
    struct f_capture c = nt_run(argv);
    assert(!c.status);
    c.out[strcspn(c.out, "\r\n")] = 0;
    f_string_add(f_field(e, "source_git"), keys[i], c.out);
    f_capture_free(&c);
  }
  nt_path(native, f->repo, "plan-example");
  char *paths[] = {tool("sh"), tool("awk"), realpath(native, NULL)};
  const char *names[] = {"shell", "awk", "native"};
  for (size_t i = 0; i < 3; i++) {
    assert(paths[i]);
    char hash[65];
    assert(!f_hash(paths[i], hash));
    json_object *t = json_object_new_object();
    f_string_add(t, "path", paths[i]);
    f_string_add(t, "sha256", hash);
    json_object_object_add(f_field(e, "tools"), names[i], t);
    free(paths[i]);
  }
  return e;
}
json_object *measurement_manifest(struct outcome_fixture *f) {
  json_object *m = f_parse("{\"schema_version\":2,\"commands\":{},\"sources\":{"
                           "},\"warmups\":[],\"failures\":[]}");
  char records[F_PATH];
  nt_path(records, f->inputs, "records");
  json_object_object_add(m, "workload", source_info(records, "records.txt"));
  json_object *env = environment(f);
  json_object_object_add(m, "environment", env);
  json_object_object_add(
      m, "protocol",
      f_parse("{\"warmups\":2,\"trials\":10,\"order\":\"alternating_AB_BA\","
              "\"stopping_rule\":\"exactly_10_pairs_no_exclusions_or_retries\","
              "\"sample_timeout_seconds\":10,\"expected_count\":\"4003\","
              "\"exclusions\":[],\"exclusive_hydra_measurement\":true,"
              "\"exclusivity_scope\":\"coordinated Hydra jobs; other system "
              "activity is observed, not excluded\"}"));
  const char *names[] = {"baseline", "candidate"};
  for (size_t i = 0; i < 2; i++) {
    char label[40], path[F_PATH];
    assert(snprintf(label, sizeof label, "%s.sh", names[i]) > 0);
    nt_path(path, f->repo, label);
    json_object_object_add(f_field(m, "sources"), names[i],
                           source_info(path, label));
    json_object *cmd = json_object_new_array();
    json_object_array_add(
        cmd, json_object_new_string(
                 f_string(f_field(f_field(env, "tools"), "shell"), "path")));
    json_object_array_add(cmd, json_object_new_string(path));
    json_object_array_add(cmd, json_object_new_string(records));
    json_object_object_add(f_field(m, "commands"), names[i], cmd);
  }
  for (int i = 0; i < 4; i++) {
    json_object *r = f_parse("{\"elapsed_ns\":1000,\"status\":\"ok\",\"count\":"
                             "\"4003\",\"returncode\":0}");
    f_string_add(r, "implementation", names[i % 2]);
    json_object_object_add(r, "warmup", json_object_new_int(i / 2 + 1));
    json_object_array_add(f_field(m, "warmups"), r);
  }
  return m;
}
json_object *measurement_rows(double multiplier) {
  json_object *rows = json_object_new_array();
  for (int trial = 1; trial <= 10; trial++)
    for (int pos = 0; pos < 2; pos++) {
      bool candidate = (pos + (trial % 2 == 0)) % 2;
      const char *name = candidate ? "candidate" : "baseline";
      json_object *r = f_parse(
          "{\"status\":\"ok\",\"count\":\"4003\",\"returncode\":\"0\"}");
      char id[40], number[40];
      assert(snprintf(id, sizeof id, "%02d-%s", trial, name) > 0);
      f_string_add(r, "sample_id", id);
      f_string_add(r, "implementation", name);
      assert(snprintf(number, sizeof number, "%d", trial) > 0);
      f_string_add(r, "trial", number);
      assert(snprintf(number, sizeof number, "%d", pos) > 0);
      f_string_add(r, "order", number);
      assert(snprintf(number, sizeof number, "%d",
                      (int)((1000 + trial * 13) *
                            (candidate ? multiplier : 1.0))) > 0);
      f_string_add(r, "elapsed_ns", number);
      json_object_array_add(rows, r);
    }
  return rows;
}
void measurement_csv(const char *path, json_object *rows, bool reverse,
                     bool quoted) {
  const char *fields[] = {"sample_id", "implementation", "trial",
                          "order",     "elapsed_ns",     "status",
                          "count",     "returncode"};
  FILE *file = fopen(path, "w");
  assert(file);
  for (size_t i = 0; i < 8; i++)
    assert(fprintf(file, "%s%s%s%s", quoted ? "\"" : "",
                   fields[reverse ? 7 - i : i], quoted ? "\"" : "",
                   i == 7 ? "\r\n" : ",") > 0);
  for (size_t row = 0; row < json_object_array_length(rows); row++)
    for (size_t i = 0; i < 8; i++) {
      const char *value = f_string(json_object_array_get_idx(rows, row),
                                   fields[reverse ? 7 - i : i]);
      assert(value);
      assert(fprintf(file, "%s%s%s%s", quoted ? "\"" : "", value,
                     quoted ? "\"" : "", i == 7 ? "\r\n" : ",") > 0);
    }
  assert(!fclose(file));
}
