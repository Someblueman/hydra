#include "enrollment.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
char ec_root[F_PATH], ec_home[F_PATH], ec_project[F_PATH], ec_counter[F_PATH],
    ec_cli_path[F_PATH], ec_fleet[F_PATH], ec_origin[F_PATH];
static char ssh_fixture[F_PATH], count_fixture[F_PATH], default_config[F_PATH];
static char *original_path;
void ec_setup(void) {
  char tmp[] = "/tmp/hydra-native-enrollment-XXXXXX", ssh[F_PATH], path[16384],
       receivers[F_PATH], installs[F_PATH];
  assert(mkdtemp(tmp));
  assert(!f_copy(ec_root, sizeof ec_root, tmp));
  nt_path(ec_home, ec_root, "home");
  assert(!f_mkdirs(ec_home));
  nt_path(ec_project, ec_root, "project");
  char *args[] = {"git", "init", "-q", ec_project, NULL};
  struct f_capture c = nt_run(args);
  assert(!c.status);
  f_capture_free(&c);
  nt_path(ec_counter, ec_root, "mutations");
  nt_path(ssh, ec_root, "ssh");
  assert(!symlink(ssh_fixture, ssh));
  nt_path(receivers, ec_root, "receivers");
  nt_path(installs, ec_root, "installs");
  nt_path(default_config, ec_root, "default-ssh-config");
  nt_write(default_config, "Host *\n HostName %h\n");
  int n = snprintf(path, sizeof path, "%s:%s", ec_root, original_path);
  assert(n > 0 && (size_t)n < sizeof path);
  assert(!setenv("HYDRA_HOME", ec_home, 1) && !setenv("PATH", path, 1) &&
         !setenv("HYDRA_FLEET_BIN", ec_fleet, 1) &&
         !setenv("HYDRA_BIN_CMD", count_fixture, 1) &&
         !setenv("ENROLL_COUNT_WRAPPER", count_fixture, 1) &&
         !setenv("ENROLL_REAL", ec_cli_path, 1) &&
         !setenv("ENROLL_COUNTER", ec_counter, 1) &&
         !setenv("ENROLL_FINGERPRINT", "SHA256:fixture", 1) &&
         !setenv("ENROLL_RECEIVERS", receivers, 1) &&
         !setenv("ENROLL_INSTALL_COUNTER", installs, 1));
  const char *vars[] = {
      "ENROLL_HOST_POLICY",         "ENROLL_DROP_INSTALL_RESPONSE",
      "ENROLL_DROP_INIT_RESPONSE",  "ENROLL_STDOUT_MARKER",
      "ENROLL_OLD_RECEIVER_PREFIX", "ENROLL_HOLD_PREFLIGHT",
      "ENROLL_HOLD_QUALIFY",        "ENROLL_HOLD",
      "ENROLL_CHILD_PID",           "ENROLL_STARTED"};
  for (size_t i = 0; i < 10; i++)
    assert(!unsetenv(vars[i]));
}
void ec_cleanup(void) {
  assert(!setenv("PATH", original_path, 1));
  assert(!f_remove_tree(ec_root));
}
static void arguments(char **out, bool enroll, const char *const extra[]) {
  out[0] = ec_cli_path;
  out[1] = "fleet";
  size_t start = 2;
  if (enroll)
    out[start++] = "enroll";
  size_t i = 0;
  while (extra[i]) {
    assert(i < 240);
    out[start + i] = (char *)extra[i];
    i++;
  }
  out[start + i] = NULL;
}
json_object *ec_cli(bool enroll, const char *const extra[], bool ok) {
  char *args[248];
  arguments(args, enroll, extra);
  struct f_capture c = nt_run(args);
  if ((c.status == 0) != ok)
    fprintf(stderr, "%s\n%s", c.out, c.err);
  assert((c.status == 0) == ok);
  json_object *v = f_parse(c.out);
  assert(v);
  f_capture_free(&c);
  return v;
}
struct nt_child ec_start(bool enroll, const char *const extra[]) {
  char *args[248];
  arguments(args, enroll, extra);
  return nt_start(args, NULL);
}
void ec_qualify(char out[F_PATH], const char *fingerprint, const char *config,
                const char *const targets[]) {
  const char *args[40];
  args[0] = "qualify";
  args[1] = "--require";
  args[2] = "list";
  size_t n = 3;
  if (targets) {
    for (size_t i = 0; targets[i]; i++) {
      assert(n < 34);
      args[n++] = "--ssh";
      args[n++] = targets[i];
    }
  } else {
    args[n++] = "--ssh";
    args[n++] = "good";
  }
  args[n++] = "--ssh-config";
  args[n++] = config ? config : default_config;
  args[n] = NULL;
  json_object *v = ec_cli(false, args, true),
              *rows = f_field(f_field(v, "data"), "candidates");
  for (size_t i = 0; i < json_object_array_length(rows); i++) {
    json_object *r = json_object_array_get_idx(rows, i);
    assert(!strcmp(f_string(r, "status"), "compatible"));
    f_string_add(f_field(f_field(r, "qualification"), "data"),
                 "peer_fingerprint",
                 fingerprint ? fingerprint : "SHA256:fixture");
  }
  nt_path(out, ec_root, "qualification.json");
  nt_json_write(out, v);
  json_object_put(v);
}
void ec_review(char out[F_PATH], char digest[65], const char *qualification,
               json_object *candidates, const struct ec_package *package,
               const char *project) {
  nt_path(out, ec_root, "intent.json");
  const char *args[220];
  size_t n = 0;
  args[n++] = "review";
  args[n++] = "--input";
  args[n++] = qualification;
  args[n++] = "--project";
  args[n++] = project ? project : ec_project;
  args[n++] = "--output";
  args[n++] = out;
  if (candidates) {
    for (size_t i = 0; i < json_object_array_length(candidates); i++) {
      assert(n < 205);
      args[n++] = "--candidate";
      args[n++] =
          json_object_get_string(json_object_array_get_idx(candidates, i));
    }
  } else {
    args[n++] = "--candidate";
    args[n++] = "cand_676f6f64";
  }
  if (package) {
    args[n++] = "--package";
    args[n++] = package->path;
    args[n++] = "--sha256";
    args[n++] = package->digest;
    args[n++] = "--prefix";
    args[n++] = package->prefix;
  }
  args[n] = NULL;
  json_object_put(ec_cli(true, args, true));
  json_object *v = f_read_json(out, 8000000);
  assert(v && !f_copy(digest, 65, f_string(v, "intent_sha256")));
  json_object_put(v);
}
json_object *ec_apply(const char *intent, const char *digest) {
  json_object *v = EC(true, true, "apply", "--input", intent, "--confirm",
                      digest),
              *data = nt_clone(f_field(v, "data"));
  json_object_put(v);
  return data;
}
void ec_one_review(char intent[F_PATH], char digest[65],
                   const struct ec_package *package) {
  char qualification[F_PATH];
  ec_qualify(qualification, NULL, NULL, NULL);
  ec_review(intent, digest, qualification, NULL, package, NULL);
}
void ec_status(json_object *v, const char *expected) {
  assert(!strcmp(
      f_string(json_object_array_get_idx(f_field(v, "hosts"), 0), "status"),
      expected));
  json_object_put(v);
}
void ec_count(const char *name, const char *line, size_t count) {
  char path[F_PATH];
  nt_path(path, ec_root, name);
  char *v = f_read(path, 1000000);
  if (!count) {
    assert(!v || !*v);
    free(v);
    return;
  }
  assert(v);
  const char *p = v;
  for (size_t i = 0; i < count; i++) {
    assert(!strncmp(p, line, strlen(line)));
    p += strlen(line);
  }
  assert(!*p);
  free(v);
}
void ec_package_make(struct ec_package *p) {
  const char *binary = getenv("HYDRA_TEST_PACKAGE_BINARY");
  assert(!f_copy(p->binary, sizeof p->binary, binary ? binary : ec_fleet));
  nt_path(p->path, ec_root, "package");
  nt_path(p->prefix, ec_root, "exact-prefix");
  json_object *v = EC(false, true, "package", "--source", ec_origin, "--binary",
                      p->binary, "--output", p->path);
  assert(!f_copy(p->digest, sizeof p->digest,
                 f_string(f_field(v, "data"), "sha256")));
  json_object_put(v);
}
static void duplicate(void) {
  char intent[F_PATH], digest[65];
  ec_one_review(intent, digest, NULL);
  ec_status(ec_apply(intent, digest), "enrolled");
  ec_status(ec_apply(intent, digest), "enrolled");
  ec_count("mutations", "init\n", 1);
}
static void mismatch(void) {
  char q[F_PATH], intent[F_PATH], digest[65];
  ec_qualify(q, "SHA256:reviewed", NULL, NULL);
  ec_review(intent, digest, q, NULL, NULL, ec_root);
  ec_status(ec_apply(intent, digest), "host_key_changed");
  ec_count("mutations", "init\n", 0);
}
static void stdout_marker(void) {
  char q[F_PATH], intent[F_PATH], digest[65];
  ec_qualify(q, NULL, NULL, NULL);
  assert(!setenv("ENROLL_STDOUT_MARKER", "1", 1) &&
         !setenv("ENROLL_FINGERPRINT", "", 1));
  ec_review(intent, digest, q, NULL, NULL, ec_root);
  ec_status(ec_apply(intent, digest), "outcome_unknown");
  ec_count("mutations", "init\n", 0);
}
static void lost_response(void) {
  char drop[F_PATH], intent[F_PATH], digest[65];
  nt_path(drop, ec_root, "dropped");
  assert(!setenv("ENROLL_DROP_INIT_RESPONSE", drop, 1));
  ec_one_review(intent, digest, NULL);
  ec_status(ec_apply(intent, digest), "outcome_unknown");
  ec_status(ec_apply(intent, digest), "enrolled");
  ec_count("mutations", "init\n", 1);
}
static void concurrent(void) {
  char intent[F_PATH], digest[65], dir[F_PATH], lock[F_PATH], name[80];
  ec_one_review(intent, digest, NULL);
  nt_path(dir, ec_home, "fleet/enrollment");
  assert(!f_mkdirs(dir));
  assert(snprintf(name, sizeof name, "%s.json.lock", digest) > 0);
  nt_path(lock, dir, name);
  int fd = open(lock, O_CREAT | O_WRONLY, 0600);
  assert(fd >= 0 && !flock(fd, LOCK_EX));
  json_object *v =
      EC(true, false, "apply", "--input", intent, "--confirm", digest);
  assert(!close(fd));
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "apply_in_progress"));
  json_object_put(v);
  ec_count("mutations", "init\n", 0);
}
static void config_changed(void) {
  char config[F_PATH], q[F_PATH], intent[F_PATH], digest[65];
  nt_path(config, ec_root, "ssh-config");
  nt_write(config, "Host good\n HostName good\n User tester\n");
  ec_qualify(q, NULL, config, NULL);
  ec_review(intent, digest, q, NULL, NULL, NULL);
  nt_write(config, "Host good\n HostName changed\n User other\n");
  ec_status(ec_apply(intent, digest), "review_required");
  ec_count("mutations", "init\n", 0);
}
static void real_qualify(void) {
  char config[F_PATH], q[F_PATH], intent[F_PATH], digest[65], alias[F_PATH];
  nt_path(config, ec_root, "ssh-config");
  nt_write(config, "Host good\n HostName good\n User tester\n");
  json_object *v = EC(false, true, "qualify", "--ssh", "good", "--ssh-config",
                      config, "--require", "list");
  nt_path(q, ec_root, "qualified.json");
  nt_json_write(q, v);
  json_object_put(v);
  ec_review(intent, digest, q, NULL, NULL, NULL);
  ec_status(ec_apply(intent, digest), "enrolled");
  nt_path(alias, ec_home, "fleet/remotes/cand_676f6f64.json");
  v = f_read_json(alias, 100000);
  assert(v && !strcmp(f_string(v, "target"), "good") &&
         !strcmp(f_string(v, "ssh_config"), config));
  json_object_put(v);
}
static void mixed(void) {
  char target[10][32], q[F_PATH], intent[F_PATH], digest[65];
  const char *targets[11];
  for (size_t i = 0; i < 10; i++) {
    assert(snprintf(target[i], sizeof target[i], "host%zu", i) > 0);
    targets[i] = target[i];
  }
  targets[10] = NULL;
  ec_qualify(q, NULL, NULL, targets);
  json_object *v = f_read_json(q, 8000000),
              *candidates = json_object_new_array(),
              *rows = f_field(f_field(v, "data"), "candidates");
  for (size_t i = 0; i < json_object_array_length(rows); i++)
    json_object_array_add(
        candidates, json_object_new_string(f_string(
                        json_object_array_get_idx(rows, i), "candidate_id")));
  ec_review(intent, digest, q, candidates, NULL, NULL);
  json_object_put(candidates);
  json_object_put(v);
  assert(!setenv("ENROLL_HOST_POLICY",
                 "{\"host1\":{\"fingerprint\":\"SHA256:changed\"},\"host3\":{"
                 "\"missing_capability\":true},\"host5\":{\"offline\":true},"
                 "\"host7\":{\"bad_project\":true}}",
                 1));
  v = ec_apply(intent, digest);
  rows = f_field(v, "hosts");
  assert(json_object_array_length(rows) == 10);
  for (size_t i = 0; i < 10; i++) {
    char alias[64];
    assert(snprintf(alias, sizeof alias, "cand_686f7374%02x",
                    (unsigned)('0' + i)) > 0);
    json_object *found = NULL;
    for (size_t j = 0; j < 10; j++)
      if (!strcmp(f_string(json_object_array_get_idx(rows, j), "alias"), alias))
        found = json_object_array_get_idx(rows, j);
    assert(found);
    const char *expected = i == 1   ? "host_key_changed"
                           : i == 3 ? "capability_changed"
                           : i == 5 ? "outcome_unknown"
                           : i == 7 ? "project_unavailable"
                                    : "enrolled";
    assert(!strcmp(f_string(found, "status"), expected));
  }
  nt_equal(f_field(v, "partial_failure"), "true");
  ec_count("mutations", "init\n", 6);
  json_object *second = ec_apply(intent, digest);
  assert(json_object_equal(f_field(second, "hosts"), rows));
  json_object_put(second);
  json_object_put(v);
  ec_count("mutations", "init\n", 6);
}
static void intent_changed(void) {
  char intent[F_PATH], digest[65];
  ec_one_review(intent, digest, NULL);
  json_object *original = f_read_json(intent, 8000000);
  assert(original);
  const char *fields[] = {"target", "project", "fingerprint", "principal",
                          "required_capability"},
             *values[] = {"other", ec_root, "SHA256:other", "other", "spawn"};
  for (size_t i = 0; i < 5; i++) {
    json_object *v = nt_clone(original);
    f_string_add(json_object_array_get_idx(f_field(v, "hosts"), 0), fields[i],
                 values[i]);
    nt_json_write(intent, v);
    json_object_put(v);
    v = EC(true, false, "apply", "--input", intent, "--confirm", digest);
    assert(!strcmp(f_string(f_field(v, "error"), "code"), "invalid_intent"));
    json_object_put(v);
  }
  json_object_put(original);
  ec_count("mutations", "init\n", 0);
}
static void alias_conflict(void) {
  char intent[F_PATH], digest[65], dir[F_PATH], alias[F_PATH];
  ec_one_review(intent, digest, NULL);
  nt_path(dir, ec_home, "fleet/remotes");
  assert(!f_mkdirs(dir));
  nt_path(alias, dir, "cand_676f6f64.json");
  json_object *existing =
      f_parse("{\"schema_version\":1,\"target\":\"other\",\"hydra\":\"hydra\","
              "\"home\":\"\",\"multiplex\":false}");
  nt_json_write(alias, existing);
  ec_status(ec_apply(intent, digest), "alias_conflict");
  json_object *actual = f_read_json(alias, 100000);
  assert(json_object_equal(actual, existing));
  json_object_put(actual);
  json_object_put(existing);
  ec_count("mutations", "init\n", 0);
}
static void interrupt_apply(void) {
  char intent[F_PATH], digest[65], hold[F_PATH], started[F_PATH];
  ec_one_review(intent, digest, NULL);
  nt_path(hold, ec_root, "preflight-release");
  assert(snprintf(started, sizeof started, "%s.started", hold) > 0);
  assert(!setenv("ENROLL_HOLD_PREFLIGHT", hold, 1));
  const char *args[] = {"apply", "--input", intent, "--confirm", digest, NULL};
  struct nt_child child = ec_start(true, args);
  nt_await_file(started, 10);
  assert(!kill(child.pid, SIGTERM));
  struct f_capture c = nt_wait(&child, 10);
  assert(*c.out && !c.timeout);
  f_capture_free(&c);
  ec_count("mutations", "init\n", 0);
  nt_write(hold, "");
  assert(!unsetenv("ENROLL_HOLD_PREFLIGHT"));
  ec_status(ec_apply(intent, digest), "enrolled");
  ec_count("mutations", "init\n", 1);
}
int main(int argc, char **argv) {
  assert(getcwd(ec_origin, sizeof ec_origin));
  nt_path(ec_cli_path, ec_origin, "bin/hydra");
  const char *fleet = getenv("HYDRA_FLEET_BIN");
  if (fleet)
    assert(!f_copy(ec_fleet, sizeof ec_fleet, fleet));
  else
    nt_path(ec_fleet, ec_origin, "build/hydra-fleet");
  if (argc > 1)
    assert(!f_copy(ssh_fixture, sizeof ssh_fixture, argv[1]));
  else
    nt_path(ssh_fixture, ec_origin,
            "build/native-tests/enrollment-ssh-fixture");
  if (argc > 2)
    assert(!f_copy(count_fixture, sizeof count_fixture, argv[2]));
  else
    nt_path(count_fixture, ec_origin,
            "build/native-tests/enrollment-receiver-fixture");
  const char *path_env = getenv("PATH");
  assert(path_env);
  original_path = strdup(path_env);
  assert(original_path);
  void (*cases[])(void) = {duplicate,      mismatch,       stdout_marker,
                           lost_response,  concurrent,     config_changed,
                           real_qualify,   mixed,          intent_changed,
                           alias_conflict, interrupt_apply};
  for (size_t i = 0; i < 11; i++) {
    ec_setup();
    cases[i]();
    ec_cleanup();
    printf("Enrollment case %zu passed\n", i + 1);
  }
  ec_setup();
  ec_batch_case();
  ec_cleanup();
  ec_package_cases();
  free(original_path);
  return 0;
}
