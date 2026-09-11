#include "contracts.h"
struct hc_fixture hc;
void hc_write(const char *dir, const char *name, const char *text) {
  char p[F_PATH];
  nt_path(p, dir, name);
  nt_write(p, text);
}
void hc_json(const char *dir, const char *name, json_object *v) {
  char p[F_PATH];
  nt_path(p, dir, name);
  nt_json_write(p, v);
}
json_object *hc_read(const char *dir, const char *name) {
  char p[F_PATH];
  nt_path(p, dir, name);
  json_object *v = f_read_json(p, 8000000);
  assert(v);
  return v;
}
struct f_capture hc_command(char *const args[], bool ok) {
  struct f_capture c = nt_run(args);
  if ((c.status == 0) != ok)
    fprintf(stderr, "%s\n%s\n", c.out, c.err);
  assert((c.status == 0) == ok);
  return c;
}
void hc_native(const char *op, const char *a, const char *b, const char *c,
               const char *d, bool ok) {
  char *args[] = {hc.fleet,  "workflow-data", (char *)op, (char *)a,
                  (char *)b, (char *)c,       (char *)d,  NULL};
  struct f_capture r = hc_command(args, ok);
  if (!ok) {
    json_object *v = f_parse(r.out);
    assert(v && !strcmp(f_string(f_field(v, "error"), "code"), "invalid_data"));
    json_object_put(v);
  }
  f_capture_free(&r);
}
void hc_setup(void) {
  char tmp[] = "/tmp/hydra-native-contract-XXXXXX";
  assert(mkdtemp(tmp));
  assert(!f_copy(hc.root, sizeof hc.root, tmp));
  nt_path(hc.repo, hc.root, "repo");
  nt_path(hc.home, hc.root, "home");
  nt_path(hc.manifest, hc.root, "data.json");
  nt_path(hc.graph, hc.root, "graph.tsv");
  nt_path(hc.run, hc.root, "run");
  assert(!f_mkdirs(hc.repo) && !f_mkdirs(hc.run));
  hc_write(hc.root, "graph.tsv",
           "step\tproduce\texec\t-\nstep\tconsume\texec\tproduce\n");
  hc_write(hc.run, "graph.tsv",
           "step\tproduce\texec\t-\nstep\tconsume\texec\tproduce\n");
  assert(!setenv("HYDRA_HOME", hc.home, 1) &&
         !setenv("HYDRA_SKIP_AI", "1", 1) &&
         !setenv("HYDRA_NONINTERACTIVE", "1", 1) &&
         !setenv("HYDRA_NO_SWITCH", "1", 1));
  assert(!chdir(hc.repo));
  char *init[] = {"git", "init", "-q", NULL},
       *name[] = {"git", "config", "user.name", "Contract Test", NULL},
       *email[] = {"git", "config", "user.email", "contract@example.invalid",
                   NULL},
       *add[] = {"git", "add", ".", NULL},
       *commit[] = {"git",  "-c", "commit.gpgSign=false", "commit", "-qm",
                    "base", NULL};
  char **args[] = {init, name, email};
  for (size_t i = 0; i < 3; i++) {
    struct f_capture c = hc_command(args[i], true);
    f_capture_free(&c);
  }
  hc_write(hc.repo, "base", "base\n");
  struct f_capture c = hc_command(add, true);
  f_capture_free(&c);
  c = hc_command(commit, true);
  f_capture_free(&c);
}
void hc_cleanup(void) {
  assert(!chdir(hc.origin));
  assert(!f_remove_tree(hc.root));
}
json_object *hc_data(void) {
  return hc_read(hc.origin, "tests/fixtures/plan-9b/data.json");
}
json_object *hc_contract(const char *unit, const char *kind, const char *schema,
                         json_object *fields) {
  json_object *c =
      f_parse("{\"version\":1,\"cardinality\":{\"min\":1,\"max\":1}}");
  f_string_add(c, "schema", schema ? schema : "measurement");
  f_string_add(c, "type", kind ? kind : "object");
  if (!fields) {
    fields =
        f_parse("{\"candidate\":{\"type\":\"string\",\"equals\":\"current\"},"
                "\"duration\":{\"type\":\"integer\",\"minimum\":0}}");
    f_string_add(f_field(fields, "duration"), "unit", unit ? unit : "ms");
  }
  json_object_object_add(c, "fields", fields);
  return c;
}
json_object *hc_declaration(json_object *c, const char *path) {
  if (!c)
    c = hc_contract(NULL, NULL, NULL, NULL);
  json_object *d = f_parse("{\"max_bytes\":4096}");
  f_string_add(d, "path", path ? path : "value.json");
  f_string_add(d, "type", f_string(c, "type"));
  json_object_object_add(d, "contract", c);
  return d;
}
json_object *hc_value(int duration, const char *candidate, const char *unit) {
  json_object *v = json_object_new_object(), *d = json_object_new_object();
  f_string_add(v, "candidate", candidate ? candidate : "current");
  json_object_object_add(d, "value", json_object_new_int(duration));
  f_string_add(d, "unit", unit ? unit : "ms");
  json_object_object_add(v, "duration", d);
  return v;
}
json_object *hc_step(json_object *v, const char *name) {
  return f_field(f_field(v, "steps"), name);
}
void hc_validate(json_object *v, bool ok) {
  nt_json_write(hc.manifest, v);
  hc_native("validate", hc.root, "data.json", hc.graph, NULL, ok);
}
void hc_initialize(json_object *v) {
  hc_validate(v, true);
  hc_native("init", hc.run, hc.repo, hc.root, "data.json", true);
}
void hc_prepare(const char *step, char attempt[F_PATH], bool ok) {
  char relative[128];
  assert(snprintf(relative, sizeof relative, "steps/%s/attempt-1", step) > 0);
  nt_path(attempt, hc.run, relative);
  assert(!f_mkdirs(attempt));
  hc_native("prepare", hc.run, step, attempt, NULL, ok);
}
void hc_seal(const char *step, const char *attempt, bool ok) {
  hc_native("seal", hc.run, step, attempt, NULL, ok);
  if (ok) {
    char dir[F_PATH], name[128];
    assert(snprintf(name, sizeof name, "steps/%s", step) > 0);
    nt_path(dir, hc.run, name);
    hc_write(dir, "state", "succeeded\n");
    hc_write(dir, "authoritative-attempt", "1\n");
  }
}
int main(int argc, char **argv) {
  assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "--runtime")));
  assert(getcwd(hc.origin, sizeof hc.origin));
  nt_path(hc.hydra, hc.origin, "bin/hydra");
  const char *fleet = getenv("HYDRA_FLEET_BIN");
  if (fleet)
    assert(!f_copy(hc.fleet, sizeof hc.fleet, fleet));
  else
    nt_path(hc.fleet, hc.origin, "build/hydra-fleet");
  assert(!setenv("HYDRA_FLEET_BIN", hc.fleet, 1));
  hc_data_cases();
  hc_plan_cases();
  if (argc == 2)
    hc_runtime_cases();
  else
    puts("Supervised runtime controls not requested; run with --runtime in "
         "allocated test slot");
  return 0;
}
