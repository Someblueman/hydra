#include "support.h"
#include <signal.h>
#include <time.h>
static void require(bool condition) {
  if (!condition) {
    const char *p = getenv("HD_ERRORS");
    FILE *f = p ? fopen(p, "a") : NULL;
    if (f) {
      fputs("SSH fixture boundary assertion failed\n", f);
      fclose(f);
    }
    abort();
  }
}
static json_object *effective(const char *config, const char *target) {
  char *args[] = {(char *)getenv("HD_REAL_SSH"),
                  "-G",
                  "-F",
                  (char *)config,
                  (char *)target,
                  NULL};
  struct f_capture c = nt_run(args);
  require(!c.status);
  json_object *v = json_object_new_object();
  char *save, *row = strtok_r(c.out, "\n", &save);
  while (row) {
    char *space = strchr(row, ' ');
    require(space != NULL);
    *space++ = 0;
    f_string_add(v, row, space);
    row = strtok_r(NULL, "\n", &save);
  }
  f_capture_free(&c);
  return v;
}
static bool option(int argc, char **argv, const char *value) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], value))
      return true;
  return false;
}
static void setting(json_object *v, const char *key, const char *a,
                    const char *b) {
  const char *s = f_string(v, key);
  require(s != NULL);
  require(!strcmp(s, a) || (b && !strcmp(s, b)));
}
int main(int argc, char **argv) {
  const char *config = NULL, *target, *command;
  for (int i = 1; i + 1 < argc; i++)
    if (!strcmp(argv[i], "-F"))
      config = argv[i + 1];
  require(config != NULL);
  struct stat st;
  require(!stat(config, &st) && (st.st_mode & 0777) == 0600);
  if (option(argc, argv, "-G")) {
    target = argv[argc - 1];
    if (!strcmp(target, "badconfig"))
      return 1;
    argv[0] = "ssh";
    const char *real_ssh = getenv("HD_REAL_SSH");
    require(real_ssh != NULL);
    // The test runner supplies the selected local ssh executable; argv is not
    // shell text. NOLINTNEXTLINE(clang-analyzer-optin.taint.GenericTaint)
    execv(real_ssh, argv);
    return 127;
  }
  require(argc >= 3 && !strcmp(argv[1], "-T"));
  require(option(argc, argv, "BatchMode=yes") &&
          option(argc, argv, "StrictHostKeyChecking=yes"));
  target = argv[argc - 2];
  command = argv[argc - 1];
  require(!strcmp(command, "env LC_ALL=C  'hydra' fleet serve"));
  json_object *settings = effective(config, target);
  setting(settings, "batchmode", "yes", NULL);
  setting(settings, "stricthostkeychecking", "true", "yes");
  setting(settings, "updatehostkeys", "false", "no");
  const char *control = f_string(settings, "controlpath");
  require(!control || !*control || !strcmp(control, "none"));
  setting(settings, "permitlocalcommand", "no", NULL);
  setting(settings, "clearallforwardings", "yes", NULL);
  if (!strcmp(target, "good")) {
    setting(settings, "proxyjump", "jump", NULL);
    json_object *jump = effective(config, "jump");
    setting(jump, "batchmode", "yes", NULL);
    setting(jump, "stricthostkeychecking", "true", "yes");
    setting(jump, "updatehostkeys", "false", "no");
    json_object_put(jump);
  }
  json_object_put(settings);
  char input[4096];
  size_t n = fread(input, 1, sizeof input - 1, stdin);
  require(feof(stdin) && !ferror(stdin));
  input[n] = 0;
  json_object *request = f_parse(input);
  require(request != NULL);
  json_object *expected = f_parse("{\"protocol\":1,\"action\":\"handshake\"}");
  require(json_object_equal(request, expected));
  json_object_put(expected);
  json_object *record = json_object_new_object();
  f_string_add(record, "target", target);
  f_string_add(record, "config", config);
  const char *calls_path = getenv("HD_CALLS");
  require(calls_path != NULL);
  FILE *calls = fopen(calls_path, "a");
  require(calls != NULL);
  require(fprintf(calls, "%s\n",
                  json_object_to_json_string_ext(record,
                                                 JSON_C_TO_STRING_PLAIN)) > 0 &&
          !fclose(calls));
  json_object_put(record);
  const char *hosts[] = {"unknown", "changed", "keygeneric", "auth", "offline"},
             *errors[] = {
                 "No ED25519 host key is known for fixture and you have "
                 "requested strict checking.\nHost key verification failed.",
                 "REMOTE HOST IDENTIFICATION HAS CHANGED!\nHost key "
                 "verification failed.",
                 "Host key verification failed.",
                 "Permission denied (publickey).", "Connection refused"};
  for (size_t i = 0; i < 5; i++)
    if (!strcmp(target, hosts[i])) {
      fprintf(stderr, "%s secret-test-token\n", errors[i]);
      json_object_put(request);
      return 255;
    }
  if (!strcmp(target, "slow")) {
    char pid[64];
    require(snprintf(pid, sizeof pid, "%ld", (long)getpid()) > 0);
    nt_write(getenv("HD_PID"), pid);
    struct timespec pause = {30, 0};
    nanosleep(&pause, NULL);
    json_object_put(request);
    return 0;
  }
  if (!strcmp(target, "malformed")) {
    puts("{broken");
    json_object_put(request);
    return 0;
  }
  if (!strcmp(target, "oversized")) {
    signal(SIGPIPE, SIG_IGN);
    for (size_t i = 0; i < 8 * 1024 * 1024 + 1; i++)
      if (fputc('x', stdout) == EOF) {
        json_object_put(request);
        return 0;
      }
    fflush(stdout);
    json_object_put(request);
    return 0;
  }
  char *args[] = {(char *)getenv("HYDRA_FLEET_BIN"), "fleet", "serve", NULL};
  struct f_capture c = {0};
  require(!f_run(args, input, n, 20, &c) && !c.status);
  json_object *response = f_parse(c.out);
  require(response != NULL);
  if (!strcmp(target, "skew"))
    json_object_object_add(f_field(response, "data"), "fleet_protocol",
                           json_object_new_int(99));
  if (!strcmp(target, "nocap"))
    json_object_object_add(f_field(response, "data"), "capabilities",
                           json_object_new_array());
  puts(json_object_to_json_string_ext(response, JSON_C_TO_STRING_PLAIN));
  json_object_put(response);
  json_object_put(request);
  f_capture_free(&c);
  return 0;
}
