#include "support.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
static char root[F_PATH], origin[F_PATH], fleet[F_PATH], hydra[F_PATH],
    config[F_PATH], host_pub[F_PATH], counter[F_PATH], project[F_PATH],
    loopback[F_PATH], count_fixture[F_PATH];
static char *original_path;
static struct nt_child daemon_child;
static void stop_server(void) {
  if (daemon_child.pid > 0) {
    kill(daemon_child.pid, SIGTERM);
    struct f_capture c = nt_wait(&daemon_child, 5);
    if (c.status != 0 && c.status != 143)
      fprintf(stderr, "sshd: %s\n", c.err);
    f_capture_free(&c);
  }
}
static void setup(void) {
  char tmp[] = "/tmp/hydra-native-loopback-XXXXXX", client[F_PATH],
       host[F_PATH], authorized[F_PATH], known[F_PATH], force[F_PATH],
       server_config[F_PATH], transport[F_PATH], inject[F_PATH], home[F_PATH],
       path[16384];
  assert(mkdtemp(tmp));
  assert(!f_copy(root, sizeof root, tmp));
  nt_path(project, root, "project");
  nt_path(counter, root, "mutations");
  nt_path(client, root, "client");
  nt_path(host, root, "host");
  nt_path(host_pub, root, "host.pub");
  nt_path(authorized, root, "authorized_keys");
  nt_path(known, root, "known_hosts");
  nt_path(config, root, "ssh_config");
  nt_path(force, root, "receiver");
  nt_path(server_config, root, "sshd_config");
  char *git[] = {"git", "init", "-q", project, NULL};
  struct f_capture c = nt_run(git);
  assert(!c.status);
  f_capture_free(&c);
  const char *keys[] = {client, host};
  for (size_t i = 0; i < 2; i++) {
    char *args[] = {"ssh-keygen",    "-q", "-t", "ed25519", "-N", "", "-f",
                    (char *)keys[i], NULL};
    c = nt_run(args);
    assert(!c.status);
    f_capture_free(&c);
  }
  char pub[F_PATH];
  nt_path(pub, root, "client.pub");
  char *s = f_read(pub, 10000);
  assert(s);
  nt_write(authorized, s);
  free(s);
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(!bind(listener, (struct sockaddr *)&address, sizeof address));
  socklen_t len = sizeof address;
  assert(!getsockname(listener, (struct sockaddr *)&address, &len));
  unsigned port = ntohs(address.sin_port);
  assert(!close(listener));
  s = f_read(host_pub, 10000);
  assert(s);
  FILE *f = fopen(known, "w");
  assert(f);
  assert(fprintf(f, "[127.0.0.1]:%u %s", port, s) > 0 && !fclose(f));
  free(s);
  struct passwd *pw = getpwuid(getuid());
  assert(pw && pw->pw_name);
  f = fopen(config, "w");
  assert(f);
  assert(
      fprintf(f,
              "Host fixture\n HostName 127.0.0.1\n Port %u\n User %s\n "
              "IdentityFile %s\n IdentitiesOnly yes\n UserKnownHostsFile %s\n "
              "GlobalKnownHostsFile /dev/null\n StrictHostKeyChecking yes\n",
              port, pw->pw_name, client, known) > 0 &&
      !fclose(f));
  f = fopen(force, "w");
  assert(f);
  const char *values[] = {loopback, root,          fleet,
                          hydra,    count_fixture, original_path};
  assert(fputs("#!/bin/sh\nexec ", f) >= 0);
  for (size_t i = 0; i < 6; i++) {
    char *q = f_quote(values[i]);
    assert(q);
    assert(fprintf(f, "%s%s ", i == 1 ? "--force " : "", q) > 0);
    free(q);
  }
  assert(fputc('\n', f) != EOF && !fclose(f) && !chmod(force, 0700));
  f = fopen(server_config, "w");
  assert(f);
  assert(fprintf(f,
                 "Port %u\nListenAddress 127.0.0.1\nHostKey %s\nPidFile "
                 "%s/pid\nAuthorizedKeysFile %s\nStrictModes no\nUsePAM "
                 "no\nPasswordAuthentication no\nKbdInteractiveAuthentication "
                 "no\nAllowUsers %s\nForceCommand %s\nLogLevel ERROR\n",
                 port, host, root, authorized, pw->pw_name, force) > 0 &&
         !fclose(f));
  char *args[] = {"/usr/sbin/sshd", "-D", "-e", "-f", server_config, NULL};
  daemon_child = nt_start(args, NULL);
  bool ready = false;
  for (size_t i = 0; i < 250; i++) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (!connect(fd, (struct sockaddr *)&address, sizeof address))
      ready = true;
    assert(!close(fd));
    if (ready)
      break;
    struct timespec pause = {0, 20000000};
    nanosleep(&pause, NULL);
  }
  if (!ready) {
    stop_server();
    fprintf(stderr, "ephemeral SSH server did not become ready\n");
    abort();
  }
  nt_path(transport, root, "transport");
  assert(!f_mkdirs(transport));
  nt_path(inject, transport, "ssh");
  assert(!symlink(loopback, inject));
  nt_path(home, root, "client-home");
  int n = snprintf(path, sizeof path, "%s:%s", transport, original_path);
  assert(n > 0 && (size_t)n < sizeof path);
  assert(!setenv("PATH", path, 1) && !setenv("HYDRA_HOME", home, 1) &&
         !setenv("HYDRA_FLEET_BIN", fleet, 1) &&
         !unsetenv("ENROLL_BREAK_MASTER"));
}
static void cleanup(void) {
  stop_server();
  assert(!setenv("PATH", original_path, 1));
  assert(!f_remove_tree(root));
}
static json_object *call(const char *const extra[]) {
  char *args[48];
  args[0] = hydra;
  args[1] = "fleet";
  size_t i = 0;
  while (extra[i]) {
    assert(i < 45);
    args[i + 2] = (char *)extra[i];
    i++;
  }
  args[i + 2] = NULL;
  struct f_capture c = nt_run(args);
  if (c.status)
    fprintf(stderr, "%s\n%s", c.out, c.err);
  json_object *v = nt_output(&c, 0);
  f_capture_free(&c);
  return v;
}
#define LC(...) call((const char *[]){__VA_ARGS__, NULL})
static void status_is(json_object *v, const char *state) {
  if (strcmp(f_string(json_object_array_get_idx(
                          f_field(f_field(v, "data"), "hosts"), 0),
                      "status"),
             state))
    fprintf(stderr, "%s\n", json_object_to_json_string(v));
  assert(!strcmp(f_string(json_object_array_get_idx(
                              f_field(f_field(v, "data"), "hosts"), 0),
                          "status"),
                 state));
  json_object_put(v);
}
static void pinned(void) {
  json_object *qualification = LC("qualify", "--ssh", "fixture", "--ssh-config",
                                  config, "--require", "list"),
              *row = json_object_array_get_idx(
                  f_field(f_field(qualification, "data"), "candidates"), 0);
  assert(!strcmp(f_string(row, "status"), "compatible"));
  char *args[] = {"ssh-keygen", "-lf", host_pub, NULL};
  struct f_capture c = nt_run(args);
  assert(!c.status);
  char *save;
  assert(strtok_r(c.out, " \t", &save));
  char *key = strtok_r(NULL, " \t", &save);
  assert(key && !strcmp(f_string(f_field(f_field(row, "qualification"), "data"),
                                 "peer_fingerprint"),
                        key));
  char q[F_PATH], package[F_PATH], prefix[F_PATH], intent[F_PATH];
  nt_path(q, root, "qualification.json");
  nt_path(package, root, "package");
  nt_path(prefix, root, "exact-prefix");
  nt_path(intent, root, "intent.json");
  nt_json_write(q, qualification);
  json_object *packaged =
      LC("package", "--source", origin, "--binary", fleet, "--output", package);
  json_object *review =
      LC("enroll", "review", "--input", q, "--candidate",
         f_string(row, "candidate_id"), "--project", project, "--package",
         package, "--sha256", f_string(f_field(packaged, "data"), "sha256"),
         "--prefix", prefix, "--output", intent);
  const char *digest = f_string(f_field(review, "data"), "intent_sha256");
  status_is(LC("enroll", "apply", "--input", intent, "--confirm", digest),
            "enrolled");
  char binary[F_PATH], a[65], b[65];
  nt_path(binary, prefix, "libexec/hydra/hydra-fleet");
  assert(!f_hash(binary, a) && !f_hash(fleet, b) && !strcmp(a, b));
  char alias[F_PATH], relative[256];
  assert(snprintf(relative, sizeof relative,
                  "client-home/fleet/remotes/%s.json",
                  f_string(row, "candidate_id")) > 0);
  nt_path(alias, root, relative);
  json_object *record = f_read_json(alias, 1000000);
  assert(record && !strcmp(f_string(record, "accepted_host_key"), key) &&
         !strcmp(f_string(record, "project"), project));
  nt_path(binary, prefix, "bin/hydra");
  assert(!strcmp(f_string(record, "hydra"), binary));
  struct passwd *pw = getpwuid(getuid());
  assert(pw && !strcmp(f_string(record, "principal"), pw->pw_name));
  json_object_put(record);
  status_is(LC("enroll", "apply", "--input", intent, "--confirm", digest),
            "enrolled");
  char *count = f_read(counter, 1000);
  assert(count && !strcmp(count, "init\n"));
  free(count);
  json_object_put(review);
  json_object_put(packaged);
  json_object_put(qualification);
  f_capture_free(&c);
}
static void lost_master(void) {
  json_object *qualification = LC("qualify", "--ssh", "fixture", "--ssh-config",
                                  config, "--require", "list"),
              *row = json_object_array_get_idx(
                  f_field(f_field(qualification, "data"), "candidates"), 0);
  assert(!strcmp(f_string(row, "status"), "compatible"));
  char q[F_PATH], intent[F_PATH];
  nt_path(q, root, "qualification.json");
  nt_path(intent, root, "intent.json");
  nt_json_write(q, qualification);
  json_object *review = LC("enroll", "review", "--input", q, "--candidate",
                           f_string(row, "candidate_id"), "--project", project,
                           "--output", intent);
  const char *digest = f_string(f_field(review, "data"), "intent_sha256");
  assert(!setenv("ENROLL_BREAK_MASTER", "1", 1));
  status_is(LC("enroll", "apply", "--input", intent, "--confirm", digest),
            "outcome_unknown");
  assert(access(counter, F_OK));
  char relative[256], alias[F_PATH];
  assert(snprintf(relative, sizeof relative,
                  "client-home/fleet/remotes/%s.json",
                  f_string(row, "candidate_id")) > 0);
  nt_path(alias, root, relative);
  assert(access(alias, F_OK));
  assert(!unsetenv("ENROLL_BREAK_MASTER"));
  status_is(LC("enroll", "apply", "--input", intent, "--confirm", digest),
            "enrolled");
  char *count = f_read(counter, 1000);
  assert(count && !strcmp(count, "init\n"));
  free(count);
  json_object_put(review);
  json_object_put(qualification);
}
int main(int argc, char **argv) {
  if (access("/usr/sbin/sshd", X_OK)) {
    puts("SKIP loopback SSH: OpenSSH server unavailable");
    return 0;
  }
  char *find[] = {"sh", "-c", "command -v ssh-keygen", NULL};
  struct f_capture c = nt_run(find);
  if (c.status) {
    f_capture_free(&c);
    puts("SKIP loopback SSH: ssh-keygen unavailable");
    return 0;
  }
  f_capture_free(&c);
  assert(getcwd(origin, sizeof origin));
  nt_path(hydra, origin, "bin/hydra");
  const char *f = getenv("HYDRA_FLEET_BIN");
  if (f)
    assert(!f_copy(fleet, sizeof fleet, f));
  else
    nt_path(fleet, origin, "build/hydra-fleet");
  if (argc > 1)
    assert(!f_copy(loopback, sizeof loopback, argv[1]));
  else
    nt_path(loopback, origin, "build/native-tests/enrollment-loopback-fixture");
  if (argc > 2)
    assert(!f_copy(count_fixture, sizeof count_fixture, argv[2]));
  else
    nt_path(count_fixture, origin,
            "build/native-tests/enrollment-receiver-fixture");
  const char *p = getenv("PATH");
  assert(p);
  original_path = strdup(p);
  assert(original_path);
  assert(!atexit(stop_server));
  setup();
  pinned();
  cleanup();
  puts("Strict loopback pinned install/init/duplicate passed");
  setup();
  lost_master();
  cleanup();
  puts("Strict loopback lost authenticated master refused fallback and "
       "reconciled");
  free(original_path);
  return 0;
}
