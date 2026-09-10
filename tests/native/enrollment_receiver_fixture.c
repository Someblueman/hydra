#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void write_file(const char *path, const char *text, const char *mode) {
  if (!path)
    return;
  FILE *f = fopen(path, mode);
  assert(f);
  assert(fputs(text, f) >= 0 && !fclose(f));
}
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "init")) {
      char pid[64];
      assert(snprintf(pid, sizeof pid, "%ld", (long)getpid()) > 0);
      write_file(getenv("ENROLL_CHILD_PID"), pid, "w");
      write_file(getenv("ENROLL_STARTED"), "", "w");
      const char *hold = getenv("ENROLL_HOLD");
      while (hold && access(hold, F_OK)) {
        struct timespec pause = {0, 10000000};
        nanosleep(&pause, NULL);
      }
      write_file(getenv("ENROLL_COUNTER"), "init\n", "a");
      break;
    }
  const char *real = getenv("ENROLL_REAL");
  assert(real);
  argv[0] = (char *)real;
  // The isolated test runner selects this local Hydra executable; preserve argv
  // exactly. NOLINTNEXTLINE(clang-analyzer-optin.taint.GenericTaint)
  execv(real, argv);
  perror(real);
  return 127;
}
