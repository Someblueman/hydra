#include "outcome_support.h"
#include <ctype.h>
static struct outcome_fixture f;
static json_object *commands;
static char *call(char *const argv[], int status, const char *label) {
  struct f_capture c = nt_run(argv);
  char stem[100], name[128], path[F_PATH];
  if (label)
    assert(!f_copy(stem, sizeof stem, label));
  else
    assert(snprintf(stem, sizeof stem, "command-%zu",
                    json_object_array_length(commands)) > 0);
  assert(snprintf(name, sizeof name, "%s.stdout", stem) > 0);
  nt_path(path, f.folder, name);
  nt_write(path, c.out);
  assert(snprintf(name, sizeof name, "%s.stderr", stem) > 0);
  nt_path(path, f.folder, name);
  nt_write(path, c.err);
  json_object *record = json_object_new_object(),
              *args = json_object_new_array();
  for (size_t i = 0; argv[i]; i++)
    json_object_array_add(args, json_object_new_string(argv[i]));
  json_object_object_add(record, "argv", args);
  json_object_object_add(record, "exit", json_object_new_int(c.status));
  f_string_add(record, "log", stem);
  json_object_array_add(commands, record);
  nt_path(path, f.folder, "commands.json");
  nt_json_write(path, commands);
  if (c.status != status)
    fprintf(stderr, "%s exit %d expected %d\n%s\n%s\n", stem, c.status, status,
            c.out, c.err);
  assert(c.status == status);
  char *out = strdup(c.out);
  assert(out);
  f_capture_free(&c);
  return out;
}
static void commit(const char *message) {
  char *add[] = {"git", "add", ".", NULL},
       *argv[] = {"git",
                  "-c",
                  "user.name=Test",
                  "-c",
                  "user.email=test@example.invalid",
                  "-c",
                  "commit.gpgSign=false",
                  "commit",
                  "-qm",
                  (char *)message,
                  NULL};
  free(call(add, 0, NULL));
  free(call(argv, 0, NULL));
}
static void run_id(const char *text, char id[100]) {
  while (*text) {
    const char *end = strchr(text, '\n');
    if (!end)
      end = text + strlen(text);
    size_t n = (size_t)(end - text);
    if (n > 4 && n < 100 && !strncmp(text, "run_", 4)) {
      bool valid = true;
      for (size_t i = 4; i < n; i++)
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= '0' && text[i] <= '9')))
          valid = false;
      if (valid) {
        memcpy(id, text, n);
        id[n] = 0;
        return;
      }
    }
    text = *end ? end + 1 : end;
  }
  assert(!"missing run id");
}
static json_object *run_plan(int number, const char *plan, const char *policy,
                             json_object **data) {
  char compiled[F_PATH], name[100], label[100], id[100];
  assert(snprintf(name, sizeof name, "compiled%d.json", number) > 0);
  nt_path(compiled, f.folder, name);
  assert(snprintf(label, sizeof label, "compile%d", number) > 0);
  char *compile[] = {f.hydra,      "workflow",     "plan",   "compile",
                     (char *)plan, (char *)policy, compiled, NULL};
  char *text = call(compile, 0, label);
  json_object *v = f_parse(text);
  free(text);
  assert(v);
  char digest[65];
  assert(
      !f_copy(digest, sizeof digest, f_string(f_field(v, "data"), "sha256")));
  json_object_put(v);
  char *launch[] = {f.hydra,  "workflow", "plan", "run",
                    compiled, "--accept", digest, NULL};
  assert(snprintf(label, sizeof label, "launch%d", number) > 0);
  text = call(launch, 0, label);
  run_id(text, id);
  free(text);
  char *result[] = {f.hydra, "workflow", "plan", "result", id, NULL};
  assert(snprintf(label, sizeof label, "result%d", number) > 0);
  text = call(result, 0, label);
  v = f_parse(text);
  free(text);
  assert(v && json_object_get_boolean(f_field(v, "ok")) &&
         !strcmp(f_string(f_field(v, "data"), "verdict"), "pass"));
  *data = nt_clone(f_field(v, "data"));
  json_object_put(v);
  json_object *summary = json_object_new_object();
  f_string_add(summary, "run_id", id);
  f_string_add(summary, "plan_sha256", digest);
  assert(snprintf(name, sizeof name, "result%d.stdout", number) > 0);
  nt_path(compiled, f.folder, name);
  f_string_add(summary, "result", compiled);
  return summary;
}
static json_object *choose_manifest(const char *choice) {
  json_object *v = f_read_json("manifest.json", 1000000);
  assert(v);
  json_object *items = f_field(v, "items");
  if (!strcmp(choice, "empty"))
    json_object_object_add(v, "items", json_object_new_array());
  else if (!strcmp(choice, "all-skipped"))
    for (size_t i = 0; i < json_object_array_length(items); i++)
      json_object_object_add(json_object_array_get_idx(items, i), "enabled",
                             json_object_new_boolean(false));
  else if (!strcmp(choice, "collision") || !strcmp(choice, "maximum")) {
    items = json_object_new_array();
    json_object_object_add(v, "items", items);
    bool max = !strcmp(choice, "maximum");
    const char *names[] = {"finding", "manifest", "compose", "check"};
    for (size_t i = 0; i < (max ? 8 : 4); i++) {
      char id[40];
      if (max)
        assert(snprintf(id, sizeof id, "item-%zu", i) > 0);
      else
        assert(!f_copy(id, sizeof id, names[i]));
      json_object *a = json_object_new_object();
      f_string_add(a, "id", id);
      json_object_object_add(a, "value", json_object_new_int(max ? (int)i : 2));
      json_object_object_add(a, "enabled", json_object_new_boolean(true));
      json_object_array_add(items, a);
    }
  } else
    assert(!strcmp(choice, "normal"));
  nt_json_write("manifest.json", v);
  return v;
}
static void setup(const char *choice) {
  assert(getcwd(f.origin, sizeof f.origin));
  char path[F_PATH], source[F_PATH], temp[F_PATH], home[F_PATH];
  nt_path(path, f.origin, "build/qualification");
  assert(!f_mkdirs(path));
  char name[100];
  assert(snprintf(name, sizeof name, "staged-%s-native-XXXXXX", choice) > 0);
  nt_path(temp, path, name);
  assert(mkdtemp(temp));
  assert(!f_copy(f.folder, sizeof f.folder, temp));
  nt_path(f.repo, f.folder, "source");
  nt_path(source, f.origin, "examples/planning/staged");
  nt_copy_tree(source, f.repo);
  nt_path(source, f.origin, "examples/planning/native/payload.sh");
  nt_path(path, f.repo, "payload.sh");
  nt_copy_tree(source, path);
  const char *bin = getenv("HYDRA_PLAN_EXAMPLE_BIN");
  if (!bin) {
    nt_path(source, f.origin, "build/plan-example");
    bin = source;
  }
  nt_path(path, f.repo, "plan-example");
  nt_copy_tree(bin, path);
  nt_path(f.hydra, f.origin, "bin/hydra");
  nt_path(home, f.folder, "home");
  assert(!setenv("HYDRA_HOME", home, 1) && !setenv("HYDRA_BIN", f.hydra, 1) &&
         !setenv("HYDRA_NONINTERACTIVE", "1", 1) &&
         !setenv("HYDRA_SKIP_AI", "1", 1) &&
         !setenv("HYDRA_NO_SWITCH", "1", 1));
  if (!getenv("HYDRA_FLEET_BIN")) {
    nt_path(path, f.origin, "build/hydra-fleet");
    assert(!setenv("HYDRA_FLEET_BIN", path, 1));
  }
  assert(!chdir(f.repo));
  commands = json_object_new_array();
}
static void precompile_gates(const char *run, json_object *result,
                             char plan[F_PATH]) {
  json_object *delivery = f_field(f_field(result, "deliverables"), "report");
  const char *accepted = f_string(delivery, "path");
  char digest[65];
  assert(!f_hash(accepted, digest) &&
         !strcmp(digest, f_string(delivery, "sha256")));
  char *bytes = f_read(accepted, 1000000);
  assert(bytes);
  nt_write("finding.json", bytes);
  nt_path(plan, f.folder, "plan2.json");
  char *argv[] = {f.precompiler, "staged", "manifest.json",
                  (char *)run,   plan,     NULL};
  free(call(argv, 0, "precompile2"));
  FILE *file = fopen("finding.json", "a");
  assert(file && fputc(' ', file) != EOF && !fclose(file));
  free(call(argv, 1, "reject-local-finding-change"));
  nt_write("finding.json", bytes);
  free(bytes);
  char *original = f_read("manifest.json", 1000000);
  assert(original);
  file = fopen("manifest.json", "a");
  assert(file && fputc(' ', file) != EOF && !fclose(file));
  free(call(argv, 1, "reject-stale-source"));
  nt_write("manifest.json", original);
  free(original);
  free(call(argv, 0, "precompile2-final"));
  commit("freeze accepted stage-one finding");
}
static void assert_report(json_object *manifest, json_object *result) {
  json_object *report = f_read_json(
                  f_string(f_field(f_field(result, "deliverables"), "report"),
                           "path"),
                  1000000),
              *expected = f_parse(
                  "{\"schema_version\":1,\"selected_ids\":[],\"members\":[]}");
  json_object *items = f_field(manifest, "items");
  for (size_t i = 0; i < json_object_array_length(items); i++) {
    json_object *a = json_object_array_get_idx(items, i);
    if (!json_object_get_boolean(f_field(a, "enabled")))
      continue;
    json_object_array_add(f_field(expected, "selected_ids"),
                          json_object_new_string(f_string(a, "id")));
    json_object *m = nt_clone(a);
    json_object_object_del(m, "enabled");
    int value = json_object_get_int(f_field(a, "value"));
    json_object_object_add(m, "square", json_object_new_int(value * value));
    json_object_array_add(f_field(expected, "members"), m);
  }
  assert(json_object_equal(report, expected));
  json_object_put(report);
  json_object_put(expected);
}
int main(int argc, char **argv) {
  const char *choice = "normal", *output = NULL, *fleet_arg = NULL,
             *pre = getenv("HYDRA_PLAN_PRECOMPILE_BIN");
  for (int i = 1; i < argc; i++) {
    assert(i + 1 < argc);
    if (!strcmp(argv[i], "--case"))
      choice = argv[++i];
    else if (!strcmp(argv[i], "--output"))
      output = argv[++i];
    else if (!strcmp(argv[i], "--fleet"))
      fleet_arg = argv[++i];
    else if (!strcmp(argv[i], "--precompiler"))
      pre = argv[++i];
    else
      assert(!"unknown argument");
  }
  assert(output);
  char out[F_PATH], origin[F_PATH];
  assert(getcwd(origin, sizeof origin));
  if (output[0] == '/')
    assert(!f_copy(out, sizeof out, output));
  else
    nt_path(out, origin, output);
  if (pre && pre[0] == '/')
    assert(!f_copy(f.precompiler, sizeof f.precompiler, pre));
  else if (pre)
    nt_path(f.precompiler, origin, pre);
  else
    nt_path(f.precompiler, origin, "build/plan-precompile");
  const char *fleet = fleet_arg ? fleet_arg : getenv("HYDRA_FLEET_BIN");
  if (fleet) {
    char path[F_PATH];
    if (fleet[0] == '/')
      assert(!f_copy(path, sizeof path, fleet));
    else
      nt_path(path, origin, fleet);
    assert(!setenv("HYDRA_FLEET_BIN", path, 1));
  }
  setup(choice);
  json_object *manifest = choose_manifest(choice);
  char *init[] = {"git", "init", "-q", NULL};
  free(call(init, 0, NULL));
  commit("staged fixture source");
  char *hydra_init[] = {f.hydra, "init", "--no-agent", "--trust", NULL};
  free(call(hydra_init, 0, NULL));
  json_object *result = NULL, *stage1 = run_plan(1, "stage1-plan.json",
                                                 "stage1-policy.json", &result);
  char plan[F_PATH];
  precompile_gates(f_string(stage1, "run_id"), result, plan);
  json_object_put(result);
  json_object *stage2 = run_plan(2, plan, "policy.json", &result);
  assert_report(manifest, result);
  json_object_put(result);
  json_object_put(manifest);
  json_object *summary = f_parse(
      "{\"schema_version\":1,\"negative_controls\":[\"changed-local-finding\","
      "\"stale-source\"],\"verdict\":\"pass\",\"source_files\":{}}");
  f_string_add(summary, "case", choice);
  f_string_add(summary, "fixture", f.folder);
  json_object_object_add(summary, "stage1", stage1);
  json_object_object_add(summary, "stage2", stage2);
  const char *files[] = {"plan-example", "payload.sh"};
  for (size_t i = 0; i < 2; i++) {
    char hash[65];
    assert(!f_hash(files[i], hash));
    f_string_add(f_field(summary, "source_files"), files[i], hash);
  }
  nt_json_write(out, summary);
  puts(json_object_to_json_string_ext(summary, JSON_C_TO_STRING_PRETTY));
  json_object_put(summary);
  json_object_put(commands);
  assert(!chdir(origin));
  return 0;
}
