#include "enrollment.h"
/* Scoped revocation (H6) against reviewed enrollment: review refuses revoked
 * candidates, packages, projects, aliases and accepted keys; a revocation
 * after review invalidates the pending intent without any connection; an
 * enrolled alias stays on disk and inspectable while every new operation
 * and alias reuse is blocked. Nothing is uninstalled or cancelled. */
static json_object *run(char *const argv[]) {
  struct f_capture c = nt_run(argv);
  json_object *v = f_parse(c.out);
  if (!v)
    fprintf(stderr, "%s\n%s", c.out, c.err);
  assert(v);
  f_capture_free(&c);
  return v;
}
static void revoke_identity(const char *scope, const char *id) {
  json_object *v = EC(false, true, "revoke", scope, id, "--reason",
                      "operator decision");
  assert(!strcmp(f_string(f_field(v, "data"), "scope"), scope));
  json_object_put(v);
}
static void refused(json_object *v, const char *scope) {
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked") &&
         !strcmp(f_string(f_field(v, "data"), "scope"), scope));
  json_object_put(v);
}
static json_object *review(const char *q, const struct ec_package *package,
                           const char *alias) {
  char intent[F_PATH];
  nt_path(intent, ec_root, "intent.json");
  const char *args[24];
  size_t n = 0;
  args[n++] = "review";
  args[n++] = "--input";
  args[n++] = q;
  args[n++] = "--project";
  args[n++] = ec_project;
  args[n++] = "--output";
  args[n++] = intent;
  args[n++] = "--candidate";
  args[n++] = "cand_676f6f64";
  if (alias) {
    args[n++] = "--alias";
    args[n++] = alias;
  }
  if (package) {
    args[n++] = "--package";
    args[n++] = package->path;
    args[n++] = "--sha256";
    args[n++] = package->digest;
    args[n++] = "--prefix";
    args[n++] = package->prefix;
  }
  args[n] = NULL;
  return ec_cli(true, args, false);
}
static void candidate_refused(void) {
  char q[F_PATH];
  ec_qualify(q, NULL, NULL, NULL);
  revoke_identity("candidate", "good");
  refused(review(q, NULL, NULL), "candidate");
  ec_count("mutations", "init\n", 0);
}
static void package_refused(void) {
  char q[F_PATH];
  struct ec_package package;
  ec_package_make(&package);
  ec_qualify(q, NULL, NULL, NULL);
  revoke_identity("package", package.digest);
  refused(review(q, &package, NULL), "package");
  json_object *v = EC(false, false, "bootstrap", "cand_676f6f64", "--input",
                      package.path, "--sha256", package.digest);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked"));
  json_object_put(v);
  ec_count("installs", "install\n", 0);
}
static void project_refused(void) {
  char q[F_PATH];
  ec_qualify(q, NULL, NULL, NULL);
  revoke_identity("project", ec_project);
  refused(review(q, NULL, NULL), "project");
  json_object *v = EC(false, false, "list", "cand_676f6f64", "--project",
                      ec_project);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked"));
  json_object_put(v);
}
static void apply_invalidated(void) {
  char intent[F_PATH], digest[65];
  ec_one_review(intent, digest, NULL);
  revoke_identity("candidate", "cand_676f6f64");
  json_object *v = ec_apply(intent, digest),
              *row = json_object_array_get_idx(f_field(v, "hosts"), 0);
  assert(!strcmp(f_string(row, "status"), "revoked") &&
         !strcmp(f_string(f_field(row, "revocation"), "scope"), "candidate") &&
         !f_field(row, "preflight"));
  nt_equal(f_field(v, "partial_failure"), "true");
  json_object_put(v);
  ec_count("mutations", "init\n", 0);
  v = EC(false, true, "revoke", "list", "--scope", "candidate");
  nt_equal(f_field(f_field(v, "data"), "count"), "1");
  json_object_put(v);
}
static void transport_block(void) {
  char intent[F_PATH], digest[65], alias[F_PATH], q[F_PATH];
  ec_one_review(intent, digest, NULL);
  ec_status(ec_apply(intent, digest), "enrolled");
  nt_path(alias, ec_home, "fleet/remotes/cand_676f6f64.json");
  char *before = f_read(alias, 100000);
  assert(before);
  revoke_identity("transport", "cand_676f6f64");
  char *after = f_read(alias, 100000);
  assert(after && !strcmp(before, after));
  free(after);
  free(before);
  ec_status(ec_apply(intent, digest), "enrolled");
  ec_count("mutations", "init\n", 1);
  json_object *v = EC(false, true, "revoke", "list");
  json_object *record =
      json_object_array_get_idx(f_field(f_field(v, "data"), "revocations"), 0);
  nt_equal(f_field(f_field(record, "superseded"), "accepted_host_key"),
           "\"SHA256:fixture\"");
  assert(f_string(f_field(record, "revoked_by"), "user") &&
         f_field(f_field(record, "superseded"), "alias_record"));
  json_object_put(v);
  char *list[] = {ec_cli_path, "fleet", "list", "--json", NULL};
  v = run(list);
  json_object *host = json_object_array_get_idx(f_field(f_field(v, "data"), "hosts"), 0);
  assert(!strcmp(f_string(host, "host"), "cand_676f6f64") &&
         !strcmp(f_string(f_field(host, "error"), "code"), "revoked"));
  json_object_put(v);
  v = EC(false, false, "list", "cand_676f6f64");
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked"));
  json_object_put(v);
  char *add[] = {ec_cli_path, "remote", "add", "cand_676f6f64", "elsewhere",
                 NULL};
  v = run(add);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked"));
  json_object_put(v);
  after = f_read(alias, 100000);
  assert(after && !strstr(after, "elsewhere"));
  free(after);
  char *names[] = {ec_cli_path, "remote", "list", NULL};
  v = run(names);
  nt_equal(f_field(v, "data"), "[\"cand_676f6f64\"]");
  json_object_put(v);
  /* A new review under another alias of a host still presenting the
   * revoked key is refused: the key, not only the name, is superseded. */
  ec_qualify(q, NULL, NULL, NULL);
  v = review(q, NULL, "fresh");
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked") &&
         f_string(f_field(v, "data"), "matched"));
  json_object_put(v);
  ec_count("mutations", "init\n", 1);
}
static void key_changed_detail(void) {
  char q[F_PATH], intent[F_PATH], digest[65];
  ec_qualify(q, "SHA256:reviewed", NULL, NULL);
  ec_review(intent, digest, q, NULL, NULL, ec_root);
  json_object *v = ec_apply(intent, digest),
              *row = json_object_array_get_idx(f_field(v, "hosts"), 0),
              *key = f_field(row, "host_key");
  assert(!strcmp(f_string(row, "status"), "host_key_changed"));
  nt_equal(f_field(key, "reviewed_fingerprint"), "\"SHA256:reviewed\"");
  nt_equal(f_field(key, "presented_fingerprint"), "\"SHA256:fixture\"");
  assert(strstr(f_string(key, "acceptance"), "ssh-keygen -R") &&
         f_string(key, "observation") && !f_field(key, "presented_key_revoked"));
  json_object_put(v);
  ec_count("mutations", "init\n", 0);
}
void ec_revocation_cases(void) {
  void (*cases[])(void) = {candidate_refused, package_refused,
                           project_refused,   apply_invalidated,
                           transport_block,   key_changed_detail};
  for (size_t i = 0; i < 6; i++) {
    ec_setup();
    cases[i]();
    ec_cleanup();
    printf("Enrollment revocation case %zu passed\n", i + 1);
  }
}
