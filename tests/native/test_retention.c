#include "support.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <utime.h>
static char base[F_PATH], home[F_PATH], policy[F_PATH], hydra[F_PATH];
static const char *fleet;
static void file(char *out, const char *dir, const char *name) {
  nt_path(out, dir, name);
}
static void write_at(const char *dir, const char *name, const char *text) {
  char p[F_PATH];
  file(p, dir, name);
  nt_write(p, text);
}
static void json_at(const char *dir, const char *name, json_object *v) {
  char p[F_PATH];
  file(p, dir, name);
  nt_json_write(p, v);
}
static json_object *read_at(const char *dir, const char *name) {
  char p[F_PATH];
  file(p, dir, name);
  json_object *v = f_read_json(p, 2000000);
  assert(v);
  return v;
}
static bool exists(const char *dir, const char *name) {
  char p[F_PATH];
  struct stat s;
  file(p, dir, name);
  return !stat(p, &s);
}
static char *bytes(const char *dir, const char *name) {
  char p[F_PATH];
  file(p, dir, name);
  char *v = f_read(p, 2000000);
  assert(v);
  return v;
}
static const char *basename_of(const char *p) {
  const char *v = strrchr(p, '/');
  return v ? v + 1 : p;
}
static void old(const char *dir) {
  DIR *d = opendir(dir);
  struct dirent *e;
  assert(d);
  struct utimbuf times = {time(NULL) - 3 * 86400, time(NULL) - 3 * 86400};
  while ((e = readdir(d))) {
    char p[F_PATH];
    struct stat st;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    file(p, dir, e->d_name);
    assert(!lstat(p, &st));
    if (S_ISLNK(st.st_mode))
      continue;
    if (S_ISDIR(st.st_mode))
      old(p);
    assert(!utime(p, &times));
  }
  assert(!closedir(d));
}
static void setup(void) {
  char tmp[] = "/tmp/hydra-native-retention-XXXXXX";
  assert(mkdtemp(tmp));
  assert(!f_copy(base, sizeof base, tmp));
  file(home, base, "home");
  assert(!f_mkdirs(home));
  file(policy, base, "policy.json");
  nt_write(policy, "{\"schema_version\":1,\"audit_days\":1,\"max_bytes\":"
                   "1048576,\"max_evidence_records\":100}");
  assert(!setenv("HYDRA_HOME", home, 1));
}
static void cleanup(void) { assert(!f_remove_tree(base)); }
static void task(char out[F_PATH], const char *key, const char *state) {
  char digest[65], name[80], tasks[F_PATH], p[F_PATH];
  json_object *v = json_object_new_object();
  f_string_add(v, "submission_key", key);
  assert(!plan_digest(v, digest));
  json_object_put(v);
  assert(snprintf(name, sizeof name, "task_%s", digest) > 0);
  file(tasks, home, "fleet/tasks");
  file(out, tasks, name);
  assert(!f_mkdirs(out));
  v = f_parse("{\"schema_version\":1,\"project\":\"/"
              "fixture\",\"project_id\":\"project_fixture\",\"spec_sha256\":"
              "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aa\",\"accepted_at\":1}");
  f_string_add(v, "task_id", name);
  f_string_add(v, "submission_key", key);
  json_at(out, "acceptance.json", v);
  json_object_put(v);
  v = f_parse("{\"schema_version\":1,\"launch_intent\":\"started\",\"run_id\":"
              "\"run_inner\",\"exit_status\":0,\"finished_at\":1,\"result_"
              "state\":\"ready\"}");
  f_string_add(v, "state", state);
  json_at(out, "state.json", v);
  json_object_put(v);
  write_at(out, "package.json", "{\"fixture\":true}");
  write_at(out, "result.json", "{\"fixture\":\"sealed evidence\"}");
  write_at(out, "stdout", "recorded output\n");
  file(p, out, "workspace");
  assert(!f_mkdirs(p));
  write_at(p, "dirty-user-file", "keep workspace");
  old(out);
}
static void run_record(char out[F_PATH], const char *id, const char *state,
                       const char *task_id, const char *key) {
  char runs[F_PATH], attempt[F_PATH], p[F_PATH];
  file(runs, home, "state/v2/projects/project_fixture/workflows/runs");
  file(out, runs, id);
  assert(!f_mkdirs(out));
  write_at(out, "state", state);
  write_at(out, "run-id", id);
  write_at(out, "project-id", "project_fixture\n");
  write_at(out, "graph.tsv", "step\twork\texec\n");
  write_at(out, "completed-at", "1\n");
  write_at(out, "compiled.json", "{\"fixture\":\"contract\"}");
  file(attempt, out, "steps/work/attempt-1/remote");
  assert(!f_mkdirs(attempt));
  json_object *v = json_object_new_object();
  if (task_id)
    f_string_add(v, "task_id", task_id);
  else if (key)
    f_string_add(v, "submission_key", key);
  json_at(attempt, "receipt.json", v);
  json_object_put(v);
  file(p, out, "steps/work");
  write_at(p, "state", "succeeded\n");
  write_at(p, "attempts", "1\n");
  old(out);
}
static json_object *command(const char *op, const char *a, const char *b,
                            bool ok) {
  char *args[] = {hydra,     "fleet",   "retention", (char *)op,
                  (char *)a, (char *)b, NULL};
  struct f_capture c = nt_run(args);
  if ((c.status == 0) != ok)
    fprintf(stderr, "%s\n%s", c.out, c.err);
  assert((c.status == 0) == ok);
  json_object *v = f_parse(c.out);
  assert(v);
  f_capture_free(&c);
  return v;
}
static json_object *action(const char *op, bool ok) {
  return command(op, "--policy", policy, ok);
}
static void apply(void) { json_object_put(action("apply", true)); }
static json_object *serve(json_object *request) {
  char *args[] = {(char *)fleet, "fleet", "serve", NULL};
  struct f_capture c = {0};
  const char *input =
      json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
  assert(!f_run(args, input, strlen(input), 10, &c));
  json_object *v = f_parse(c.out);
  assert(v);
  f_capture_free(&c);
  return v;
}
static json_object *operation(const char *t, const char *op) {
  json_object *r = f_parse("{\"protocol\":1,\"action\":\"task\"}");
  f_string_add(r, "operation", op);
  f_string_add(r, "task_id", basename_of(t));
  json_object *v = serve(r);
  json_object_put(r);
  return v;
}
static void error_is(json_object *v, const char *code) {
  if (strcmp(f_string(f_field(v, "error"), "code"), code))
    fprintf(stderr, "%s\n", json_object_to_json_string(v));
  assert(!strcmp(f_string(f_field(v, "error"), "code"), code));
  json_object_put(v);
}
static void preserved(json_object *v, const char *reason) {
  json_object *a = f_field(f_field(v, "data"), "records");
  assert(json_object_array_length(a) > 0);
  for (size_t i = 0; i < json_object_array_length(a); i++) {
    json_object *r = json_object_array_get_idx(a, i);
    assert(!strcmp(f_string(r, "action"), "preserve"));
    if (reason)
      assert(!strcmp(f_string(r, "reason"), reason));
  }
  json_object_put(v);
}
static void preview_expiry(void) {
  char t[F_PATH], p[F_PATH];
  task(t, "a", "succeeded");
  char *before = bytes(t, "acceptance.json");
  json_object *v = action("preview", true);
  nt_equal(f_field(f_field(v, "data"), "capacity_satisfied"), "true");
  nt_equal(f_field(f_field(v, "data"), "applied"), "false");
  json_object_put(v);
  assert(exists(t, "result.json"));
  v = action("apply", true);
  nt_equal(f_field(f_field(v, "data"), "applied"), "true");
  json_object_put(v);
  char *after = bytes(t, "acceptance.json");
  assert(!strcmp(before, after));
  free(before);
  free(after);
  assert(!exists(t, "result.json"));
  file(p, t, "workspace");
  after = bytes(p, "dirty-user-file");
  assert(!strcmp(after, "keep workspace"));
  free(after);
  v = operation(t, "status");
  nt_equal(f_field(v, "ok"), "true");
  assert(
      !strcmp(f_string(f_field(f_field(v, "data"), "runtime"), "result_state"),
              "expired"));
  json_object_put(v);
  const char *ops[] = {"result", "logs", "observe"};
  for (size_t i = 0; i < 3; i++)
    error_is(operation(t, ops[i]), "evidence_expired");
  v = action("apply", true);
  nt_equal(f_field(f_field(v, "data"), "applied"), "true");
  json_object_put(v);
}
static void active_unknown(void) {
  char t[F_PATH], r[F_PATH];
  task(t, "a", "outcome_unknown");
  run_record(r, "run_outer", "waiting-remote", basename_of(t), NULL);
  preserved(action("apply", true), NULL);
  assert(exists(t, "result.json"));
}
static void overview_bounds(void) {
  char tasks[F_PATH], t[F_PATH], id[80];
  file(tasks, home, "fleet/tasks");
  for (int i = 0; i < 513; i++) {
    assert(snprintf(id, sizeof id, "task_%064x", i) > 0);
    file(t, tasks, id);
    assert(!f_mkdirs(t));
    write_at(t, "retention.json",
             "{\"schema_version\":1,\"state\":\"expired\"}");
    if (i < 511)
      continue;
    json_object *r = f_parse("{\"protocol\":1,\"action\":\"overview\"}"),
                *v = serve(r);
    json_object_put(r);
    if (i == 511) {
      nt_equal(f_field(v, "ok"), "true");
      nt_equal(f_field(f_field(v, "data"), "expired_task_count"), "512");
      nt_equal(f_field(f_field(v, "data"), "tasks"), "[]");
      json_object_put(v);
    } else {
      nt_equal(f_field(v, "ok"), "false");
      error_is(v, "limit");
    }
  }
}
static void lost_ack(void) {
  char t[F_PATH], r[F_PATH];
  task(t, "lost-ack", "succeeded");
  run_record(r, "run_outer", "waiting-remote", NULL, "lost-ack");
  json_object *v = action("apply", true),
              *a = f_field(f_field(v, "data"), "records");
  bool found = false;
  for (size_t i = 0; i < json_object_array_length(a); i++) {
    json_object *item = json_object_array_get_idx(a, i);
    if (!strcmp(f_string(item, "id"), basename_of(t))) {
      assert(!strcmp(f_string(item, "reason"), "referenced_evidence"));
      found = true;
    }
  }
  assert(found && exists(t, "result.json") && exists(r, "compiled.json"));
  json_object_put(v);
}
static void pin_transitive(void) {
  char t[F_PATH], r[F_PATH];
  task(t, "a", "succeeded");
  run_record(r, "run_outer", "succeeded", basename_of(t), NULL);
  json_object_put(command("pin", "run", basename_of(r), true));
  apply();
  assert(exists(t, "result.json"));
  json_object_put(command("unpin", "run", basename_of(r), true));
  apply();
  assert(!exists(t, "result.json") && !exists(r, "compiled.json"));
  char *args[] = {(char *)fleet, "workflow-plan", "result", r, NULL};
  struct f_capture c = nt_run(args);
  json_object *v = f_parse(c.out);
  assert(v);
  error_is(v, "evidence_expired");
  f_capture_free(&c);
  error_is(command("pin", "run", basename_of(r), false), "evidence_expired");
}
static void quota_refusal(void) {
  char t[F_PATH], u[F_PATH];
  task(t, "old", "succeeded");
  task(u, "unknown", "outcome_unknown");
  json_object *p = f_read_json(policy, 100000);
  json_object_object_add(p, "max_evidence_records", json_object_new_int(0));
  nt_json_write(policy, p);
  json_object_put(p);
  error_is(action("apply", false), "protected_capacity_exceeded");
  assert(exists(t, "result.json"));
}
static void locks_and_audit(void) {
  char t[F_PATH], r[F_PATH], p[F_PATH];
  task(t, "a", "succeeded");
  run_record(r, "run_outer", "succeeded", NULL, NULL);
  file(p, t, "owner.lock");
  int owner = open(p, O_CREAT | O_WRONLY, 0600);
  assert(owner >= 0 && !flock(owner, LOCK_EX | LOCK_NB));
  file(p, r, "coordinator.lock");
  int coordinator = open(p, O_CREAT | O_WRONLY, 0600);
  struct flock lock = {0};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  assert(coordinator >= 0 && !fcntl(coordinator, F_SETLK, &lock));
  apply();
  assert(exists(t, "result.json") && exists(r, "compiled.json"));
  assert(!close(owner) && !close(coordinator));
  file(p, t, "result.json");
  assert(!utime(p, NULL));
  apply();
  assert(exists(t, "result.json"));
}
static void links_and_corrupt(void) {
  char t[F_PATH], r[F_PATH], target[F_PATH], p[F_PATH];
  task(t, "a", "succeeded");
  file(target, base, "unrelated");
  nt_write(target, "preserved");
  file(p, t, "stdout");
  assert(!unlink(p) && !symlink(target, p));
  error_is(action("apply", false), "incomplete_inventory");
  char *v = f_read(target, 100);
  assert(v && !strcmp(v, "preserved"));
  free(v);
  assert(!unlink(p));
  run_record(r, "run_outer", "succeeded", basename_of(t), NULL);
  write_at(r, "steps/work/attempt-1/remote/receipt.json", "bad");
  error_is(action("apply", false), "incomplete_inventory");
  assert(exists(t, "result.json"));
}
static void interrupted_expiry(void) {
  char t[F_PATH];
  task(t, "a", "succeeded");
  apply();
  json_object *v = read_at(t, "retention.json");
  f_string_add(v, "state", "expiring");
  json_at(t, "retention.json", v);
  json_object_put(v);
  error_is(operation(t, "result"), "evidence_expired");
  write_at(t, "stdout", "recorded output\n");
  apply();
  assert(!exists(t, "stdout"));
  write_at(t, "stdout", "new unexpected bytes");
  error_is(action("apply", false), "expiry_incomplete");
  char *s = bytes(t, "stdout");
  assert(!strcmp(s, "new unexpected bytes"));
  free(s);
}
static void invalid_policy(void) {
  char t[F_PATH];
  task(t, "a", "succeeded");
  char *s = f_read(policy, 10000);
  assert(s);
  size_t n = strlen(s);
  char bad[1024];
  assert(n > 0 && n + 18 < sizeof bad);
  memcpy(bad, s, n - 1);
  assert(snprintf(bad + n - 1, sizeof bad - n + 1, ",\"audit_days\":0}") > 0);
  nt_write(policy, bad);
  error_is(action("apply", false), "invalid_input");
  nt_write(policy, s);
  free(s);
  apply();
  write_at(t, "retention-manifest.json", "{\"files\":[]}");
  error_is(action("apply", false), "expiry_incomplete");
}
static void audit_not_shortened(void) {
  char t[F_PATH];
  task(t, "a", "succeeded");
  json_object *p = f_read_json(policy, 10000);
  json_object_object_add(p, "audit_days", json_object_new_int(30));
  nt_json_write(policy, p);
  apply();
  char *before = bytes(t, "retention-audit.json");
  assert(exists(t, "result.json"));
  json_object_object_add(p, "audit_days", json_object_new_int(1));
  nt_json_write(policy, p);
  json_object_put(p);
  json_object *v = action("apply", true);
  assert(!strcmp(f_string(json_object_array_get_idx(
                              f_field(f_field(v, "data"), "records"), 0),
                          "reason"),
                 "declared_audit_window"));
  json_object_put(v);
  char *after = bytes(t, "retention-audit.json");
  assert(!strcmp(before, after) && exists(t, "result.json"));
  free(before);
  free(after);
  write_at(t, "retention-audit.json", "broken");
  apply();
  assert(exists(t, "result.json"));
}
static void incomplete_acceptance(void) {
  const char *fields[] = {"project", "project_id", "accepted_at"};
  for (size_t i = 0; i < 3; i++) {
    char t[F_PATH], key[32];
    assert(snprintf(key, sizeof key, "missing-%zu", i) > 0);
    task(t, key, "succeeded");
    json_object *v = read_at(t, "acceptance.json");
    json_object_object_del(v, fields[i]);
    json_at(t, "acceptance.json", v);
    json_object_put(v);
    old(t);
  }
  preserved(action("apply", true), NULL);
}
static void metadata_capacity(void) {
  char t[F_PATH];
  task(t, "a", "outcome_unknown");
  json_object *v = action("preview", true);
  int original =
      json_object_get_int(f_field(f_field(v, "data"), "bytes_before"));
  json_object_put(v);
  assert(original >= 0 && original < 4096);
  size_t n = 4096 - (size_t)original + strlen("recorded output\n");
  char *s = malloc(n + 1);
  assert(s);
  memset(s, 'x', n);
  s[n] = 0;
  write_at(t, "stdout", s);
  free(s);
  v = f_read_json(policy, 10000);
  json_object_object_add(v, "max_bytes", json_object_new_int(4096));
  nt_json_write(policy, v);
  json_object_put(v);
  error_is(action("apply", false), "protected_capacity_exceeded");
  assert(!exists(t, "retention-audit.json"));
}
static void unresolved_steps(void) {
  const char *states[] = {"queued", "ready", "retrying", "unrecognized"};
  for (size_t i = 0; i < 4; i++) {
    char r[F_PATH], id[32];
    assert(snprintf(id, sizeof id, "run_state_%zu", i) > 0);
    run_record(r, id, "succeeded", NULL, NULL);
    write_at(r, "steps/work/state", states[i]);
    old(r);
  }
  preserved(action("apply", true), "unresolved_step");
}
int main(void) {
  char root[F_PATH], binary[F_PATH];
  assert(getcwd(root, sizeof root));
  file(hydra, root, "bin/hydra");
  fleet = getenv("HYDRA_FLEET_BIN");
  if (!fleet) {
    file(binary, root, "build/hydra-fleet");
    fleet = binary;
    assert(!setenv("HYDRA_FLEET_BIN", fleet, 1));
  }
  void (*cases[])(void) = {
      preview_expiry,    active_unknown,      overview_bounds,
      lost_ack,          pin_transitive,      quota_refusal,
      locks_and_audit,   links_and_corrupt,   interrupted_expiry,
      invalid_policy,    audit_not_shortened, incomplete_acceptance,
      metadata_capacity, unresolved_steps};
  for (size_t i = 0; i < 14; i++) {
    setup();
    cases[i]();
    cleanup();
    printf("Retention case %zu passed\n", i + 1);
  }
  return 0;
}
