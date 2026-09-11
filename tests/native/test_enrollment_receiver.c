#include "support.h"
#include <errno.h>
#include <signal.h>
static char root[F_PATH], repo[F_PATH], home[F_PATH], counter[F_PATH],
    started[F_PATH], child_pid[F_PATH], fleet[F_PATH], fixture[F_PATH],
    hydra[F_PATH];
static json_object *request;
static void setup(void) {
  char tmp[] = "/tmp/hydra-native-enrollment-receiver-XXXXXX";
  assert(mkdtemp(tmp));
  assert(!f_copy(root, sizeof root, tmp));
  nt_path(repo, root, "project");
  char *args[] = {"git", "init", "-q", repo, NULL};
  struct f_capture c = nt_run(args);
  assert(!c.status);
  f_capture_free(&c);
  nt_path(home, root, "home");
  assert(!f_mkdirs(home));
  nt_path(counter, root, "mutations");
  nt_path(started, root, "started");
  nt_path(child_pid, root, "child-pid");
  assert(!setenv("HYDRA_HOME", home, 1) &&
         !setenv("HYDRA_BIN_CMD", fixture, 1) &&
         !setenv("ENROLL_REAL", hydra, 1) &&
         !setenv("ENROLL_COUNTER", counter, 1) &&
         !setenv("ENROLL_CHILD_PID", child_pid, 1) &&
         !setenv("ENROLL_STARTED", started, 1) && !unsetenv("ENROLL_HOLD"));
  request =
      f_parse("{\"protocol\":1,\"action\":\"init\",\"args\":[\"--no-agent\",\"-"
              "-trust\"],\"enrollment_operation_id\":"
              "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aa:host\",\"expected_peer_fingerprint\":\"SHA256:reviewed\"}");
  f_string_add(request, "project", repo);
}
static void cleanup(void) {
  json_object_put(request);
  assert(!f_remove_tree(root));
}
static struct nt_child start(json_object *r) {
  char *args[] = {fleet, "fleet", "serve", NULL};
  return nt_start(args, json_object_to_json_string_ext(r ? r : request,
                                                       JSON_C_TO_STRING_PLAIN));
}
static json_object *call(json_object *r) {
  struct nt_child child = start(r);
  struct f_capture c = nt_wait(&child, 30);
  if (!*c.out)
    fprintf(stderr, "%s\n", c.err);
  assert(!c.timeout && *c.out);
  json_object *v = f_parse(c.out);
  assert(v);
  f_capture_free(&c);
  return v;
}
static void error_is(json_object *v, const char *code) {
  assert(!strcmp(f_string(f_field(v, "error"), "code"), code));
  json_object_put(v);
}
static void count(bool one) {
  char *v = f_read(counter, 10000);
  if (one)
    assert(v && !strcmp(v, "init\n"));
  else
    assert(!v || !*v);
  free(v);
}
static void success(json_object *v) {
  nt_equal(f_field(v, "ok"), "true");
  json_object_put(v);
}
static void duplicate(void) {
  json_object *a = call(NULL), *b = call(NULL);
  nt_equal(f_field(a, "ok"), "true");
  assert(json_object_equal(a, b));
  json_object_put(a);
  json_object_put(b);
  count(true);
}
static void changed(void) {
  success(call(NULL));
  char other[F_PATH];
  nt_path(other, root, "other");
  char *args[] = {"git", "init", "-q", other, NULL};
  struct f_capture c = nt_run(args);
  assert(!c.status);
  f_capture_free(&c);
  for (size_t i = 0; i < 3; i++) {
    json_object *r = nt_clone(request);
    if (i == 0)
      f_string_add(r, "project", other);
    else if (i == 1)
      json_object_object_add(r, "args", f_parse_value("[\"--no-agent\"]"));
    else
      f_string_add(r, "expected_peer_fingerprint", "SHA256:changed");
    error_is(call(r), "intent_changed");
    json_object_put(r);
  }
  count(true);
}
static void concurrent(void) {
  char release[F_PATH];
  nt_path(release, root, "release");
  assert(!setenv("ENROLL_HOLD", release, 1));
  struct nt_child child = start(NULL);
  nt_await_file(started, 10);
  error_is(call(NULL), "outcome_unknown");
  count(false);
  nt_write(release, "");
  struct f_capture c = nt_wait(&child, 30);
  json_object *a = f_parse(c.out);
  assert(a);
  nt_equal(f_field(a, "ok"), "true");
  json_object *b = call(NULL);
  assert(json_object_equal(a, b));
  json_object_put(a);
  json_object_put(b);
  f_capture_free(&c);
  count(true);
}
static void owner_loss(void) {
  char release[F_PATH];
  nt_path(release, root, "never-release");
  assert(!setenv("ENROLL_HOLD", release, 1));
  struct nt_child child = start(NULL);
  nt_await_file(started, 10);
  assert(!kill(-child.pid, SIGKILL));
  char *pid = f_read(child_pid, 100);
  assert(pid);
  int rc = kill(-(pid_t)strtol(pid, NULL, 10), SIGKILL);
  assert(!rc || errno == ESRCH);
  free(pid);
  struct f_capture c = nt_wait(&child, 10);
  f_capture_free(&c);
  assert(!unsetenv("ENROLL_HOLD"));
  error_is(call(NULL), "outcome_unknown");
  count(false);
}
static void invalid(void) {
  const char *values[] = {"null", "7", "\"../escape\"",
                          ("\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                          "aaaaaaaaaaaaaa:../escape\"")};
  for (size_t i = 0; i < 4; i++) {
    json_object *r = nt_clone(request);
    json_object_object_add(r, "enrollment_operation_id",
                           f_parse_value(values[i]));
    error_is(call(r), "invalid_input");
    json_object_put(r);
  }
  char dir[F_PATH], p[F_PATH];
  nt_path(dir, home, "fleet");
  assert(!f_mkdirs(dir));
  nt_path(p, dir, "enrollment-ops");
  nt_write(p, "not a directory");
  error_is(call(NULL), "state_unavailable");
  count(false);
}
static void corrupt(void) {
  char dir[F_PATH], p[F_PATH];
  nt_path(dir, home, "fleet/enrollment-ops");
  assert(!f_mkdirs(dir));
  nt_path(
      p, dir,
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_host");
  const char *values[] = {"succeeded", "pending", "{invalid"};
  for (size_t i = 0; i < 3; i++) {
    nt_write(p, values[i]);
    error_is(call(NULL), "outcome_unknown");
  }
  count(false);
}
int main(int argc, char **argv) {
  char origin[F_PATH];
  assert(getcwd(origin, sizeof origin));
  nt_path(hydra, origin, "bin/hydra");
  const char *f = getenv("HYDRA_FLEET_BIN");
  if (f)
    assert(!f_copy(fleet, sizeof fleet, f));
  else
    nt_path(fleet, origin, "build/hydra-fleet");
  if (argc > 1)
    assert(!f_copy(fixture, sizeof fixture, argv[1]));
  else
    nt_path(fixture, origin, "build/native-tests/enrollment-receiver-fixture");
  void (*cases[])(void) = {duplicate,  changed, concurrent,
                           owner_loss, invalid, corrupt};
  for (size_t i = 0; i < 6; i++) {
    setup();
    cases[i]();
    cleanup();
    printf("Enrollment receiver case %zu passed\n", i + 1);
  }
  return 0;
}
