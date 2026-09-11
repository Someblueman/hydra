#include "outcome_support.h"
void outcome_status(char *const argv[], int expected) {
  struct f_capture c = nt_run(argv);
  if (c.status != expected)
    fprintf(stderr, "%s exit %d expected %d\n%s\n%s\n", argv[0], c.status,
            expected, c.out, c.err);
  assert(c.status == expected);
  f_capture_free(&c);
}
void outcome_commit(void) {
  char *init[] = {"git", "init", "-q", NULL},
       *add[] = {"git", "add", ".", NULL};
  char *commit[] = {"git",
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "-c",
                    "commit.gpgSign=false",
                    "commit",
                    "--allow-empty",
                    "-qm",
                    "outcome fixture",
                    NULL};
  outcome_status(init, 0);
  outcome_status(add, 0);
  outcome_status(commit, 0);
}
void outcome_setup(struct outcome_fixture *f, const char *example) {
  char temp[] = "/tmp/hydra-outcome-XXXXXX", path[F_PATH], source[F_PATH],
       home[F_PATH];
  assert(getcwd(f->origin, sizeof f->origin));
  assert(mkdtemp(temp));
  assert(!f_copy(f->folder, sizeof f->folder, temp));
  nt_path(f->repo, f->folder, "repo");
  nt_path(path, f->origin, "examples/planning");
  nt_path(source, path, example);
  nt_copy_tree(source, f->repo);
  const char *binary = getenv("HYDRA_PLAN_EXAMPLE_BIN");
  if (!binary) {
    nt_path(source, f->origin, "build/plan-example");
    binary = source;
  }
  nt_path(path, f->repo, "plan-example");
  nt_copy_tree(binary, path);
  nt_path(source, f->origin, "examples/planning/native/payload.sh");
  nt_path(path, f->repo, "payload.sh");
  nt_copy_tree(source, path);
  nt_path(f->hydra, f->origin, "bin/hydra");
  nt_path(home, f->folder, "home");
  assert(!setenv("HYDRA_HOME", home, 1));
  if (!getenv("HYDRA_FLEET_BIN")) {
    nt_path(path, f->origin, "build/hydra-fleet");
    assert(!setenv("HYDRA_FLEET_BIN", path, 1));
  }
  const char *pre = getenv("HYDRA_PLAN_PRECOMPILE_BIN");
  if (pre)
    assert(!f_copy(f->precompiler, sizeof f->precompiler, pre));
  else
    nt_path(f->precompiler, f->origin, "build/plan-precompile");
  nt_path(f->inputs, f->folder, "payload inputs");
  nt_path(f->outputs, f->folder, "payload outputs");
  assert(!mkdir(f->inputs, 0700) && !mkdir(f->outputs, 0700));
  assert(!setenv("HYDRA_WORKFLOW_INPUTS_DIR", f->inputs, 1) &&
         !setenv("HYDRA_WORKFLOW_OUTPUTS_DIR", f->outputs, 1));
  nt_path(path, f->folder, "validation.json");
  nt_write(
      path,
      "{\"data\":{\"check\":"
      "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
      "\"check-recipe\":"
      "\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}");
  assert(!setenv("HYDRA_WORKFLOW_VALIDATION_FILE", path, 1));
  assert(!chdir(f->repo));
  outcome_commit();
}
void outcome_finish(struct outcome_fixture *f, const char *message) {
  assert(!chdir(f->origin));
  nt_finish(f->folder, message);
}
json_object *outcome_cli(struct outcome_fixture *f, const char *op,
                         const char *a, const char *b, const char *c,
                         int status) {
  char *argv[] = {f->hydra,  "workflow", "plan",    (char *)op,
                  (char *)a, (char *)b,  (char *)c, NULL};
  struct f_capture r = nt_run(argv);
  json_object *v = nt_output(&r, status);
  f_capture_free(&r);
  return v;
}
void outcome_check(const char *command, int status) {
  char *argv[] = {"./plan-example", (char *)command, NULL};
  outcome_status(argv, status);
}
