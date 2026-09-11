#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Independent durable-record arithmetic: never use the metric implementation
 * being checked to derive expected values. No JSON-C dependency is needed. */
static void path(char *out, size_t cap, const char *a, const char *b) {
  int n = snprintf(out, cap, "%s/%s", a, b);
  assert(n >= 0 && (size_t)n < cap);
}
static bool regular(const char *p) {
  struct stat st;
  return !lstat(p, &st) && S_ISREG(st.st_mode);
}
static void scalar(const char *p, char *out, size_t cap) {
  FILE *f;
  size_t n;
  assert(cap >= 2);
  out[0] = '-';
  out[1] = 0;
  if (!regular(p))
    return;
  f = fopen(p, "r");
  assert(f);
  n = fread(out, 1, cap - 1, f);
  assert(!ferror(f) && feof(f));
  assert(!fclose(f));
  out[n] = 0;
  while (n && isspace((unsigned char)out[n - 1]))
    out[--n] = 0;
  n = strspn(out, " \t\r\n");
  memmove(out, out + n, strlen(out + n) + 1);
}
static long long decimal(const char *s) {
  char *end;
  long long n;
  if (!*s)
    return -1;
  for (const char *p = s; *p; p++)
    if (!isdigit((unsigned char)*p))
      return -1;
  errno = 0;
  n = strtoll(s, &end, 10);
  assert(!errno && !*end);
  return n;
}
static long long number(const char *base, const char *name) {
  char p[4096], value[128];
  path(p, sizeof p, base, name);
  scalar(p, value, sizeof value);
  return decimal(value);
}
static void execute(char *const argv[], int fd) {
  pid_t pid = fork();
  int status;
  assert(pid >= 0);
  if (!pid) {
    assert(dup2(fd, STDOUT_FILENO) >= 0);
    close(fd);
    execv(argv[0], argv);
    perror(argv[0]);
    _exit(127);
  }
  while (waitpid(pid, &status, 0) < 0)
    assert(errno == EINTR);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main(int argc, char **argv) {
  char p[4096], value[256], other[256], build[4096], binary[4096], root[4096];
  char feed[] = "/tmp/hydra-statistics-feed-XXXXXX",
       observed[] = "/tmp/hydra-statistics-observed-XXXXXX";
  long long expected[4][4] = {
      {0, 0, 0, 0}, {1, 1, 0, 0}, {2, 0, 0, 0}, {3, 1, 0, 0}};
  long long start, end, recoveries;
  FILE *f;
  char *line = NULL;
  size_t cap = 0;
  int fd, outfd;
  const char *run, *base;
  bool verified;
  assert(argc == 5);
  run = argv[2];
  verified = !strcmp(argv[4], "verified");
  path(p, sizeof p, run, "recovery-count");
  scalar(p, value, sizeof value);
  assert(!strcmp(value, argv[3]));
  start = number(run, "started-at");
  end = number(run, "completed-at");
  assert(end > 0 && (start < 0 || end >= start));
  path(p, sizeof p, run, "graph.tsv");
  f = fopen(p, "r");
  assert(f);
  while (getline(&line, &cap, f) >= 0) {
    char *save, *kind = strtok_r(line, "\t", &save),
                *id = strtok_r(NULL, "\t", &save),
                *type = strtok_r(NULL, "\t\r\n", &save);
    char step[4096], steps[4096];
    long long ready, first;
    if (!kind || strcmp(kind, "step"))
      continue;
    assert(id && type);
    if (!strcmp(type, "approval-wait"))
      continue;
    path(steps, sizeof steps, run, "steps");
    path(step, sizeof step, steps, id);
    if (number(step, "attempts") == 0)
      continue;
    expected[0][1]++;
    ready = number(step, "initial-ready-at");
    first = number(step, "initial-started-at");
    if (ready >= 0 && first >= 0) {
      assert(first >= ready);
      assert(LLONG_MAX - expected[0][3] >= first - ready);
      expected[0][2]++;
      expected[0][3] += first - ready;
    }
  }
  assert(!ferror(f));
  fclose(f);
  free(line);
  expected[1][2] = start >= 0;
  expected[1][3] = start >= 0 ? end - start : 0;
  if (verified) {
    long long passed = number(run, "verified-at");
    assert(start >= 0 && passed >= start && passed <= end);
    path(p, sizeof p, run, "verification-plan-sha256");
    scalar(p, value, sizeof value);
    path(p, sizeof p, run, "plan-accepted");
    scalar(p, other, sizeof other);
    assert(!strcmp(value, other));
    expected[2][1] = expected[2][2] = 1;
    expected[2][3] = passed - start;
  } else {
    struct stat st;
    path(p, sizeof p, run, "verified-at");
    assert(lstat(p, &st) != 0 && errno == ENOENT);
    path(p, sizeof p, run, "compiled.json");
    expected[2][1] = !stat(p, &st);
  }
  recoveries = decimal(argv[3]);
  expected[3][2] = recoveries >= 0;
  expected[3][3] = recoveries >= 0 ? recoveries : 0;
  assert(strlen(argv[1]) < sizeof root);
  memcpy(root, argv[1], strlen(argv[1]) + 1);
  char *slash = strrchr(root, '/');
  assert(slash);
  *slash = 0;
  slash = strrchr(root, '/');
  assert(slash);
  *slash = 0;
  base = getenv("BUILD_DIR");
  if (!base || !*base)
    base = "build";
  if (*base == '/') {
    assert(strlen(base) < sizeof build);
    memcpy(build, base, strlen(base) + 1);
  } else
    path(build, sizeof build, root, base);
  path(binary, sizeof binary, build, "test-statistics");
  if (!regular(binary)) {
    puts("SKIP native aggregate comparison: build test-statistics; durable "
         "boundaries passed");
    return 0;
  }
  fd = mkstemp(feed);
  outfd = mkstemp(observed);
  assert(fd >= 0 && outfd >= 0);
  {
    char *args[] = {argv[1], "workflow", "statistics-data", NULL};
    execute(args, fd);
  }
  close(fd);
  base = strrchr(run, '/');
  base = base ? base + 1 : run;
  {
    char *args[] = {binary, feed, (char *)base, NULL};
    execute(args, outfd);
  }
  close(outfd);
  f = fopen(observed, "r");
  assert(f);
  for (size_t i = 0; i < 4; i++)
    for (size_t j = 0; j < 4; j++) {
      long long actual;
      assert(fscanf(f, "%lld", &actual) == 1);
      if (actual != expected[i][j])
        fprintf(stderr, "metric %zu field %zu: %lld != %lld\n", i, j, actual,
                expected[i][j]);
      assert(actual == expected[i][j]);
    }
  int ch;
  do {
    ch = fgetc(f);
  } while (ch != EOF && isspace((unsigned char)ch));
  assert(ch == EOF && !ferror(f));
  fclose(f);
  assert(!unlink(feed) && !unlink(observed));
  printf("PASS durable-to-native metric reconciliation: %s\n", base);
  return 0;
}
