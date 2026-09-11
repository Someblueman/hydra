#include "support.h"
#include <time.h>
static bool has(int argc, char **argv, const char *s) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], s))
      return true;
  return false;
}
static void append(const char *p, const char *text) {
  assert(p && text);
  FILE *f = fopen(p, "a");
  assert(f);
  assert(fputs(text, f) >= 0 && !fclose(f));
}
static void hold(const char *var) {
  const char *p = getenv(var);
  if (!p)
    return;
  char started[F_PATH];
  int n = snprintf(started, sizeof started, "%s.started", p);
  assert(n > 0 && (size_t)n < sizeof started);
  nt_write(started, "");
  while (access(p, F_OK)) {
    struct timespec pause = {0, 10000000};
    nanosleep(&pause, NULL);
  }
}
int main(int argc, char **argv) {
  if (has(argc, argv, "-G")) {
    argv[0] = "ssh";
    execv("/usr/bin/ssh", argv);
    return 127;
  }
  if (has(argc, argv, "-O"))
    return 0;
  assert(argc >= 3);
  const char *target = argv[argc - 2], *command = argv[argc - 1];
  assert(target && command);
  const char *pol = getenv("ENROLL_HOST_POLICY");
  json_object *policies = f_parse(pol ? pol : "{}"),
              *policy = f_field(policies, target);
  if (json_object_get_boolean(f_field(policy, "offline"))) {
    json_object_put(policies);
    return 255;
  }
  const char *fingerprint = f_string(policy, "fingerprint");
  if (!fingerprint)
    fingerprint = getenv("ENROLL_FINGERPRINT");
  assert(fingerprint);
  char diagnostic[1024];
  assert(snprintf(diagnostic, sizeof diagnostic,
                  "debug1: Server host key: ssh-ed25519 %s\n",
                  fingerprint) > 0);
  const char *log = NULL;
  for (int i = 1; i + 1 < argc; i++)
    if (!strcmp(argv[i], "-E"))
      log = argv[i + 1];
  if (log)
    append(log, diagnostic);
  else
    fputs(diagnostic, stderr);
  char receiver[F_PATH];
  nt_path(receiver, getenv("ENROLL_RECEIVERS"), target);
  assert(!setenv("HYDRA_HOME", receiver, 1) &&
         !setenv("HYDRA_BIN_CMD", getenv("ENROLL_COUNT_WRAPPER"), 1));
  size_t used = 0, cap = 65536;
  char *input = malloc(cap);
  assert(input);
  for (;;) {
    if (used == cap) {
      assert(cap < 64 * 1024 * 1024);
      cap *= 2;
      char *grown = realloc(input, cap);
      assert(grown);
      input = grown;
    }
    size_t n = fread(input + used, 1, cap - used, stdin);
    used += n;
    if (!n) {
      assert(feof(stdin) && !ferror(stdin));
      break;
    }
  }
  if (strstr(command, " install '") || strstr(command, " install-check '")) {
    bool installing = strstr(command, " install '") != NULL;
    char *args[] = {"/bin/sh", "-c", (char *)command, NULL};
    struct f_capture c = {0};
    assert(!f_run(args, input, used, 120, &c));
    if (installing)
      append(getenv("ENROLL_INSTALL_COUNTER"), "install\n");
    const char *drop = getenv("ENROLL_DROP_INSTALL_RESPONSE");
    if (installing && drop && access(drop, F_OK)) {
      nt_write(drop, "");
      f_capture_free(&c);
      free(input);
      json_object_put(policies);
      return 255;
    }
    assert(fwrite(c.out, 1, c.out_bytes, stdout) == c.out_bytes &&
           fwrite(c.err, 1, c.err_bytes, stderr) == c.err_bytes);
    int status = c.status;
    f_capture_free(&c);
    free(input);
    json_object_put(policies);
    return status;
  }
  if (!strncmp(command, "git -C ", 7)) {
    char *args[] = {"/bin/sh", "-c", (char *)command, NULL};
    struct f_capture c = nt_run(args);
    fputs(c.out, stdout);
    int status = c.status;
    f_capture_free(&c);
    free(input);
    json_object_put(policies);
    return status;
  }
  char *s = realloc(input, used + 1);
  assert(s);
  input = s;
  input[used] = 0;
  json_object *request = f_parse(input);
  assert(request);
  const char *action = f_string(request, "action");
  assert(action);
  if (!strcmp(action, "enrollment-preflight"))
    hold("ENROLL_HOLD_PREFLIGHT");
  if (!strcmp(action, "handshake"))
    hold("ENROLL_HOLD_QUALIFY");
  if (json_object_get_boolean(f_field(policy, "bad_project")) &&
      !strcmp(action, "enrollment-preflight"))
    f_string_add(request, "project", "/nonexistent/hydra-enrollment-fixture");
  char *args[] = {(char *)getenv("HYDRA_FLEET_BIN"), "fleet", "serve", NULL};
  struct f_capture c = {0};
  const char *text =
      json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
  assert(!f_run(args, text, strlen(text), 120, &c));
  const char *drop = getenv("ENROLL_DROP_INIT_RESPONSE");
  if (!strcmp(action, "init") && drop && access(drop, F_OK)) {
    nt_write(drop, "");
    f_capture_free(&c);
    free(input);
    json_object_put(request);
    json_object_put(policies);
    return 255;
  }
  if (getenv("ENROLL_STDOUT_MARKER") && !strcmp(action, "handshake"))
    puts("Server host key: ssh-ed25519 SHA256:fixture");
  json_object *out = f_parse(c.out);
  if (!out)
    fprintf(stderr, "%s\n%s", c.out, c.err);
  assert(out);
  const char *old = getenv("ENROLL_OLD_RECEIVER_PREFIX");
  if (!strcmp(action, "handshake") &&
      (json_object_get_boolean(f_field(policy, "missing_capability")) ||
       (old && access(old, F_OK)))) {
    json_object *data = f_field(out, "data"),
                *caps = f_field(data, "capabilities"),
                *filtered = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(caps); i++) {
      const char *name =
          json_object_get_string(json_object_array_get_idx(caps, i));
      if (json_object_get_boolean(f_field(policy, "missing_capability")) &&
          !strcmp(name, "list"))
        continue;
      if (old && access(old, F_OK) && !strncmp(name, "enrollment-", 11))
        continue;
      json_object_array_add(filtered, json_object_new_string(name));
    }
    json_object_object_add(data, "capabilities", filtered);
  }
  puts(json_object_to_json_string_ext(out, JSON_C_TO_STRING_PLAIN));
  int status = c.status;
  json_object_put(out);
  json_object_put(request);
  json_object_put(policies);
  free(input);
  f_capture_free(&c);
  return status;
}
