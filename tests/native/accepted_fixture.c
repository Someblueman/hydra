#include "accepted_fixture.h"
#include <dirent.h>
#include <time.h>
#include <utime.h>
void af_glob(glob_t *paths, const char *base, const char *suffix) {
  char pattern[F_PATH];
  nt_path(pattern, base, suffix);
  memset(paths, 0, sizeof *paths);
  int rc = glob(pattern, 0, NULL, paths);
  assert(!rc || rc == GLOB_NOMATCH);
}
void af_run_path(char out[F_PATH], const char *home) {
  glob_t paths;
  af_glob(&paths, home, "state/v2/projects/*/workflows/runs/run_*");
  assert(paths.gl_pathc == 1);
  assert(!f_copy(out, F_PATH, paths.gl_pathv[0]));
  globfree(&paths);
}
void af_environment(const char *run, const char *home, const char *fleet) {
  char p[F_PATH];
  nt_path(p, run, "artifacts/environment");
  json_object *v = f_read_json(p, 2000000);
  assert(v);
  json_object *variables = f_field(v, "variables");
  assert(json_object_is_type(variables, json_type_object));
  json_object_object_foreach(variables, key, value) {
    assert(f_text(value) && !setenv(key, f_text(value), 1));
  }
  json_object_put(v);
  assert(!setenv("HYDRA_HOME", home, 1) &&
         !setenv("HYDRA_FLEET_BIN", fleet, 1));
  const char *bad[] = {"LD_PRELOAD",
                       "LD_LIBRARY_PATH",
                       "DYLD_INSERT_LIBRARIES",
                       "DYLD_LIBRARY_PATH",
                       "ENV",
                       "BASH_ENV"};
  for (size_t i = 0; i < 6; i++)
    assert(!unsetenv(bad[i]));
}
void af_hash_json(json_object *value, char digest[65], bool canonical) {
  if (canonical) {
    assert(!plan_digest(value, digest));
    return;
  }
  char file[] = "/tmp/hydra-envelope-hash-XXXXXX";
  int fd = mkstemp(file);
  assert(fd >= 0 && !close(fd));
  nt_json_write(file, value);
  assert(!f_hash(file, digest) && !unlink(file));
}
void af_old(const char *directory) {
  DIR *dir = opendir(directory);
  struct dirent *e;
  assert(dir);
  struct utimbuf t = {time(NULL) - 3 * 86400, time(NULL) - 3 * 86400};
  while ((e = readdir(dir))) {
    char p[F_PATH];
    struct stat st;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    nt_path(p, directory, e->d_name);
    assert(!lstat(p, &st));
    if (S_ISLNK(st.st_mode))
      continue;
    if (S_ISDIR(st.st_mode))
      af_old(p);
    assert(!utime(p, &t));
  }
  assert(!closedir(dir));
}
