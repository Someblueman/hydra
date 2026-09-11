#include "support.h"
static const char *core;
static const char *fixture = "tests/fixtures/tui/statistics-v2.tsv";
static struct f_capture call(const char *command, const char *left,
                             const char *right, const char *a, const char *b,
                             const char *c, const char *d) {
  char *args[] = {(char *)core,  (char *)command, (char *)left,
                  (char *)right, (char *)a,       (char *)b,
                  (char *)c,     (char *)d,       NULL};
  return nt_run(args);
}
static json_object *load(const char *file) {
  struct f_capture c =
      call("statistics-json", file, NULL, NULL, NULL, NULL, NULL);
  json_object *v = nt_output(&c, 0);
  f_capture_free(&c);
  return v;
}
static json_object *compare(const char *file) {
  struct f_capture c =
      call("statistics-compare", fixture, file, NULL, NULL, NULL, NULL);
  json_object *v = nt_output(&c, 0);
  f_capture_free(&c);
  return v;
}
static void malformed(const char *file) {
  struct f_capture c =
      call("statistics-json", file, NULL, NULL, NULL, NULL, NULL);
  assert(c.status && !*c.out);
  f_capture_free(&c);
  c = call("statistics-compare", fixture, file, "--format", "text", NULL, NULL);
  assert(c.status && !*c.out);
  f_capture_free(&c);
}
static void text_fields(const char *text, const char *side, const char *name,
                        json_object *metric) {
  char prefix[128];
  int n = snprintf(prefix, sizeof prefix, "%s %s ", side, name);
  assert(n > 0 && (size_t)n < sizeof prefix);
  const char *line = text;
  while (*line && strncmp(line, prefix, strlen(prefix))) {
    line = strchr(line, '\n');
    assert(line);
    line++;
  }
  assert(*line);
  const char *end = strchr(line, '\n');
  size_t len = end ? (size_t)(end - line) : strlen(line);
  char *copy = malloc(len + 1);
  assert(copy);
  memcpy(copy, line, len);
  copy[len] = 0;
  char *save, *field = strtok_r(copy + strlen(prefix), " ", &save);
  json_object *actual = json_object_new_object();
  while (field) {
    char *eq = strchr(field, '=');
    assert(eq);
    *eq++ = 0;
    f_string_add(actual, field, eq);
    field = strtok_r(NULL, " ", &save);
  }
  if (strcmp(side, "delta")) {
    assert(!strcmp(f_string(actual, "unit"),
                   !strcmp(name, "recoveries") ? "count" : "seconds"));
    json_object_object_del(actual, "unit");
  }
  assert(json_object_object_length(actual) ==
         json_object_object_length(metric));
  json_object_object_foreach(metric, key, value) {
    const char *expected = value ? json_object_get_string(value) : "unknown";
    assert(!strcmp(f_string(actual, key), expected));
  }
  json_object_put(actual);
  free(copy);
}
int main(int argc, char **argv) {
  char folder[] = "/tmp/hydra-stat-export-XXXXXX", recorded[F_PATH],
       mixed[F_PATH], recovered[F_PATH], bad[F_PATH], partial[F_PATH],
       oversized[F_PATH], different[F_PATH], empty[F_PATH], missing[F_PATH],
       home[F_PATH];
  core = argc > 1 ? argv[1] : "build/hydra-core";
  json_object *v = load(fixture);
  nt_equal(f_field(v, "schema_version"), "1");
  nt_equal(f_field(v, "observed"), "1788789600");
  nt_equal(f_field(v, "cohort"), "{\"runs\":8,\"steps\":11}");
  nt_equal(f_field(f_field(v, "metrics"), "queue"),
           "{\"state\":\"known\",\"eligible\":10,\"known\":7,\"sum\":70,"
           "\"mean\":10,\"max\":10,\"p50\":10,\"p95\":10}");
  nt_equal(f_field(f_field(f_field(v, "metrics"), "verified"), "known"), "2");
  assert(!strcmp(f_string(f_field(v, "remote"), "state"), "unavailable"));
  nt_equal(
      f_field(v, "recovery_outcomes"),
      "{\"scope\":\"coordinator_owner_recovery\",\"eligible\":2,\"known_"
      "terminal\":1,\"succeeded\":0,\"failed_or_cancelled\":1,\"unknown\":1,"
      "\"missing_recovery_history\":2,\"success_fraction_among_known\":0}");
  assert(!f_field(f_field(v, "unmeasured"), "manual_interventions"));
  nt_equal(f_field(f_field(v, "coverage"), "partial"), "false");
  json_object_put(v);
  assert(mkdtemp(folder));
  nt_path(recorded, folder, "recorded-v3.tsv");
  nt_path(mixed, folder, "mixed.tsv");
  nt_path(recovered, folder, "recovered.tsv");
  nt_path(bad, folder, "malformed.tsv");
  nt_path(partial, folder, "partial.tsv");
  nt_path(oversized, folder, "oversized.tsv");
  nt_path(different, folder, "different.tsv");
  nt_path(empty, folder, "empty.tsv");
  nt_path(missing, folder, "missing.tsv");
  nt_path(home, folder, "home");
  const char *record =
      "HYDRA_STATISTICS\t3\t1788789600\nR\trun_a\tworkflow\tsucceeded\tp\t2026-"
      "09-07T13:00:00Z\tcomplete\t1788787800\t1788787900\t-"
      "\t0\t0\t2\t3\t4096\nZ\t1\t0\n";
  nt_write(recorded, record);
  v = load(recorded);
  nt_equal(f_field(v, "schema_version"), "2");
  nt_equal(f_field(v, "unmeasured"),
           "{\"total_manual_interventions\":null,\"network_transfer_bytes\":"
           "null,\"provider_usage\":null}");
  nt_equal(
      f_field(v, "recorded"),
      "{\"unknown_receiver_outcomes\":{\"state\":\"known\",\"eligible\":1,"
      "\"known\":1,\"sum\":2},\"recorded_operator_actions\":{\"state\":"
      "\"known\",\"eligible\":1,\"known\":1,\"sum\":3},\"transport_stdio_"
      "bytes\":{\"state\":\"known\",\"eligible\":1,\"known\":1,\"sum\":4096}}");
  json_object_put(v);
  char *s = nt_replace(
      record, "Z\t1\t0\n",
      "R\trun_b\tworkflow\tsucceeded\tp\t2026-09-07T13:00:"
      "00Z\tcomplete\t1788787800\t1788787900\t-\t0\t0\t-\t-\t-\nZ\t2\t0\n");
  nt_write(mixed, s);
  free(s);
  v = load(mixed);
  json_object_object_foreach(f_field(v, "recorded"), key, value) {
    (void)key;
    nt_equal(value,
             "{\"state\":\"partial\",\"eligible\":2,\"known\":1,\"sum\":null}");
  }
  json_object_put(v);
  v = compare(recorded);
  nt_equal(f_field(v, "schema_version"), "2");
  nt_equal(f_field(f_field(v, "left"), "schema_version"), "1");
  nt_equal(f_field(f_field(v, "right"), "schema_version"), "2");
  json_object_put(v);
  char *original = f_read(fixture, 2000000);
  assert(original);
  s = nt_replace(original, "1788699790\t0\t1", "1788699790\t1\t1");
  char *t = nt_replace(s, "Z\t8\t11", "X\tMore records exist\nZ\t8\t11");
  free(s);
  nt_write(recovered, t);
  free(t);
  v = load(recovered);
  nt_equal(f_field(f_field(v, "recovery_outcomes"), "succeeded"), "1");
  nt_equal(
      f_field(f_field(v, "recovery_outcomes"), "success_fraction_among_known"),
      "0.5");
  nt_equal(f_field(v, "coverage"),
           "{\"partial\":true,\"partial_runs\":0,\"warnings\":1,\"last_"
           "warning\":\"More records exist\"}");
  json_object_put(v);
  nt_write(bad, "HYDRA_STATISTICS\t2\t10\n");
  malformed(bad);
  s = nt_replace(original, "Z\t8\t11\n", "");
  nt_write(partial, s);
  free(s);
  malformed(partial);
  FILE *f = fopen(oversized, "w");
  assert(f);
  assert(fputs("HYDRA_STATISTICS\t2\t10\nX\t", f) >= 0);
  for (size_t i = 0; i < 1024 * 1024; i++)
    assert(fputc('x', f) != EOF);
  assert(fputc('\n', f) != EOF && !fclose(f));
  malformed(oversized);
  v = compare(fixture);
  nt_equal(f_field(f_field(f_field(v, "delta"), "metrics"), "queue"),
           "{\"state\":\"known\",\"mean\":0,\"max\":0,\"p50\":0,\"p95\":0}");
  json_object_put(v);
  s = nt_replace(original, "1788787810", "1788787820");
  nt_write(different, s);
  free(s);
  nt_write(empty, "HYDRA_STATISTICS\t2\t1788789600\nZ\t0\t0\n");
  f = fopen(missing, "w");
  assert(f);
  s = strdup(original);
  assert(s);
  char *save, *line = strtok_r(s, "\n", &save);
  assert(line);
  assert(fprintf(f, "%s\n", line) > 0);
  while ((line = strtok_r(NULL, "\n", &save)))
    if (strstr(line, "run_dddddddddddddddddddd"))
      assert(fprintf(f, "%s\n", line) > 0);
  assert(fputs("Z\t1\t1\n", f) >= 0 && !fclose(f));
  free(s);
  free(original);
  const char *rights[] = {fixture, different, empty, missing, recovered};
  for (size_t i = 0; i < 5; i++) {
    v = compare(rights[i]);
    struct f_capture c = call("statistics-compare", fixture, rights[i],
                              "--format", "text", NULL, NULL);
    assert(!c.status && !*c.err);
    for (char *p = c.out; *p; p++)
      assert(*p == '\n' ||
             ((unsigned char)*p >= 32 && (unsigned char)*p < 127));
    assert(strstr(c.out, "delta is right minus left"));
    const char *sides[] = {"delta", "left", "right"};
    for (size_t j = 0; j < 3; j++) {
      json_object *m = f_field(f_field(v, sides[j]), "metrics");
      json_object_object_foreach(m, name, metric) {
        text_fields(c.out, sides[j], name, metric);
      }
    }
    assert(strstr(c.out, "network_transfer_bytes") &&
           strstr(c.out, "provider_usage"));
    f_capture_free(&c);
    json_object_put(v);
  }
  const char *options[][4] = {{"--format", "html", NULL, NULL},
                              {"--format", NULL, NULL, NULL},
                              {"--format", "text", "--format", "json"}};
  for (size_t i = 0; i < 3; i++) {
    struct f_capture c =
        call("statistics-compare", fixture, fixture, options[i][0],
             options[i][1], options[i][2], options[i][3]);
    assert(c.status && !*c.out);
    f_capture_free(&c);
  }
  assert(!setenv("HYDRA_CORE", core, 1) && !setenv("HYDRA_HOME", home, 1));
  char *args[] = {"bin/hydra",
                  "workflow",
                  "statistics-compare",
                  (char *)fixture,
                  (char *)fixture,
                  "--format",
                  "text",
                  NULL};
  struct f_capture public = nt_run(args),
                   expected = call("statistics-compare", fixture, fixture,
                                   "--format", "text", NULL, NULL);
  if (public.status)
    fprintf(stderr, "%s", public.err);
  assert(!public.status && !strcmp(public.out, expected.out));
  f_capture_free(&public);
  f_capture_free(&expected);
  nt_finish(
      folder,
      "Statistics exporter JSON, comparison and fail-closed bounds passed");
  return 0;
}
