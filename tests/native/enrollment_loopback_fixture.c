#include "support.h"
static char *input_bytes(size_t *size) {
  size_t cap = 65536, n = 0;
  char *s = malloc(cap);
  assert(s);
  for (;;) {
    if (n == cap) {
      assert(cap < 64 * 1024 * 1024);
      cap *= 2;
      char *v = realloc(s, cap);
      assert(v);
      s = v;
    }
    size_t got = fread(s + n, 1, cap - n, stdin);
    n += got;
    if (!got) {
      assert(feof(stdin) && !ferror(stdin));
      break;
    }
  }
  char *v = realloc(s, n + 1);
  assert(v);
  v[n] = 0;
  *size = n;
  return v;
}
static int forward(char *const args[], const char *input, size_t size,
                   const char *log) {
  struct f_capture c = {0};
  assert(!f_run(args, input, size, 120, &c));
  assert(fwrite(c.out, 1, c.out_bytes, stdout) == c.out_bytes &&
         fwrite(c.err, 1, c.err_bytes, stderr) == c.err_bytes);
  if (log) {
    FILE *f = fopen(log, "wb");
    assert(f);
    assert(fwrite(c.err, 1, c.err_bytes, f) == c.err_bytes &&
           fwrite(c.out, 1, c.out_bytes, f) == c.out_bytes && !fclose(f));
  }
  int status = c.status;
  f_capture_free(&c);
  return status;
}
static bool suffix(const char *text, const char *ending) {
  size_t a = strlen(text), b = strlen(ending);
  return a >= b && !strcmp(text + a - b, ending);
}
static int force(int argc, char **argv) {
  assert(argc == 7);
  const char *root = argv[2], *fleet = argv[3], *hydra = argv[4],
             *counter = argv[5], *path = argv[6];
  const char *original = getenv("SSH_ORIGINAL_COMMAND");
  assert(original);
  char *command = strdup(original);
  assert(command);
  char home[F_PATH], counts[F_PATH], installed[F_PATH], binary[F_PATH],
      log[F_PATH];
  nt_path(home, root, "remote-home");
  nt_path(counts, root, "mutations");
  nt_path(installed, root, "exact-prefix/bin/hydra");
  assert(!setenv("HYDRA_HOME", home, 1) &&
         !setenv("HYDRA_BIN_CMD", counter, 1) &&
         !setenv("ENROLL_COUNTER", counts, 1) && !setenv("PATH", path, 1));
  if (suffix(command,
             " fleet serve")) { /* Match only the two exact executables this
                                   fixture provisions, not shell syntax. */
    char *quoted = f_quote(installed);
    assert(quoted);
    char expected[F_PATH + 32];
    int n = snprintf(expected, sizeof expected, "%s fleet serve", quoted);
    free(quoted);
    assert(n > 0 && (size_t)n < sizeof expected);
    const char *real;
    if (suffix(command, "'hydra' fleet serve")) {
      assert(!f_copy(binary, sizeof binary, fleet));
      real = hydra;
    } else {
      assert(suffix(command, expected));
      nt_path(binary, root, "exact-prefix/libexec/hydra/hydra-fleet");
      real = installed;
    }
    assert(!setenv("ENROLL_REAL", real, 1));
    char *args[] = {binary, "fleet", "serve",
                    NULL}; /* Binary was selected from the fixture's exact
                              source or installed prefix. */
    free(command);
    execv(binary, args);
    return 127;
  }
  size_t size;
  char *input = input_bytes(&size);
  char *args[] = {"/bin/sh", "-c", (char *)command, NULL};
  nt_path(log, root, "install.err");
  int status = forward(args, input, size, log);
  free(input);
  free(command);
  return status;
}
static int inject(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "-O")) {
      argv[0] = "ssh";
      execv("/usr/bin/ssh", argv);
      return 127;
    }
  size_t size;
  char *input = input_bytes(&size);
  json_object *request = size && input[0] == '{' ? f_parse(input) : NULL;
  const char *action = f_string(request, "action");
  if (getenv("ENROLL_BREAK_MASTER") && action && !strcmp(action, "init")) {
    const char *control = NULL, *config = NULL;
    for (int i = 1; i < argc; i++) {
      if (!strncmp(argv[i], "ControlPath=", 12))
        control = argv[i] + 12;
      if (!strcmp(argv[i], "-F") && i + 1 < argc)
        config = argv[i + 1];
    }
    assert(control && config && argc >= 3);
    char *close[] = {
        "/usr/bin/ssh", "-F",   (char *)config, "-S", (char *)control,
        "-O",           "exit", argv[argc - 2], NULL};
    struct f_capture c = nt_run(close);
    assert(!c.status);
    f_capture_free(&c);
  }
  argv[0] = "/usr/bin/ssh";
  int status = forward(argv, input, size, NULL);
  free(input);
  json_object_put(request);
  return status;
}
int main(int argc, char **argv) {
  return argc > 1 && !strcmp(argv[1], "--force") ? force(argc, argv)
                                                 : inject(argc, argv);
}
