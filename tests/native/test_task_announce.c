#include "support.h"
static const char *binary;
static char file[F_PATH];
static json_object *page(void) {
  return f_parse(
      "{\"schema_version\":1,\"ok\":true,\"command\":\"fleet-observation\","
      "\"data\":{\"snapshot_schema_version\":1,\"receiver_observed_at\":123,"
      "\"task\":{\"task_id\":\"task_"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
      "\"run_id\":\"run_a\",\"step_id\":null,\"attempt_id\":null,\"assigned_"
      "host\":null,\"workspace\":null,\"agent_profile\":\"codex\",\"execution_"
      "state\":\"running\",\"execution_owner\":{\"kind\":\"receiver\","
      "\"state\":\"running\",\"recorded_state\":null,\"failure\":null},"
      "\"waiting\":{\"reason\":\"none\",\"detail\":\"\",\"next_action\":\"\"},"
      "\"observation_timestamps\":{\"accepted_at\":1,\"started_at\":2,"
      "\"resumed_at\":null,\"finished_at\":null,\"receiver_observed_at\":123},"
      "\"effective_configuration\":{},\"contract\":{\"availability\":"
      "\"available\",\"reason\":\"\",\"missing_evidence\":[]},\"steps\":[],"
      "\"pending_requests\":[]},\"event_observation\":{\"schema_version\":1,"
      "\"events\":[{\"schema_version\":1,\"sequence\":1,\"occurred_at\":\"2026-"
      "01-01T00:00:00Z\",\"run_id\":\"run_a\",\"step_id\":null,\"type\":\"run."
      "created\",\"detail\":\"hello\"}],\"available\":true,\"stream_id\":\"dev:"
      "ino\",\"scan_truncated\":false,\"next_byte_offset\":100,\"oldest_"
      "cursor\":0,\"next_cursor\":1,\"head_cursor\":1,\"retention_gap\":false,"
      "\"stream_reset\":false,\"duplicate_policy\":\"sequence-cursor\"}}}");
}
static json_object *obs(json_object *p) {
  return f_field(f_field(p, "data"), "event_observation");
}
static json_object *event(json_object *p) {
  return json_object_array_get_idx(f_field(obs(p), "events"), 0);
}
static struct f_capture call(const char *a, const char *b, const char *c,
                             const char *d) {
  char *argv[] = {(char *)binary, "fleet",   "task",    "announce",
                  "--input",      file,      (char *)a, (char *)b,
                  (char *)c,      (char *)d, NULL};
  return nt_run(argv);
}
static char *accept(json_object *p) {
  struct f_capture c;
  json_object *v;
  char *out;
  nt_json_write(file, p);
  c = call(NULL, NULL, NULL, NULL);
  v = nt_output(&c, 0);
  out = strdup(f_string(f_field(v, "data"), "announcement"));
  assert(out);
  json_object_put(v);
  f_capture_free(&c);
  return out;
}
static void reject(json_object *p) {
  struct f_capture c;
  nt_json_write(file, p);
  c = call(NULL, NULL, NULL, NULL);
  assert(c.status && !strstr(c.out, "announcement"));
  f_capture_free(&c);
}
int main(int argc, char **argv) {
  char folder[] = "/tmp/hydra-announcer-XXXXXX";
  json_object *p;
  char *text;
  struct f_capture c, d;
  binary = argc > 1 ? argv[1] : "build/hydra-fleet";
  assert(mkdtemp(folder));
  nt_path(file, folder, "page.json");
  p = page();
  text = accept(p);
  assert(strstr(text, "task id=task_") &&
         strstr(text, "receiver_observed_at=123"));
  assert(strstr(text, "event time=2026-01-01T00:00:00Z run=run_a step=- "
                      "sequence=1 type=run.created detail=hello"));
  assert(strstr(text, "resume cursor=1 byte_offset=100 stream_id=dev:ino"));
  c = call("--format", "text", NULL, NULL);
  assert(!c.status && !strcmp(c.out, text) && !*c.err);
  f_capture_free(&c);
  free(text);
  c = call(NULL, NULL, NULL, NULL);
  d = call("--format", "json", NULL, NULL);
  assert(!d.status && !strcmp(c.out, d.out));
  f_capture_free(&c);
  f_capture_free(&d);
  const char *options[][4] = {{"--format", "html", NULL, NULL},
                              {"--format", NULL, NULL, NULL},
                              {"--format", "text", "--format", "json"},
                              {"--input", file, NULL, NULL},
                              {"--unknown", "text", NULL, NULL}};
  for (size_t i = 0; i < 5; i++) {
    c = call(options[i][0], options[i][1], options[i][2], options[i][3]);
    assert(c.status && !strstr(c.out, "announcement"));
    f_capture_free(&c);
  }
  json_object_put(p);
  const char *fields[] = {"retention_gap", "stream_reset", "scan_truncated",
                          "available"},
             *expected[] = {"retention_gap=true", "stream_reset=true",
                            "truncated=true", "history unavailable"};
  for (size_t i = 0; i < 4; i++) {
    p = page();
    json_object_object_add(obs(p), fields[i], json_object_new_boolean(i != 3));
    if (i == 3) {
      json_object_object_add(obs(p), "events", json_object_new_array());
      json_object_object_add(obs(p), "next_cursor", json_object_new_int(0));
      json_object_object_add(obs(p), "head_cursor", json_object_new_int(0));
    }
    text = accept(p);
    assert(strstr(text, expected[i]));
    free(text);
    json_object_put(p);
  }
  p = page();
  f_string_add(event(p), "step_id", "create");
  f_string_add(f_field(f_field(p, "data"), "task"), "step_id", "work");
  text = accept(p);
  assert(strstr(text, "step=create"));
  free(text);
  json_object_put(p);
  p = page();
  f_string_add(event(p), "run_id", "run_other");
  reject(p);
  json_object_put(p);
  p = page();
  json_object_object_add(event(p), "sequence", json_object_new_int(2));
  json_object *e = nt_clone(event(p));
  json_object_object_add(e, "sequence", json_object_new_int(4));
  json_object_array_add(f_field(obs(p), "events"), e);
  json_object_object_add(obs(p), "next_cursor", json_object_new_int(2));
  json_object_object_add(obs(p), "head_cursor", json_object_new_int(2));
  reject(p);
  json_object_put(p);
  p = page();
  json_object_object_add(obs(p), "next_cursor", json_object_new_int(2));
  reject(p);
  json_object_put(p);
  p = page();
  json_object_object_add(p, "schema_version", json_object_new_int(2));
  reject(p);
  json_object_put(p);
  for (int i = 0; i < 2; i++) {
    p = page();
    json_object_object_del(p, "command");
    if (i)
      json_object_object_add(p, "command", json_object_new_int(7));
    reject(p);
    json_object_put(p);
  }
  p = page();
  json_object_object_add(obs(p), "oldest_cursor", json_object_new_int(2));
  reject(p);
  json_object_put(p);
  p = page();
  f_string_add(event(p), "detail", "x\n\033[31m");
  text = accept(p);
  assert(strstr(text, "x??[31m") && !strchr(text, 27));
  c = call("--format", "text", NULL, NULL);
  assert(!c.status && !strcmp(c.out, text));
  f_capture_free(&c);
  free(text);
  json_object_put(p);
  p = page();
  f_string_add(event(p), "detail", "caf\303\251");
  text = accept(p);
  assert(strstr(text, "caf?"));
  for (char *q = text; *q; q++)
    assert((unsigned char)*q < 128);
  free(text);
  json_object_put(p);
  p = page();
  e = nt_clone(event(p));
  json_object_object_add(obs(p), "events", json_object_new_array());
  for (int i = 1; i < 130; i++) {
    json_object *v = nt_clone(e);
    json_object_object_add(v, "sequence", json_object_new_int(i));
    json_object_array_add(f_field(obs(p), "events"), v);
  }
  json_object_put(e);
  json_object_object_add(obs(p), "next_cursor", json_object_new_int(129));
  json_object_object_add(obs(p), "head_cursor", json_object_new_int(129));
  reject(p);
  json_object_put(p);
  p = f_parse("{}");
  reject(p);
  json_object_put(p);
  c = call("--format", "text", NULL, NULL);
  assert(c.status && strncmp(c.out, "task id=", 8));
  f_capture_free(&c);
  text = malloc(270002);
  assert(text);
  text[0] = '{';
  memset(text + 1, 'x', 270000);
  text[270001] = 0;
  nt_write(file, text);
  free(text);
  c = call(NULL, NULL, NULL, NULL);
  assert(c.status);
  f_capture_free(&c);
  nt_finish(folder, "Task observation announcer validation, gaps, resets and "
                    "sanitization passed");
  return 0;
}
