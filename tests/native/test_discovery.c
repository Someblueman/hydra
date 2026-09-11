#include "discovery.h"
#include <dirent.h>
char dc_root[F_PATH], dc_home[F_PATH], dc_config[F_PATH], dc_included[F_PATH],
    dc_calls[F_PATH], dc_pid[F_PATH], dc_errors[F_PATH], dc_hydra[F_PATH];
static char origin[F_PATH], fixture[F_PATH], fleet[F_PATH], *original_path,
    *ssh;
static json_object *before;
static void state_files(json_object *v, const char *dir) {
  DIR *d = opendir(dir);
  struct dirent *e;
  assert(d);
  while ((e = readdir(d))) {
    char p[F_PATH], hash[65];
    struct stat st;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    nt_path(p, dir, e->d_name);
    assert(!stat(p, &st));
    if (S_ISDIR(st.st_mode))
      state_files(v, p);
    else if (S_ISREG(st.st_mode)) {
      assert(!f_hash(p, hash));
      f_string_add(v, p, hash);
    }
  }
  closedir(d);
}
static void unchanged(void) {
  json_object *v = json_object_new_object();
  state_files(v, dc_home);
  assert(json_object_equal(v, before));
  json_object_put(v);
}
void dc_setup(void) {
  char tmp[] = "/tmp/hydra-native-discovery-XXXXXX", bin[F_PATH],
       wrapper[F_PATH], path[16384], p[F_PATH];
  assert(mkdtemp(tmp));
  assert(!f_copy(dc_root, sizeof dc_root, tmp));
  nt_path(dc_home, dc_root, "home");
  nt_path(dc_config, dc_root, "ssh.config");
  nt_path(dc_included, dc_root, "included.config");
  nt_path(dc_calls, dc_root, "calls");
  nt_path(dc_pid, dc_root, "pid");
  nt_path(dc_errors, dc_root, "errors");
  nt_write(dc_included,
           "Host good other\n HostName fixture.invalid\n User builder\n Port "
           "2222\n ProxyJump jump\n IdentityFile /fixture/key\nHost jump\n "
           "HostName jump.invalid\n User jumper\n Port 2200\n "
           "StrictHostKeyChecking no\n BatchMode no\n UpdateHostKeys yes\n");
  FILE *f = fopen(dc_config, "w");
  assert(f);
  assert(fprintf(f,
                 "Include \"%s\"\nHost *\n ControlMaster auto\n ControlPath "
                 "/unsafe/socket\n LocalCommand touch /must-not-run\n "
                 "PermitLocalCommand yes\n ForwardAgent yes\n "
                 "StrictHostKeyChecking no\n UpdateHostKeys yes\n",
                 dc_included) > 0 &&
         !fclose(f));
  nt_path(bin, dc_root, "bin");
  assert(!f_mkdirs(bin));
  nt_path(wrapper, bin, "ssh");
  assert(!symlink(fixture, wrapper));
  assert(!f_mkdirs(dc_home));
  nt_path(p, dc_home, "sentinel");
  nt_write(p, "preserve remote state and installation");
  nt_path(p, dc_home, "fleet/remotes");
  assert(!f_mkdirs(p));
  nt_path(p, dc_home, "fleet/remotes/existing.json");
  nt_write(p, "{\"schema_version\":1,\"target\":\"existing\",\"hydra\":"
              "\"hydra\",\"home\":\"\",\"multiplex\":false}");
  before = json_object_new_object();
  state_files(before, dc_home);
  int n = snprintf(path, sizeof path, "%s:%s", bin, original_path);
  assert(n > 0 && (size_t)n < sizeof path);
  assert(!setenv("PATH", path, 1) && !setenv("HYDRA_HOME", dc_home, 1) &&
         !setenv("HYDRA_FLEET_BIN", fleet, 1) &&
         !setenv("HD_REAL_SSH", ssh, 1) && !setenv("HD_CALLS", dc_calls, 1) &&
         !setenv("HD_PID", dc_pid, 1) && !setenv("HD_ERRORS", dc_errors, 1));
}
void dc_cleanup(void) {
  assert(!setenv("PATH", original_path, 1));
  json_object_put(before);
  assert(!f_remove_tree(dc_root));
}
static void args(char **out, const char *action, const char *const extra[]) {
  out[0] = dc_hydra;
  out[1] = "fleet";
  out[2] = (char *)action;
  out[3] = "--ssh-config";
  out[4] = dc_config;
  out[5] = "--json";
  size_t i = 0;
  while (extra[i]) {
    assert(i < 240);
    out[6 + i] = (char *)extra[i];
    i++;
  }
  out[6 + i] = NULL;
}
json_object *dc_cli(const char *action, const char *const extra[], bool ok) {
  char *argv[248];
  args(argv, action, extra);
  struct f_capture c = nt_run(argv);
  if (!access(dc_errors, F_OK)) {
    char *error = f_read(dc_errors, 100000);
    fprintf(stderr, "%s\n", error);
    free(error);
    abort();
  }
  json_object *v = nt_output(&c, ok ? 0 : 1);
  assert(!strstr(c.out, "secret-test-token"));
  f_capture_free(&c);
  return v;
}
struct nt_child dc_start(const char *action, const char *const extra[]) {
  char *argv[248];
  args(argv, action, extra);
  return nt_start(argv, NULL);
}
json_object *dc_rows(json_object *v) {
  return f_field(f_field(v, "data"), "candidates");
}
void dc_error(json_object *v, const char *code) {
  assert(!strcmp(f_string(f_field(v, "error"), "code"), code));
  json_object_put(v);
}
size_t dc_call_count(void) {
  char *s = f_read(dc_calls, 2000000);
  if (!s)
    return 0;
  size_t count = 0;
  for (char *p = s; *p; p++)
    if (*p == '\n')
      count++;
  free(s);
  return count;
}
void dc_snapshot(char out[F_PATH], const char *kind, json_object *records,
                 int64_t observed) {
  char name[80], locator[80];
  assert(snprintf(name, sizeof name, "%s.json", kind) > 0 &&
         snprintf(locator, sizeof locator, "fixture:%s", kind) > 0);
  nt_path(out, dc_root, name);
  json_object *v =
      f_parse("{\"schema_version\":2,\"hosts\":[],\"source_snapshot\":{"
              "\"scope\":\"fixture\",\"freshness\":\"source_reported\"}}");
  json_object_object_add(v, "observed_at", json_object_new_int64(observed));
  json_object *snapshot = f_field(v, "source_snapshot");
  f_string_add(snapshot, "kind", kind);
  f_string_add(snapshot, "locator", locator);
  json_object_object_add(snapshot, "observed_at",
                         json_object_new_int64(observed));
  json_object_object_add(snapshot, "records", records);
  nt_json_write(out, v);
  json_object_put(v);
}
static void aliases(void) {
  char *config = f_read(dc_config, 100000),
       *included = f_read(dc_included, 100000);
  json_object *v = DC("discover", true, "--ssh", "good", "--ssh", "other",
                      "--ssh", "good"),
              *rows = dc_rows(v);
  assert(json_object_array_length(rows) == 2);
  json_object *a = json_object_array_get_idx(rows, 0),
              *b = json_object_array_get_idx(rows, 1);
  assert(!strcmp(f_string(a, "target"), "good") &&
         !strcmp(f_string(b, "target"), "other"));
  json_object *e = f_field(f_field(a, "resolution"), "data");
  nt_equal(f_field(e, "hostname"), "[\"fixture.invalid\"]");
  nt_equal(f_field(e, "user"), "[\"builder\"]");
  nt_equal(f_field(e, "port"), "[\"2222\"]");
  nt_equal(f_field(e, "proxyjump"), "[\"jump\"]");
  assert(strstr(json_object_to_json_string(f_field(e, "identityfile")),
                "/fixture/key") ||
         strstr(json_object_to_json_string(f_field(e, "identityfile")),
                "\\/fixture\\/key"));
  assert(strcmp(f_string(a, "candidate_id"), f_string(b, "candidate_id")));
  assert(!f_field(a, "verified_identity") && !f_field(b, "verified_identity") &&
         access(dc_calls, F_OK));
  json_object *second = DC("discover", true, "--ssh", "other", "--ssh", "good");
  for (size_t i = 0; i < 2; i++)
    assert(!strcmp(f_string(json_object_array_get_idx(rows, i), "candidate_id"),
                   f_string(json_object_array_get_idx(dc_rows(second), i),
                            "candidate_id")));
  json_object_put(second);
  json_object_put(v);
  v = DC("qualify", true, "--ssh", "good");
  assert(!strcmp(f_string(json_object_array_get_idx(dc_rows(v), 0), "status"),
                 "compatible"));
  json_object_put(v);
  char *after = f_read(dc_config, 100000);
  assert(after && !strcmp(config, after));
  free(after);
  after = f_read(dc_included, 100000);
  assert(after && !strcmp(included, after));
  free(after);
  free(config);
  free(included);
  unchanged();
  assert(dc_call_count() == 1);
  char *calls = f_read(dc_calls, 100000);
  v = f_parse(calls);
  assert(v && !strcmp(f_string(v, "target"), "good") &&
         access(f_string(v, "config"), F_OK));
  json_object_put(v);
  free(calls);
}
static void mixed(void) {
  const char *hosts[] = {"good", "unknown", "changed",   "auth", "offline",
                         "skew", "nocap",   "malformed", "slow", "keygeneric"},
             *codes[] = {NULL,
                         "host_key_unknown",
                         "host_key_changed",
                         "authentication_failed",
                         "offline",
                         "version_mismatch",
                         "capability_unavailable",
                         "invalid_response",
                         "timeout",
                         "host_key_failed"};
  const char *extra[23];
  for (size_t i = 0; i < 10; i++) {
    extra[2 * i] = "--ssh";
    extra[2 * i + 1] = hosts[9 - i];
  }
  extra[20] = "--timeout";
  extra[21] = "1";
  extra[22] = NULL;
  json_object *v = dc_cli("qualify", extra, false), *rows = dc_rows(v);
  assert(json_object_array_length(rows) == 10);
  for (size_t i = 0; i < 10; i++) {
    json_object *r = json_object_array_get_idx(rows, i);
    if (i)
      assert(strcmp(f_string(json_object_array_get_idx(rows, i - 1), "target"),
                    f_string(r, "target")) < 0);
    size_t index = 0;
    while (index < 10 && strcmp(hosts[index], f_string(r, "target")))
      index++;
    assert(index < 10);
    json_object *error = f_field(f_field(r, "qualification"), "error");
    if (codes[index])
      assert(!strcmp(f_string(error, "code"), codes[index]));
    else
      assert(!error);
    assert(json_object_array_length(f_field(r, "sources")) &&
           !f_field(r, "verified_identity"));
    for (size_t j = 0; j < i; j++)
      assert(
          strcmp(f_string(r, "candidate_id"),
                 f_string(json_object_array_get_idx(rows, j), "candidate_id")));
    if (index == 5)
      nt_equal(f_field(f_field(f_field(r, "qualification"), "data"),
                       "fleet_protocol"),
               "99");
  }
  json_object_put(v);
  unchanged();
}
static void inventory(void) {
  char file[F_PATH];
  nt_path(file, dc_root, "inventory.json");
  json_object *original =
      f_parse("{\"schema_version\":1,\"observed_at\":1,\"hosts\":[{\"name\":"
              "\"chosen\",\"target\":\"good\",\"labels\":[\"build\"]},{"
              "\"name\":\"excluded\",\"target\":\"offline\",\"labels\":[]}]}");
  nt_json_write(file, original);
  const char *args[] = {"--inventory", file,   "--select", "chosen",
                        "--ssh",       "good", NULL};
  json_object *v = dc_cli("discover", args, true), *rows = dc_rows(v);
  assert(json_object_array_length(rows) == 1);
  json_object *sources = f_field(json_object_array_get_idx(rows, 0), "sources");
  assert(json_object_array_length(sources) == 2);
  json_object *s = json_object_array_get_idx(sources, 1);
  nt_equal(f_field(s, "labels"), "[\"build\"]");
  nt_equal(f_field(s, "observed_at"), "1");
  assert(json_object_get_int64(f_field(s, "age_seconds")) > 1000);
  json_object_put(v);
  const char *keys[] = {"schema_version", "command", "observed_at"},
             *values[] = {"2", "\"touch unsafe\"", "\"yesterday\""};
  for (size_t i = 0; i < 3; i++) {
    v = nt_clone(original);
    json_object_object_add(v, keys[i], f_parse_value(values[i]));
    nt_json_write(file, v);
    json_object_put(v);
    dc_error(dc_cli("discover", args, false), "invalid_inventory");
  }
  f_string_add(json_object_array_get_idx(f_field(original, "hosts"), 0),
               "target", "good;touch unsafe");
  nt_json_write(file, original);
  json_object_put(original);
  json_object_put(dc_cli("discover", args, false));
  assert(access(dc_calls, F_OK));
}
static void limits(void) {
  json_object_put(DC("discover", false, NULL));
  json_object_put(DC("discover", false, "--ssh", "good", "--timeout", "0"));
  const char *args[35];
  char names[17][32];
  for (size_t i = 0; i < 17; i++) {
    assert(snprintf(names[i], sizeof names[i], "host%zu", i) > 0);
    args[i * 2] = "--ssh";
    args[i * 2 + 1] = names[i];
  }
  args[34] = NULL;
  json_object_put(dc_cli("discover", args, false));
  json_object *v = DC("qualify", false, "--ssh", "badconfig", "--ssh", "good");
  assert(json_object_array_length(dc_rows(v)) == 2);
  json_object_put(v);
  v = DC("qualify", false, "--ssh", "oversized");
  assert(
      !strcmp(f_string(f_field(f_field(json_object_array_get_idx(dc_rows(v), 0),
                                       "qualification"),
                               "error"),
                       "code"),
              "output_limit"));
  json_object_put(v);
}
static void snapshots(void) {
  const char
      *kinds[] = {"mdns", "vpn", "cloud-tags", "config-management"},
      *names[] = {"printer", "peer-a", "i-1", "node-a"},
      *records[] = {
          "[{\"instance\":\"printer\",\"host\":\"good\",\"port\":22,\"labels\":"
          "[\"lan\"]}]",
          "[{\"peer\":\"peer-a\",\"address\":\"good\",\"labels\":[\"vpn\"]}]",
          "[{\"instance_id\":\"i-1\",\"private_ip\":\"good\",\"labels\":["
          "\"prod\"]}]",
          "[{\"host\":\"node-a\",\"address\":\"good\",\"labels\":[\"web\"]}]"};
  for (size_t i = 0; i < 4; i++) {
    char file[F_PATH], locator[80];
    dc_snapshot(file, kinds[i], f_parse_value(records[i]), 1);
    json_object *v = DC("discover", true, "--inventory", file, "--select",
                        names[i], "--ssh", "good"),
                *row = json_object_array_get_idx(dc_rows(v), 0),
                *sources = f_field(row, "sources");
    assert(!strcmp(f_string(row, "target"), "good"));
    bool found = false;
    assert(snprintf(locator, sizeof locator, "fixture:%s", kinds[i]) > 0);
    for (size_t j = 0; j < json_object_array_length(sources); j++) {
      json_object *s = json_object_array_get_idx(sources, j);
      if (strcmp(f_string(s, "kind"), kinds[i]))
        continue;
      found = true;
      assert(!strcmp(f_string(s, "locator"), locator) &&
             !strcmp(f_string(s, "scope"), "fixture") &&
             !strcmp(f_string(s, "freshness"), "source_reported"));
      nt_equal(f_field(s, "observed_at"), "1");
    }
    assert(found);
    json_object_put(v);
  }
}
static void bad_snapshots(void) {
  json_object *original = f_parse(
      "{\"instance\":\"x\",\"host\":\"good\",\"port\":22,\"labels\":[]}");
  char file[F_PATH];
  const char *keys[] = {"token", "port", "port", "host"},
             *values[] = {"\"secret\"", "\"22\"", "2222", "\"-option\""};
  for (size_t i = 0; i < 6; i++) {
    json_object *r = nt_clone(original), *records = json_object_new_array();
    if (i < 4)
      json_object_object_add(r, keys[i], f_parse_value(values[i]));
    json_object_array_add(records, r);
    dc_snapshot(file, "mdns", records, i == 4 ? -1 : i == 5 ? 9999999999LL : 1);
    dc_error(DC("discover", false, "--inventory", file, "--select", "x"),
             "invalid_inventory");
  }
  nt_path(file, dc_root, "old.json");
  nt_write(file, "{\"schema_version\":1,\"observed_at\":1,\"hosts\":[],"
                 "\"source_snapshot\":{}}");
  dc_error(DC("discover", false, "--inventory", file, "--select", "x"),
           "invalid_inventory");
  json_object *records = json_object_new_array();
  json_object_array_add(records, nt_clone(original));
  f_string_add(original, "host", "other");
  json_object_array_add(records, original);
  dc_snapshot(file, "mdns", records, 1);
  json_object_put(DC("discover", false, "--inventory", file, "--select", "x"));
}
static void hundred(void) {
  json_object *records = json_object_new_array();
  char names[101][32], targets[100][32], file[F_PATH];
  const char *args[205];
  args[0] = "--inventory";
  args[1] = file;
  for (size_t i = 0; i < 100; i++) {
    assert(snprintf(names[i], sizeof names[i], "h%zu", i) > 0 &&
           snprintf(targets[i], sizeof targets[i], "host%zu", i) > 0);
    json_object *r = f_parse("{\"labels\":[]}");
    f_string_add(r, "host", names[i]);
    f_string_add(r, "address", targets[i]);
    json_object_array_add(records, r);
    args[2 + 2 * i] = "--select";
    args[3 + 2 * i] = names[i];
  }
  args[202] = NULL;
  dc_snapshot(file, "config-management", nt_clone(records), 1);
  json_object *v = dc_cli("discover", args, true), *rows = dc_rows(v);
  assert(json_object_array_length(rows) == 100);
  for (size_t i = 0; i < 100; i++) {
    size_t count = 0;
    for (size_t j = 0; j < 100; j++)
      if (!strcmp(f_string(json_object_array_get_idx(rows, j), "target"),
                  targets[i]))
        count++;
    assert(count == 1);
  }
  json_object_put(v);
  json_object_array_add(
      records,
      f_parse("{\"host\":\"h100\",\"address\":\"good\",\"labels\":[]}"));
  dc_snapshot(file, "config-management", records, 1);
  args[202] = "--select";
  args[203] = "h100";
  args[204] = NULL;
  json_object_put(dc_cli("discover", args, false));
}
int main(int argc, char **argv) {
  assert(getcwd(origin, sizeof origin));
  nt_path(dc_hydra, origin, "bin/hydra");
  const char *f = getenv("HYDRA_FLEET_BIN");
  if (f)
    assert(!f_copy(fleet, sizeof fleet, f));
  else
    nt_path(fleet, origin, "build/hydra-fleet");
  if (argc > 1)
    assert(!f_copy(fixture, sizeof fixture, argv[1]));
  else
    nt_path(fixture, origin, "build/native-tests/discovery-ssh-fixture");
  const char *path_env = getenv("PATH");
  assert(path_env);
  original_path = strdup(path_env);
  assert(original_path);
  char *cmd[] = {"sh", "-c", "command -v ssh", NULL};
  struct f_capture c = nt_run(cmd);
  assert(!c.status);
  ssh = strdup(c.out);
  assert(ssh);
  ssh[strcspn(ssh, "\r\n")] = 0;
  f_capture_free(&c);
  void (*cases[])(void) = {aliases,   mixed,         inventory, limits,
                           snapshots, bad_snapshots, hundred};
  for (size_t i = 0; i < 7; i++) {
    dc_setup();
    cases[i]();
    dc_cleanup();
    printf("Discovery case %zu passed\n", i + 1);
  }
  dc_progress_cases();
  free(original_path);
  free(ssh);
  return 0;
}
