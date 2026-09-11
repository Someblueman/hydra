#include "support.h"
static const char *fleet;
static char root[F_PATH], step[F_PATH], remote[F_PATH];
static void write_at(const char *base, const char *name, const char *text) {
  char p[F_PATH];
  nt_path(p, base, name);
  nt_write(p, text);
}
static void events(bool actions) {
  char p[F_PATH];
  FILE *f;
  const char *types[] = {"run.created", "run.recovered", "run.resume_requested",
                         "approval.decided", "run.cancel_requested"};
  nt_path(p, root, "events.jsonl");
  f = fopen(p, "w");
  assert(f);
  for (int i = 0; i < (actions ? 5 : 2); i++)
    assert(fprintf(f,
                   "{\"schema_version\":1,\"sequence\":%d,\"run_id\":\"run_"
                   "fixture\",\"occurred_at\":\"2026-09-10T10:00:00Z\",\"step_"
                   "id\":null,\"type\":\"%s\",\"detail\":\"\"}\n",
                   i + 1, types[i]) > 0);
  assert(!fclose(f));
}
static void attempt(int n, const char *state) {
  char name[64], r[F_PATH], p[F_PATH];
  json_object *v;
  assert(snprintf(name, sizeof name, "attempt-%d/remote", n) > 0);
  nt_path(r, step, name);
  assert(!f_mkdirs(r));
  v = f_parse("{\"task_id\":\"task_"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "\",\"spec_sha256\":"
              "\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
              "bb\",\"submission_key\":\"fixture\",\"runtime\":{\"schema_"
              "version\":1,\"state\":\"succeeded\",\"launch_intent\":"
              "\"started\",\"run_id\":\"run_receiver\",\"exit_status\":0}}");
  f_string_add(f_field(v, "runtime"), "state", state);
  nt_path(p, r, "receipt.json");
  nt_json_write(p, v);
  nt_path(p, r, "observation.json");
  nt_json_write(p, v);
  json_object_put(v);
  write_at(r, "transport-metrics.json",
           "{\"schema_version\":1,\"calls\":2,\"request_bytes\":10,\"response_"
           "bytes\":20,\"complete\":true}");
}
static void setup(void) {
  char tmp[] = "/tmp/hydra-native-metrics-XXXXXX";
  assert(mkdtemp(tmp));
  assert(!f_copy(root, sizeof root, tmp));
  nt_path(step, root, "steps/work");
  assert(!f_mkdirs(step));
  nt_path(remote, step, "attempt-1/remote");
  write_at(root, "run-id", "run_fixture\n");
  write_at(root, "tasks.json",
           "{\"schema_version\":1,\"steps\":{\"work\":{}}}");
  write_at(step, "attempts", "1\n");
  attempt(1, "succeeded");
  events(false);
}
static json_object *read_metrics(void) {
  char *args[] = {(char *)fleet, "workflow-task", "metrics", root, NULL};
  struct f_capture c = nt_run(args);
  json_object *v = nt_output(&c, 0), *data = nt_clone(f_field(v, "data"));
  json_object_put(v);
  f_capture_free(&c);
  args[2] = "metrics-tsv";
  c = nt_run(args);
  assert(!c.status);
  char *save, *line = strtok_r(c.out, "\n", &save);
  size_t rows = 0;
  while (line) {
    char name[128], state[32], sum[64], extra;
    long long eligible, known;
    assert(sscanf(line, "%127s %31s %lld %lld %63s %c", name, state, &eligible,
                  &known, sum, &extra) == 5);
    json_object *m = f_field(data, name);
    assert(m && !strcmp(f_string(m, "state"), state));
    assert(json_object_get_int64(f_field(m, "eligible")) == eligible &&
           json_object_get_int64(f_field(m, "known")) == known);
    if (!strcmp(sum, "-"))
      assert(!f_field(m, "sum"));
    else
      assert(json_object_get_int64(f_field(m, "sum")) ==
             strtoll(sum, NULL, 10));
    rows++;
    line = strtok_r(NULL, "\n", &save);
  }
  assert(rows == 3);
  f_capture_free(&c);
  return data;
}
static void state_is(const char *metric, const char *state) {
  json_object *d = read_metrics();
  assert(!strcmp(f_string(f_field(d, metric), "state"), state));
  json_object_put(d);
}
static void clear(void) { assert(!f_remove_tree(root)); }
int main(void) {
  fleet = getenv("HYDRA_FLEET_BIN");
  if (!fleet)
    fleet = "build/hydra-fleet";
  json_object *d, *v;
  char p[F_PATH], q[F_PATH];
  setup();
  d = read_metrics();
  nt_equal(f_field(d, "unknown_receiver_outcomes"),
           "{\"state\":\"known\",\"eligible\":1,\"known\":1,\"sum\":0}");
  assert(json_object_get_int(
             f_field(f_field(d, "recorded_operator_actions"), "sum")) == 0);
  assert(json_object_get_int(
             f_field(f_field(d, "transport_stdio_bytes"), "sum")) == 30);
  json_object_put(d);
  events(true);
  d = read_metrics();
  assert(json_object_get_int(
             f_field(f_field(d, "recorded_operator_actions"), "sum")) == 3);
  json_object_put(d);
  clear();
  setup();
  attempt(2, "outcome_unknown");
  write_at(step, "attempts", "2\n");
  d = read_metrics();
  nt_equal(f_field(d, "unknown_receiver_outcomes"),
           "{\"state\":\"known\",\"eligible\":2,\"known\":2,\"sum\":1}");
  json_object_put(d);
  write_at(step, "attempts", "3\n");
  d = read_metrics();
  nt_equal(f_field(d, "unknown_receiver_outcomes"),
           "{\"state\":\"partial\",\"eligible\":3,\"known\":2,\"sum\":null}");
  assert(!strcmp(f_string(f_field(d, "transport_stdio_bytes"), "state"),
                 "partial"));
  json_object_put(d);
  clear();
  setup();
  nt_path(p, remote, "observation.json");
  v = f_read_json(p, 100000);
  f_string_add(
      v, "task_id",
      "task_cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  nt_json_write(p, v);
  json_object_put(v);
  state_is("unknown_receiver_outcomes", "unknown");
  nt_write(p, "{}");
  d = read_metrics();
  assert(!f_field(f_field(d, "unknown_receiver_outcomes"), "sum"));
  json_object_put(d);
  clear();
  setup();
  nt_path(p, root, "events.jsonl");
  char *original = f_read(p, 100000), *changed;
  assert(original);
  const char *bad[] = {"", "{\"type\":\"run.created\"}\n"};
  for (size_t i = 0; i < 2; i++) {
    nt_write(p, bad[i]);
    state_is("recorded_operator_actions", "unknown");
  }
  changed = strdup(original);
  assert(changed);
  changed[strlen(changed) - 1] = 0;
  nt_write(p, changed);
  free(changed);
  state_is("recorded_operator_actions", "unknown");
  changed = nt_replace(original, "\"sequence\":2", "\"sequence\":3");
  nt_write(p, changed);
  free(changed);
  state_is("recorded_operator_actions", "unknown");
  changed = nt_replace(original, "run_fixture", "run_foreign");
  nt_write(p, changed);
  free(changed);
  state_is("recorded_operator_actions", "unknown");
  nt_write(p, strchr(original, '\n') + 1);
  state_is("recorded_operator_actions", "unknown");
  free(original);
  clear();
  setup();
  nt_path(p, remote, "transport-metrics.json");
  v = f_read_json(p, 100000);
  const char *fields[] = {"request_bytes",  "request_bytes", "calls",
                          "response_bytes", "complete",      "complete"};
  const char *values[] = {"-1", "18446744073709551616", "0", "\"20\"", "false",
                          "1"};
  for (size_t i = 0; i < 6; i++) {
    json_object *m = nt_clone(
        v); /* Keep overflow lexical bytes, not a saturated JSON-C integer. */
    if (i == 1) {
      char *s = nt_replace(
          json_object_to_json_string_ext(v, JSON_C_TO_STRING_PLAIN),
          "\"request_bytes\":10", "\"request_bytes\":18446744073709551616");
      nt_write(p, s);
      free(s);
    } else {
      json_object_object_add(m, fields[i], f_parse_value(values[i]));
      nt_json_write(p, m);
    }
    json_object_put(m);
    state_is("transport_stdio_bytes", "unknown");
  }
  changed =
      nt_replace(json_object_to_json_string_ext(v, JSON_C_TO_STRING_PLAIN),
                 "\"calls\":2", "\"calls\":0,\"calls\":2");
  nt_write(p, changed);
  free(changed);
  state_is("transport_stdio_bytes", "unknown");
  nt_json_write(p, v);
  json_object_put(v);
  write_at(remote, "transport-incomplete", "incomplete\n");
  d = read_metrics();
  assert(!f_field(f_field(d, "transport_stdio_bytes"), "sum"));
  json_object_put(d);
  clear();
  setup();
  nt_path(p, remote, "observation.json");
  nt_path(q, remote, "receipt.json");
  assert(!unlink(p) && !symlink(q, p));
  state_is("unknown_receiver_outcomes", "unknown");
  write_at(step, "attempts", "4097\n");
  state_is("transport_stdio_bytes", "unknown");
  write_at(root, "retention.json", "{}");
  d = read_metrics();
  json_object_object_foreach(d, key, item) {
    (void)key;
    assert(!strcmp(f_string(item, "state"), "unknown") &&
           !f_field(item, "sum"));
  }
  json_object_put(d);
  clear();
  setup();
  write_at(step, "attempts", "0\n");
  d = read_metrics();
  nt_equal(
      f_field(d, "transport_stdio_bytes"),
      "{\"state\":\"unavailable\",\"eligible\":0,\"known\":0,\"sum\":null}");
  json_object_put(d);
  clear();
  puts("Workflow task metrics: all seven original case groups passed");
  return 0;
}
