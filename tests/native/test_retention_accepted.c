#include "accepted_fixture.h"
#include <dirent.h>
static char base[F_PATH], source[F_PATH], hydra[F_PATH];
static json_object *commands;
static const char *name_of(const char *path) {
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}
static json_object *call(char *const args[], bool ok, json_object *request) {
  struct f_capture c = {0};
  const char *input =
      request ? json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN)
              : NULL;
  assert(!f_run(args, input, input ? strlen(input) : 0, 90, &c));
  assert(!c.timeout);
  size_t n = json_object_array_length(commands);
  char filename[64], out[F_PATH], err[F_PATH], log[F_PATH];
  assert(snprintf(filename, sizeof filename, "%zu.stdout", n) > 0);
  nt_path(out, base, filename);
  nt_write(out, c.out);
  json_object *entry = json_object_new_object(),
              *argv = json_object_new_array();
  f_string_add(entry, "stdout", filename);
  assert(snprintf(filename, sizeof filename, "%zu.stderr", n) > 0);
  nt_path(err, base, filename);
  nt_write(err, c.err);
  f_string_add(entry, "stderr", filename);
  for (size_t i = 0; args[i]; i++)
    json_object_array_add(argv, json_object_new_string(args[i]));
  json_object_object_add(entry, "argv", argv);
  json_object_object_add(entry, "exit", json_object_new_int(c.status));
  json_object_array_add(commands, entry);
  nt_path(log, base, "commands.json");
  nt_json_write(log, commands);
  if ((c.status == 0) != ok)
    fprintf(stderr, "%s\n%s", c.out, c.err);
  assert((c.status == 0) == ok);
  json_object *v = f_parse(c.out);
  assert(v);
  f_capture_free(&c);
  return v;
}
static json_object *result(const char *id, bool ok) {
  char *args[] = {hydra, "workflow", "plan", "result", (char *)id, NULL};
  return call(args, ok, NULL);
}
static void inventory(json_object *files, const char *dir) {
  DIR *d = opendir(dir);
  struct dirent *e;
  assert(d);
  while ((e = readdir(d))) {
    char p[F_PATH], digest[65];
    struct stat st;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
        !strcmp(e->d_name, ".git"))
      continue;
    nt_path(p, dir, e->d_name);
    assert(!lstat(p, &st));
    if (S_ISDIR(st.st_mode))
      inventory(files, p);
    else if (S_ISREG(st.st_mode)) {
      assert(!f_hash(p, digest));
      json_object *v = json_object_new_object();
      f_string_add(v, "sha256", digest);
      json_object_object_add(v, "bytes", json_object_new_int64(st.st_size));
      json_object_object_add(files, p, v);
    }
  }
  assert(!closedir(d));
}
static void all_action(json_object *v, const char *action) {
  json_object *records = f_field(f_field(v, "data"), "records");
  assert(json_object_array_length(records));
  for (size_t i = 0; i < json_object_array_length(records); i++)
    assert(!strcmp(f_string(json_object_array_get_idx(records, i), "action"),
                   action));
  json_object_put(v);
}
int main(int argc, char **argv) {
  char root[F_PATH], fixture[F_PATH], original_home[F_PATH], home[F_PATH],
      run[F_PATH], policy[F_PATH], output[F_PATH], fleet[F_PATH],
      parent[F_PATH], tmp[F_PATH];
  const char *outarg = NULL, *fleetarg = NULL;
  assert(argc >= 4);
  assert(getcwd(root, sizeof root));
  nt_path(hydra, root, "bin/hydra");
  if (argv[1][0] == '/')
    assert(!f_copy(fixture, sizeof fixture, argv[1]));
  else
    nt_path(fixture, root, argv[1]);
  for (int i = 2; i < argc; i += 2) {
    assert(i + 1 < argc);
    if (!strcmp(argv[i], "--fleet"))
      fleetarg = argv[i + 1];
    else {
      assert(!strcmp(argv[i], "--output"));
      outarg = argv[i + 1];
    }
  }
  assert(outarg);
  if (outarg[0] == '/')
    assert(!f_copy(output, sizeof output, outarg));
  else
    nt_path(output, root, outarg);
  if (fleetarg) {
    if (fleetarg[0] == '/')
      assert(!f_copy(fleet, sizeof fleet, fleetarg));
    else
      nt_path(fleet, root, fleetarg);
  } else
    nt_path(fleet, root, "build/hydra-fleet");
  nt_path(source, fixture, "source");
  nt_path(original_home, fixture, "home");
  nt_path(parent, root, "build/qualification");
  assert(!f_mkdirs(parent));
  nt_path(tmp, parent, "retention-accepted-native-XXXXXX");
  assert(mkdtemp(tmp));
  assert(!f_copy(base, sizeof base, tmp));
  nt_path(home, base, "home");
  nt_copy_tree(original_home, home);
  af_run_path(run, home);
  af_environment(run, home, fleet);
  assert(!chdir(source));
  commands = json_object_new_array();
  json_object *accepted = result(name_of(run), true);
  assert(!strcmp(f_string(f_field(accepted, "data"), "verdict"), "pass"));
  glob_t tasks;
  af_glob(&tasks, home, "fleet/tasks/task_*");
  assert(tasks.gl_pathc >= 2);
  json_object *bindings = json_object_new_object(),
              *packages = json_object_new_object(),
              *workspaces = json_object_new_object();
  for (size_t i = 0; i < tasks.gl_pathc; i++) {
    char p[F_PATH], workspace[F_PATH];
    const char *t = tasks.gl_pathv[i];
    nt_path(p, t, "acceptance.json");
    char *binding = f_read(p, 8000000);
    assert(binding);
    f_string_add(bindings, name_of(t), binding);
    free(binding);
    nt_path(p, t, "package.json");
    json_object *package = f_read_json(p, 8000000);
    assert(package);
    json_object_object_add(packages, name_of(t), package);
    nt_path(workspace, t, "workspace");
    nt_path(p, workspace, "retention-user-change.txt");
    nt_write(p, "preserve this uncommitted workspace file\n");
    inventory(workspaces, workspace);
  }
  assert(json_object_object_length(workspaces) > 0);
  nt_path(policy, base, "policy.json");
  nt_write(policy, "{\"schema_version\":1,\"audit_days\":1,\"max_bytes\":"
                   "67108864,\"max_evidence_records\":1024}");
  af_old(home);
  char *pin[] = {
      hydra, "fleet", "retention", "pin", "run", (char *)name_of(run), NULL};
  json_object_put(call(pin, true, NULL));
  char *apply[] = {hydra,      "fleet", "retention", "apply",
                   "--policy", policy,  NULL};
  all_action(call(apply, true, NULL), "preserve");
  json_object *v = result(name_of(run), true);
  assert(!strcmp(f_string(f_field(v, "data"), "verdict"), "pass"));
  json_object_put(v);
  pin[3] = "unpin";
  json_object_put(call(pin, true, NULL));
  all_action(call(apply, true, NULL), "expire");
  json_object *refused = result(name_of(run), false);
  assert(
      !strcmp(f_string(f_field(refused, "error"), "code"), "evidence_expired"));
  char *status[] = {hydra,    "workflow", "status", (char *)name_of(run),
                    "--json", NULL};
  v = call(status, true, NULL);
  assert(!strcmp(f_string(f_field(v, "data"), "state"), "succeeded"));
  nt_equal(f_field(f_field(v, "data"), "evidence_expired"), "true");
  json_object_put(v);
  char *serve[] = {fleet, "fleet", "serve", NULL};
  for (size_t i = 0; i < tasks.gl_pathc; i++) {
    const char *t = tasks.gl_pathv[i], *id = name_of(t),
               *binding = f_string(bindings, id);
    json_object
        *b = f_parse(binding),
        *r = f_parse(
            "{\"protocol\":1,\"action\":\"task\",\"operation\":\"submit\"}");
    f_string_add(r, "submission_key", f_string(b, "submission_key"));
    json_object_object_add(r, "package", nt_clone(f_field(packages, id)));
    v = call(serve, true, r);
    assert(!strcmp(f_string(f_field(v, "data"), "task_id"), id));
    assert(!strcmp(
        f_string(f_field(f_field(v, "data"), "runtime"), "result_state"),
        "expired"));
    char p[F_PATH];
    nt_path(p, t, "acceptance.json");
    char *after = f_read(p, 8000000);
    assert(after && !strcmp(after, binding));
    free(after);
    nt_path(p, t, "result.json");
    assert(access(p, F_OK));
    json_object_put(v);
    json_object_put(r);
    json_object_put(b);
  }
  json_object
      *b = f_parse(f_string(bindings, name_of(tasks.gl_pathv[0]))),
      *request = f_parse(
          "{\"protocol\":1,\"action\":\"task\",\"operation\":\"submit\"}");
  f_string_add(request, "submission_key", f_string(b, "submission_key"));
  json_object_object_add(
      request, "package",
      nt_clone(f_field(packages, name_of(tasks.gl_pathv[1]))));
  v = call(serve, false, request);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "submission_conflict"));
  json_object_put(v);
  json_object_put(b);
  json_object_put(request);
  glob_t after;
  af_glob(&after, home, "fleet/tasks/task_*");
  assert(after.gl_pathc == tasks.gl_pathc);
  for (size_t i = 0; i < tasks.gl_pathc; i++)
    assert(!strcmp(tasks.gl_pathv[i], after.gl_pathv[i]));
  globfree(&after);
  json_object_object_foreach(workspaces, workspace_path, saved) {
    char digest[65];
    struct stat st;
    assert(!stat(workspace_path, &st) && !f_hash(workspace_path, digest));
    assert(st.st_size == json_object_get_int64(f_field(saved, "bytes")) &&
           !strcmp(digest, f_string(saved, "sha256")));
  }
  json_object_put(call(apply, true, NULL));
  json_object *summary = json_object_new_object();
  f_string_add(summary, "fixture_copy", base);
  f_string_add(summary, "original_fixture", fixture);
  f_string_add(summary, "run", name_of(run));
  f_string_add(summary, "accepted_before",
               f_string(f_field(accepted, "data"), "verdict"));
  f_string_add(summary, "pinned_result", "pass");
  f_string_add(summary, "expired_result",
               f_string(f_field(refused, "error"), "code"));
  json_object_object_add(summary, "receiver_bindings_preserved",
                         json_object_new_int64((int64_t)tasks.gl_pathc));
  json_object_object_add(summary, "duplicate_submissions_reused",
                         json_object_new_int64((int64_t)tasks.gl_pathc));
  json_object_object_add(summary, "new_receiver_acceptances",
                         json_object_new_int(0));
  json_object_object_add(summary, "workspaces_preserved",
                         json_object_new_boolean(true));
  char p[F_PATH], digest[65];
  nt_path(p, base, "commands.json");
  assert(!f_hash(p, digest));
  f_string_add(summary, "commands_sha256", digest);
  nt_json_write(output, summary);
  puts(json_object_to_json_string_ext(summary, JSON_C_TO_STRING_PRETTY));
  json_object_put(summary);
  json_object_put(accepted);
  json_object_put(refused);
  json_object_put(commands);
  json_object_put(bindings);
  json_object_put(packages);
  json_object_put(workspaces);
  globfree(&tasks);
  assert(!chdir(root));
  return 0;
}
