/* Host key trust against a real loopback sshd (design §8 S2). Every client
 * uses an explicit -F config with its own UserKnownHostsFile, and every
 * refusal must leave that known_hosts file byte-identical. */
#include "support.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>

static char root[F_PATH], fleet[F_PATH], client_key[F_PATH], host_key[F_PATH], host_pub[F_PATH];
static unsigned port;
static struct nt_child daemon_child;

static void run_ok(char *const argv[]) {
  struct f_capture c = nt_run(argv);
  if (c.status)
    fprintf(stderr, "%s\n%s\n", c.out, c.err);
  assert(!c.status);
  f_capture_free(&c);
}
static void keygen(const char *path, const char *type) {
  char pub[F_PATH];
  char *args[] = {"ssh-keygen", "-q", "-t", (char *)type, "-N", "", "-f", (char *)path, NULL};
  assert(snprintf(pub, sizeof pub, "%s.pub", path) < (int)sizeof pub);
  unlink(path);
  unlink(pub);
  run_ok(args);
}
/* Caller-owned SHA256 fingerprint of a public key file. */
static char *fingerprint(const char *pub) {
  char *args[] = {"ssh-keygen", "-lf", (char *)pub, NULL}, *save = NULL, *fp;
  struct f_capture c = nt_run(args);
  assert(!c.status && strtok_r(c.out, " ", &save));
  fp = strtok_r(NULL, " ", &save);
  assert(fp && !strncmp(fp, "SHA256:", 7));
  fp = strdup(fp);
  f_capture_free(&c);
  return fp;
}
static unsigned free_port(void) {
  struct sockaddr_in address = {0};
  socklen_t len = sizeof address;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(!bind(fd, (struct sockaddr *)&address, sizeof address));
  assert(!getsockname(fd, (struct sockaddr *)&address, &len) && !close(fd));
  return ntohs(address.sin_port);
}
static bool server_ready(void) {
  struct sockaddr_in address = {0};
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  bool ready;
  assert(fd >= 0);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons((unsigned short)port);
  ready = !connect(fd, (struct sockaddr *)&address, sizeof address);
  assert(!close(fd));
  return ready;
}
static void stop_server(void) {
  struct f_capture c;
  if (daemon_child.pid <= 0)
    return;
  kill(daemon_child.pid, SIGTERM);
  c = nt_wait(&daemon_child, 5);
  f_capture_free(&c);
  daemon_child.pid = 0;
}
static void start_server(void) {
  char config[F_PATH], authorized[F_PATH];
  struct passwd *pw = getpwuid(getuid());
  struct timespec pause = {0, 20000000};
  size_t i;
  FILE *f;
  nt_path(config, root, "sshd_config");
  nt_path(authorized, root, "authorized_keys");
  assert(pw && (f = fopen(config, "w")));
  assert(fprintf(f, "Port %u\nListenAddress 127.0.0.1\nHostKey %s\nPidFile %s/pid\nAuthorizedKeysFile %s\n"
                    "StrictModes no\nUsePAM no\nPasswordAuthentication no\nKbdInteractiveAuthentication no\n"
                    "AllowUsers %s\nLogLevel ERROR\n",
                 port, host_key, root, authorized, pw->pw_name) > 0 && !fclose(f));
  char *args[] = {"/usr/sbin/sshd", "-D", "-e", "-f", config, NULL};
  daemon_child = nt_start(args, NULL);
  for (i = 0; i < 250 && !server_ready(); i++)
    nanosleep(&pause, NULL);
  assert(server_ready());
}
/* Writes an explicit client config for one known_hosts file. */
static void client_config(const char *name, const char *known, const char *extra, char config[F_PATH]) {
  struct passwd *pw = getpwuid(getuid());
  FILE *f;
  nt_path(config, root, name);
  assert(pw && (f = fopen(config, "w")));
  assert(fprintf(f, "Host fixture\n HostName 127.0.0.1\n Port %u\n User %s\n IdentityFile %s\n IdentitiesOnly yes\n"
                    " UserKnownHostsFile %s\n GlobalKnownHostsFile /dev/null\n%s",
                 port, pw->pw_name, client_key, known, extra) > 0 && !fclose(f));
}
static struct f_capture remote(const char *const extra[]) {
  char *args[24];
  size_t i;
  args[0] = fleet;
  args[1] = "remote";
  for (i = 0; extra[i]; i++) {
    assert(i < 21);
    args[i + 2] = (char *)extra[i];
  }
  args[i + 2] = NULL;
  return nt_run(args);
}
/* Runs hydra remote ... --json and checks the exit status and error code. */
static json_object *expect(int status, const char *code, const char *const extra[]) {
  struct f_capture c = remote(extra);
  json_object *v = nt_output(&c, status);
  const char *actual = f_string(f_field(v, "error"), "code");
  f_capture_free(&c);
  if (code && (!actual || strcmp(actual, code)))
    fprintf(stderr, "%s\n", json_object_to_json_string(v));
  assert(code ? actual && !strcmp(actual, code) : !actual);
  return v;
}
#define REMOTE(status, code, ...) expect(status, code, (const char *[]){__VA_ARGS__, NULL})
/* File content, or a marker for absence, for byte-identity checks. */
static char *snapshot(const char *path) {
  char *text = f_read(path, 1 << 20);
  return text ? text : strdup("<absent>");
}
static void unchanged(const char *path, char *before) {
  char *after = snapshot(path);
  assert(!strcmp(before, after));
  free(before);
  free(after);
}
static const char *next_arg(json_object *v, size_t index) {
  return f_text(json_object_array_get_idx(f_field(f_field(f_field(v, "data"), "next"), "argv"), index));
}
static void strict_ssh_ok(const char *config) {
  char *args[] = {"ssh", "-F", (char *)config, "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", "fixture", "exit 0", NULL};
  run_ok(args);
}

static void approval_required_first(const char *config, const char *known, const char *fp) {
  char *before = snapshot(known);
  json_object *v = REMOTE(3, "approval_required", "setup", "s1", "fixture", "--ssh-config", config, "--json");
  assert(!strcmp(next_arg(v, 2), "trust-key") && !strcmp(next_arg(v, 4), "--fingerprint") && !strcmp(next_arg(v, 5), fp));
  assert(!strcmp(f_string(f_field(f_field(v, "data"), "plan"), "fingerprint"), fp));
  json_object_put(v);
  unchanged(known, before);
  before = snapshot(known);
  json_object_put(REMOTE(1, "approval_mismatch", "trust-key", "s1", "--fingerprint", "SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "--json"));
  unchanged(known, before);
}
static void trusted_then_known(const char *config, const char *known, const char *fp) {
  struct stat st;
  char *text, state[F_PATH], *before;
  json_object *v = REMOTE(0, NULL, "trust-key", "s1", "--fingerprint", fp, "--json"), *record;
  assert(!strcmp(f_string(f_field(v, "data"), "result"), "trusted") && !strcmp(f_string(f_field(v, "data"), "fingerprint"), fp));
  json_object_put(v);
  text = f_read(known, 1 << 20);
  assert(text && !strncmp(text, "[127.0.0.1]:", 12) && strstr(text, " ssh-ed25519 ") && strchr(text, '\n') == text + strlen(text) - 1);
  free(text);
  assert(!stat(known, &st) && (st.st_mode & 0777) == 0600);
  strict_ssh_ok(config);
  nt_path(state, root, "home/fleet/setup/s1.json");
  record = f_read_json(state, 1 << 20);
  assert(record && !strcmp(f_string(f_field(record, "remote"), "accepted_host_key"), fp));
  json_object_put(record);
  before = snapshot(known);
  v = REMOTE(0, NULL, "trust-key", "s1", "--json");
  assert(!strcmp(f_string(f_field(v, "data"), "result"), "already trusted"));
  json_object_put(v);
  unchanged(known, before);
}
/* The guided flow resumes past the trusted key and runs preflight over sshd. */
static void preflight_over_sshd(void) {
  struct f_capture c = remote((const char *[]){"preflight", "s1", "--json", NULL}), u;
  char *uname_args[] = {"uname", "-s", NULL};
  json_object *v = f_parse(c.out), *data = f_field(v, "data"), *snapshot_data;
  u = nt_run(uname_args);
  u.out[strcspn(u.out, "\n")] = '\0';
  snapshot_data = f_field(data, "preflight") ? f_field(data, "preflight") : data;
  assert(v && !strcmp(f_string(snapshot_data, "schema"), "remote-preflight"));
  assert(!strcmp(f_string(snapshot_data, "os"), u.out) && f_string(snapshot_data, "home"));
  assert(json_object_is_type(f_field(snapshot_data, "requirements"), json_type_array));
  json_object_put(v);
  f_capture_free(&c);
  f_capture_free(&u);
}
/* ---- A user ~/.ssh/config with ControlPersist masters ---- */
static double seconds_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
/* Names a live user master socket (cm-*) under root, if any. */
static bool user_master(char found[F_PATH]) {
  DIR *dir = opendir(root);
  struct dirent *entry;
  bool any = false;
  assert(dir);
  while (!any && (entry = readdir(dir)))
    if (!strncmp(entry->d_name, "cm-", 3)) {
      nt_path(found, root, entry->d_name);
      any = true;
    }
  closedir(dir);
  return any;
}
/* Stops any master a failing build left behind, so the test never leaks one. */
static void stop_user_masters(const char *config) {
  char socket_path[F_PATH];
  size_t i;
  for (i = 0; i < 8 && user_master(socket_path); i++) {
    char *args[] = {"ssh", "-F", (char *)config, "-S", socket_path, "-O", "exit", "fixture", NULL};
    struct f_capture c = {0};
    (void)f_run(args, NULL, 0, 5, &c);
    f_capture_free(&c);
    unlink(socket_path);
  }
}
/* One hydra-fleet command that must finish promptly with any user master
 * configuration: returns its exit status. */
static int bounded(const char *config, const char *const extra[]) {
  char *args[24];
  struct f_capture c = {0};
  double started = seconds_now(), took;
  int status;
  size_t i;
  args[0] = fleet;
  for (i = 0; extra[i]; i++) {
    assert(i < 22);
    args[i + 1] = (char *)extra[i];
  }
  args[i + 1] = NULL;
  assert(!f_run(args, NULL, 0, 45, &c));
  took = seconds_now() - started;
  status = c.status;
  if (c.timeout || took > 15.0) {
    fprintf(stderr, "%s %s took %.1fs (timeout %d)\n%s\n%s\n", extra[0], extra[1], took, c.timeout, c.out, c.err);
    stop_user_masters(config);
    stop_server();
  }
  assert(!c.timeout && took <= 15.0);
  f_capture_free(&c);
  return status;
}
static void no_user_master(const char *config, const char *step) {
  char found[F_PATH];
  if (user_master(found)) {
    fprintf(stderr, "%s left a user SSH master running at %s\n", step, found);
    stop_user_masters(config);
    stop_server();
    assert(!"Hydra started a user ControlMaster");
  }
}
/* A very common ~/.ssh/config: Host * with ControlMaster auto and a long
 * ControlPersist. Hydra must neither start nor reuse such a master: a
 * daemonized master holding a captured pipe hung setup for its whole
 * persistence time, and a reused master skips host-key verification. */
static void control_persist_ignored(const char *known) {
  char config[F_PATH], extra[F_PATH + 128], remotes[F_PATH], alias[F_PATH], serve_home[F_PATH], record[F_PATH * 4];
  json_object *v;
  assert(snprintf(extra, sizeof extra, "Host *\n ControlMaster auto\n ControlPath %s/cm-%%C\n ControlPersist 15m\n", root) <
         (int)sizeof extra);
  client_config("config_persist", known, extra, config);
  stop_user_masters(config);
  bounded(config, (const char *[]){"remote", "setup", "s7", "fixture", "--ssh-config", config, "--json", NULL});
  no_user_master(config, "setup");
  v = REMOTE(0, NULL, "trust-key", "s7", "--json");
  assert(!strcmp(f_string(f_field(v, "data"), "result"), "already trusted"));
  json_object_put(v);
  no_user_master(config, "trust-key");
  bounded(config, (const char *[]){"remote", "preflight", "s7", "--json", NULL});
  no_user_master(config, "preflight");
  /* A fleet request through an alias that does not use --multiplex. */
  nt_path(remotes, root, "home/fleet/remotes");
  nt_path(alias, remotes, "persist.json");
  nt_path(serve_home, root, "serve-home");
  assert(!f_mkdirs(remotes));
  assert(snprintf(record, sizeof record, "{\"schema_version\":1,\"target\":\"fixture\",\"hydra\":\"%s\",\"home\":\"%s\",\"ssh_config\":\"%s\",\"multiplex\":false}",
                  fleet, serve_home, config) < (int)sizeof record);
  nt_write(alias, record);
  assert(!bounded(config, (const char *[]){"fleet", "list", "persist", "--json", NULL}));
  no_user_master(config, "fleet list");
  assert(!unlink(alias));
}
static void changed_key_refused(const char *known) {
  char *old = fingerprint(host_pub), *fp, *before = snapshot(known);
  json_object *v;
  stop_server();
  keygen(host_key, "ed25519");
  start_server();
  fp = fingerprint(host_pub);
  assert(strcmp(old, fp));
  v = REMOTE(1, "host_key_changed", "trust-key", "s1", "--json");
  assert(!strcmp(f_string(f_field(v, "data"), "presented_fingerprint"), fp));
  assert(strstr(f_string(f_field(v, "error"), "recovery"), "ssh-keygen -R"));
  json_object_put(v);
  json_object_put(REMOTE(1, "host_key_changed", "trust-key", "s1", "--fingerprint", fp, "--json"));
  unchanged(known, before);
  free(old);
  free(fp);
}
static void other_type_ambiguous(void) {
  char known[F_PATH], config[F_PATH], other[F_PATH], other_pub[F_PATH], *pub, *before, *fp = fingerprint(host_pub);
  FILE *f;
  nt_path(known, root, "known_ambiguous");
  nt_path(other, root, "other_ecdsa");
  nt_path(other_pub, root, "other_ecdsa.pub");
  keygen(other, "ecdsa");
  pub = f_read(other_pub, 10000);
  assert(pub && (f = fopen(known, "w")) && fprintf(f, "[127.0.0.1]:%u %s", port, pub) > 0 && !fclose(f));
  free(pub);
  client_config("config_ambiguous", known, "", config);
  before = snapshot(known);
  json_object_put(REMOTE(1, "host_key_ambiguous", "setup", "s3", "fixture", "--ssh-config", config, "--json"));
  json_object_put(REMOTE(1, "host_key_ambiguous", "trust-key", "s3", "--fingerprint", fp, "--json"));
  unchanged(known, before);
  free(fp);
}
static void hashed_line(void) {
  char known[F_PATH], config[F_PATH], *text, *fp = fingerprint(host_pub);
  nt_path(known, root, "known_hashed");
  client_config("config_hashed", known, " HashKnownHosts yes\n", config);
  json_object_put(REMOTE(3, "approval_required", "setup", "s4", "fixture", "--ssh-config", config, "--json"));
  json_object_put(REMOTE(0, NULL, "trust-key", "s4", "--fingerprint", fp, "--json"));
  text = f_read(known, 1 << 20);
  assert(text && !strncmp(text, "|1|", 3) && !strstr(text, "127.0.0.1"));
  free(text);
  strict_ssh_ok(config);
  free(fp);
}
static void unwritable_refused(void) {
  char known[F_PATH], config[F_PATH], *before, *fp = fingerprint(host_pub);
  const unsigned modes[] = {0444, 0620};
  size_t i;
  nt_path(known, root, "known_readonly");
  nt_write(known, "other.example ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIHydraFixtureKeyNotRealAAAAAAAAAAAAAAAAAAAA\n");
  client_config("config_readonly", known, "", config);
  for (i = 0; i < 2; i++) {
    assert(!chmod(known, modes[i]));
    before = snapshot(known);
    json_object_put(REMOTE(1, "known_hosts_unwritable", "setup", "s5", "fixture", "--ssh-config", config, "--json"));
    json_object_put(REMOTE(1, "known_hosts_unwritable", "trust-key", "s5", "--fingerprint", fp, "--json"));
    unchanged(known, before);
  }
  free(fp);
}
/* CI without --json never prompts: human output names the exact next command. */
static void ci_human_output(void) {
  char known[F_PATH], config[F_PATH], expected[512], *fp = fingerprint(host_pub), *before;
  struct f_capture c;
  nt_path(known, root, "known_ci");
  client_config("config_ci", known, "", config);
  before = snapshot(known);
  assert(!setenv("CI", "1", 1));
  c = remote((const char *[]){"setup", "s6", "fixture", "--ssh-config", config, NULL});
  assert(!unsetenv("CI"));
  assert(snprintf(expected, sizeof expected, "next: hydra remote trust-key s6 --fingerprint %s\n", fp) < (int)sizeof expected);
  assert(c.status == 3 && !c.out[0] && strstr(c.err, expected) && strstr(c.err, "approval_required"));
  f_capture_free(&c);
  unchanged(known, before);
  free(fp);
}
static void setup_fixture(void) {
  char tmp[] = "/tmp/hydra-setup-ssh-XXXXXX", pub[F_PATH], authorized[F_PATH], home[F_PATH], *text;
  assert(mkdtemp(tmp) && realpath(tmp, root));
  nt_path(client_key, root, "client");
  nt_path(host_key, root, "host");
  nt_path(host_pub, root, "host.pub");
  nt_path(pub, root, "client.pub");
  nt_path(authorized, root, "authorized_keys");
  nt_path(home, root, "home");
  keygen(client_key, "ed25519");
  keygen(host_key, "ed25519");
  text = f_read(pub, 10000);
  assert(text);
  nt_write(authorized, text);
  free(text);
  port = free_port();
  start_server();
  assert(!setenv("HYDRA_HOME", home, 1) && !unsetenv("CI") && !unsetenv("HYDRA_NONINTERACTIVE"));
}
static bool available(void) {
  char *find[] = {"sh", "-c", "command -v ssh-keygen && command -v ssh", NULL};
  struct f_capture c;
  if (access("/usr/sbin/sshd", X_OK))
    return false;
  c = nt_run(find);
  f_capture_free(&c);
  return !c.status;
}
int main(void) {
  char origin[F_PATH], known[F_PATH], config[F_PATH], *fp;
  const char *bin = getenv("HYDRA_FLEET_BIN");
  if (!available()) {
    puts("SKIP remote setup SSH: OpenSSH server or client unavailable");
    return 0;
  }
  assert(getcwd(origin, sizeof origin));
  if (bin)
    assert(!f_copy(fleet, sizeof fleet, bin));
  else
    nt_path(fleet, origin, "build/hydra-fleet");
  assert(!atexit(stop_server));
  setup_fixture();
  nt_path(known, root, "known_hosts");
  client_config("ssh_config", known, "", config);
  fp = fingerprint(host_pub);
  approval_required_first(config, known, fp);
  trusted_then_known(config, known, fp);
  preflight_over_sshd();
  control_persist_ignored(known);
  changed_key_refused(known);
  other_type_ambiguous();
  hashed_line();
  unwritable_refused();
  ci_human_output();
  free(fp);
  stop_server();
  assert(!f_remove_tree(root));
  puts("Remote setup host key trust over loopback SSH passed");
  return 0;
}
