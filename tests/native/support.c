#include "support.h"
const char *f_home, *f_hydra;
void nt_write(const char *path, const char *text) {
  assert(!f_write(path, text, strlen(text), true));
}
void nt_json_write(const char *path, json_object *value) {
  nt_write(path, json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN));
}
json_object *nt_clone(json_object *value) {
  json_object *out = NULL;
  assert(!json_object_deep_copy(value, &out, NULL));
  return out;
}
struct f_capture nt_run(char *const argv[]) {
  struct f_capture c = {0};
  assert(!f_run(argv, NULL, 0, 360, &c));
  assert(!c.timeout);
  return c;
}
json_object *nt_output(struct f_capture *c, int status) {
  json_object *v;
  if (c->status != status)
    fprintf(stderr, "exit %d expected %d: %s\n%s\n", c->status, status, c->out,
            c->err);
  assert(c->status == status);
  v = f_parse(c->out);
  assert(v);
  return v;
}
void nt_equal(json_object *actual, const char *expected) {
  json_object *v = f_parse_value(expected);
  assert(v || !strcmp(expected, "null"));
  if (!json_object_equal(actual, v))
    fprintf(stderr, "%s != %s\n", json_object_to_json_string(actual), expected);
  assert(json_object_equal(actual, v));
  json_object_put(v);
}
void nt_path(char *out, const char *base, const char *name) {
  assert(!f_path(out, F_PATH, base, name));
}
char *nt_replace(const char *text, const char *old, const char *replacement) {
  size_t count = 0, a = strlen(old), b = strlen(replacement), n = strlen(text);
  const char *p = text, *hit;
  char *out, *w;
  assert(a);
  while ((hit = strstr(p, old))) {
    count++;
    p = hit + a;
  }
  assert(count < 10000000 && n < 10000000 && b < 10000000);
  out = malloc(n + count * b + 1);
  assert(out);
  w = out;
  p = text;
  while ((hit = strstr(p, old))) {
    size_t len = (size_t)(hit - p);
    memcpy(w, p, len);
    w += len;
    memcpy(w, replacement, b);
    w += b;
    p = hit + a;
  }
  memcpy(w, p, strlen(p) + 1);
  return out;
}
void nt_copy_tree(const char *from, const char *to) {
  char *argv[] = {"cp", "-R", (char *)from, (char *)to, NULL};
  struct f_capture c = nt_run(argv);
  assert(!c.status);
  f_capture_free(&c);
}
void nt_repo(const char *from, const char *to) {
  char cwd[F_PATH];
  assert(getcwd(cwd, sizeof cwd));
  nt_copy_tree(from, to);
  assert(!chdir(to));
  char *init[] = {"git", "init", "-q", NULL},
       *add[] = {"git", "add", ".", NULL},
       *commit[] = {"git",
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "-c",
                    "commit.gpgSign=false",
                    "commit",
                    "-qm",
                    "test fixture",
                    NULL};
  char **cmds[] = {init, add, commit};
  for (size_t i = 0; i < 3; i++) {
    struct f_capture c = nt_run(cmds[i]);
    assert(!c.status);
    f_capture_free(&c);
  }
  assert(!chdir(cwd));
}
void nt_finish(const char *folder, const char *message) {
  assert(!f_remove_tree(folder));
  puts(message);
}

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
static double nt_clock(void) {
  struct timespec t;
  assert(!clock_gettime(CLOCK_MONOTONIC, &t));
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
struct nt_child nt_start(char *const argv[], const char *input) {
  struct nt_child child = {0};
  FILE *in = tmpfile();
  assert(in);
  child.out = tmpfile();
  child.err = tmpfile();
  assert(child.out && child.err);
  if (input)
    assert(fwrite(input, 1, strlen(input), in) == strlen(input));
  assert(!fflush(in) && !fseek(in, 0, SEEK_SET));
  child.pid = fork();
  assert(child.pid >= 0);
  if (!child.pid) {
    assert(setsid() >= 0);
    assert(dup2(fileno(in), STDIN_FILENO) >= 0 &&
           dup2(fileno(child.out), STDOUT_FILENO) >= 0 &&
           dup2(fileno(child.err), STDERR_FILENO) >= 0);
    fclose(in);
    fclose(child.out);
    fclose(child.err);
    execvp(argv[0], argv);
    perror(argv[0]);
    _exit(127);
  }
  fclose(in);
  return child;
}
static char *nt_capture_file(FILE *f, size_t *bytes) {
  assert(!fflush(f) && !fseek(f, 0, SEEK_END));
  long n = ftell(f);
  assert(n >= 0 && n <= 64 * 1024 * 1024);
  assert(!fseek(f, 0, SEEK_SET));
  char *s = malloc((size_t)n + 1);
  assert(s);
  assert(fread(s, 1, (size_t)n, f) == (size_t)n && !ferror(f));
  s[n] = 0;
  *bytes = (size_t)n;
  return s;
}
struct f_capture nt_wait(struct nt_child *child, unsigned seconds) {
  struct f_capture c = {0};
  int status;
  double deadline = nt_clock() + seconds;
  pid_t got;
  assert(child->pid > 0);
  for (;;) {
    got = waitpid(child->pid, &status, WNOHANG);
    if (got == child->pid)
      break;
    assert(!got || (got < 0 && errno == EINTR));
    if (nt_clock() >= deadline) {
      kill(-child->pid, SIGKILL);
      while (waitpid(child->pid, &status, 0) < 0)
        assert(errno == EINTR);
      c.timeout = true;
      break;
    }
    struct timespec pause = {0, 10000000};
    nanosleep(&pause, NULL);
  }
  c.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  c.out = nt_capture_file(child->out, &c.out_bytes);
  c.err = nt_capture_file(child->err, &c.err_bytes);
  assert(!fclose(child->out) && !fclose(child->err));
  child->pid = 0;
  child->out = child->err = NULL;
  return c;
}
void nt_await_file(const char *path, unsigned seconds) {
  double deadline = nt_clock() + seconds;
  while (access(path, F_OK) && nt_clock() < deadline) {
    struct timespec pause = {0, 10000000};
    nanosleep(&pause, NULL);
  }
  assert(!access(path, F_OK));
}
