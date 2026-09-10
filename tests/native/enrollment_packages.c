#include "enrollment.h"
#include <dirent.h>
static void changed_package(void) {
  char q[F_PATH], intent[F_PATH], digest[65];
  ec_qualify(q, NULL, NULL, NULL);
  struct ec_package p;
  ec_package_make(&p);
  ec_review(intent, digest, q, NULL, &p, NULL);
  FILE *f = fopen(p.path, "a");
  assert(f && fputc(' ', f) != EOF && !fclose(f));
  ec_status(ec_apply(intent, digest), "review_required");
  ec_count("mutations", "init\n", 0);
  assert(access(p.prefix, F_OK));
}
static bool same_mtime(const struct stat *a, const struct stat *b) {
#ifdef __APPLE__
  return a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec &&
         a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec;
#else
  return a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
         a->st_mtim.tv_nsec == b->st_mtim.tv_nsec;
#endif
}
static void lost_install(void) {
  char q[F_PATH], intent[F_PATH], digest[65], drop[F_PATH], binary[F_PATH];
  ec_qualify(q, NULL, NULL, NULL);
  struct ec_package p;
  ec_package_make(&p);
  ec_review(intent, digest, q, NULL, &p, NULL);
  nt_path(drop, ec_root, "dropped-install");
  assert(!setenv("ENROLL_DROP_INSTALL_RESPONSE", drop, 1));
  json_object *first = ec_apply(intent, digest);
  assert(!strcmp(
      f_string(json_object_array_get_idx(f_field(first, "hosts"), 0), "phase"),
      "install"));
  ec_status(first, "outcome_unknown");
  ec_count("mutations", "init\n", 0);
  nt_path(binary, p.prefix, "libexec/hydra/hydra-fleet");
  struct stat before, after;
  assert(!stat(binary, &before));
  ec_status(ec_apply(intent, digest), "enrolled");
  char actual[65], expected[65];
  assert(!f_hash(binary, actual) && !f_hash(ec_fleet, expected) &&
         !strcmp(actual, expected));
  assert(!stat(binary, &after) && before.st_ino == after.st_ino &&
         same_mtime(&before, &after));
  ec_count("installs", "install\n", 1);
  ec_count("mutations", "init\n", 1);
}
static void lost_changed(void) {
  char q[F_PATH], intent[F_PATH], digest[65], drop[F_PATH], binary[F_PATH];
  ec_qualify(q, NULL, NULL, NULL);
  struct ec_package p;
  ec_package_make(&p);
  ec_review(intent, digest, q, NULL, &p, NULL);
  nt_path(drop, ec_root, "dropped-install");
  assert(!setenv("ENROLL_DROP_INSTALL_RESPONSE", drop, 1));
  ec_status(ec_apply(intent, digest), "outcome_unknown");
  nt_path(binary, p.prefix, "bin/hydra");
  nt_write(binary, "changed");
  ec_status(ec_apply(intent, digest), "outcome_unknown");
  ec_count("installs", "install\n", 1);
  ec_count("mutations", "init\n", 0);
}
static void pinned_upgrade(void) {
  struct ec_package p;
  ec_package_make(&p);
  assert(!setenv("ENROLL_OLD_RECEIVER_PREFIX", p.prefix, 1));
  char q[F_PATH], intent[F_PATH], digest[65];
  ec_qualify(q, NULL, NULL, NULL);
  json_object *v = f_read_json(q, 8000000),
              *caps = f_field(
                  f_field(
                      f_field(json_object_array_get_idx(
                                  f_field(f_field(v, "data"), "candidates"), 0),
                              "qualification"),
                      "data"),
                  "capabilities");
  for (size_t i = 0; i < json_object_array_length(caps); i++)
    assert(strcmp(json_object_get_string(json_object_array_get_idx(caps, i)),
                  "enrollment-init"));
  json_object_put(v);
  ec_review(intent, digest, q, NULL, &p, NULL);
  ec_status(ec_apply(intent, digest), "enrolled");
  ec_count("installs", "install\n", 1);
  ec_count("mutations", "init\n", 1);
}
static void snapshot(json_object *files, const char *directory) {
  DIR *d = opendir(directory);
  struct dirent *e;
  assert(d);
  while ((e = readdir(d))) {
    char p[F_PATH], digest[65];
    struct stat st;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    nt_path(p, directory, e->d_name);
    assert(!stat(p, &st));
    if (S_ISDIR(st.st_mode))
      snapshot(files, p);
    else if (S_ISREG(st.st_mode)) {
      assert(!f_hash(p, digest));
      json_object *v = json_object_new_object();
      f_string_add(v, "sha256", digest);
#ifdef __APPLE__
      json_object_object_add(v, "seconds",
                             json_object_new_int64(st.st_mtimespec.tv_sec));
      json_object_object_add(v, "nanoseconds",
                             json_object_new_int64(st.st_mtimespec.tv_nsec));
#else
      json_object_object_add(v, "seconds",
                             json_object_new_int64(st.st_mtim.tv_sec));
      json_object_object_add(v, "nanoseconds",
                             json_object_new_int64(st.st_mtim.tv_nsec));
#endif
      json_object_object_add(files, p, v);
    }
  }
  assert(!closedir(d));
}
static json_object *install(const struct ec_package *p, const char *command,
                            const char *target) {
  struct stat st;
  assert(!stat(p->path, &st) && st.st_size > 0 &&
         st.st_size < 64 * 1024 * 1024);
  char *raw = f_read(p->path, 64 * 1024 * 1024);
  assert(raw);
  char *args[] = {ec_fleet,   (char *)command, (char *)p->digest,
                  "--prefix", (char *)target,  NULL};
  struct f_capture c = {0};
  assert(!f_run(args, raw, (size_t)st.st_size, 120, &c));
  free(raw);
  json_object *v = f_parse(c.out);
  assert(v);
  f_capture_free(&c);
  return v;
}
static void install_check(void) {
  struct ec_package p;
  ec_package_make(&p);
  json_object *v = install(&p, "install", p.prefix);
  nt_equal(f_field(v, "ok"), "true");
  json_object_put(v);
  json_object *before = json_object_new_object();
  snapshot(before, p.prefix);
  v = install(&p, "install-check", p.prefix);
  nt_equal(f_field(v, "ok"), "true");
  json_object_put(v);
  json_object *after = json_object_new_object();
  snapshot(after, p.prefix);
  assert(json_object_equal(before, after));
  json_object_put(before);
  json_object_put(after);
  char wrong[F_PATH], binary[F_PATH];
  nt_path(wrong, ec_root, "exact-prefix-evil");
  v = install(&p, "install-check", wrong);
  nt_equal(f_field(v, "ok"), "false");
  json_object_put(v);
  assert(access(wrong, F_OK));
  nt_path(binary, p.prefix, "bin/hydra");
  nt_write(binary, "changed");
  before = json_object_new_object();
  snapshot(before, p.prefix);
  v = install(&p, "install", p.prefix);
  nt_equal(f_field(v, "ok"), "false");
  json_object_put(v);
  after = json_object_new_object();
  snapshot(after, p.prefix);
  assert(json_object_equal(before, after));
  json_object_put(before);
  json_object_put(after);
}
void ec_package_cases(void) {
  void (*cases[])(void) = {changed_package, lost_install, lost_changed,
                           pinned_upgrade, install_check};
  for (size_t i = 0; i < 5; i++) {
    ec_setup();
    cases[i]();
    ec_cleanup();
    printf("Enrollment package case %zu passed\n", i + 1);
  }
}
