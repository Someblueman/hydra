#include "contracts.h"
#include <glob.h>
static bool head_active;
static const char *names[] = {"compatible", "semantic", "units",
                              "stale",      "lossy",    "composition"};
static char produce[F_PATH], consume[F_PATH], convert[F_PATH], flow[F_PATH],
    marker[F_PATH];
static void stop_head(void) {
  if (head_active) {
    char *args[] = {hc.hydra, "kill", "contract-worker", "--force", NULL};
    struct f_capture c = nt_run(args);
    f_capture_free(&c);
    head_active = false;
  }
}
static bool exists(const char *dir, const char *name) {
  char p[F_PATH];
  nt_path(p, dir, name);
  return !access(p, F_OK);
}
static void state_is(const char *dir, const char *name, const char *expected) {
  char p[F_PATH];
  nt_path(p, dir, name);
  char *v = f_read(p, 100000);
  assert(v && !strcmp(v, expected));
  free(v);
}
static void runtime_setup(void) {
  hc_setup();
  char *init[] = {hc.hydra, "init", "--no-agent", "--trust", NULL},
       *spawn[] = {hc.hydra, "spawn", "contract-worker", "--no-agent", NULL};
  struct f_capture c = hc_command(init, true);
  f_capture_free(&c);
  c = hc_command(spawn, true);
  f_capture_free(&c);
  head_active = true;
  assert(!atexit(stop_head));
  nt_path(produce, hc.root, "produce.sh");
  nt_path(consume, hc.root, "consume.sh");
  nt_path(convert, hc.root, "convert.sh");
  nt_path(flow, hc.root, "flow.yml");
  nt_path(marker, hc.root, "consumed");
  nt_write(produce, "#!/bin/sh\nset -eu\ncp \"$1/payload.json\" "
                    "\"$HYDRA_WORKFLOW_OUTPUTS_DIR/value.json\"\nif [ -f "
                    "\"$1/other.json\" ]; then cp \"$1/other.json\" "
                    "\"$HYDRA_WORKFLOW_OUTPUTS_DIR/other.json\"; fi\n");
  nt_write(consume, "#!/bin/sh\nset -eu\nprintf received > \"$1/consumed\"\n");
  nt_write(convert, "#!/bin/sh\nset -eu\ncp \"$1/converted.json\" "
                    "\"$HYDRA_WORKFLOW_OUTPUTS_DIR/value.json\"\n");
}
static void lossy_data(json_object *m, json_object *payload) {
  json_object_object_add(f_field(payload, "duration"), "value",
                         json_object_new_int(1500));
  json_object *s = f_parse(
      "{\"inputs\":{\"value\":{\"step\":\"produce\",\"output\":\"value\"}},"
      "\"outputs\":{},\"conversion\":{\"input\":\"value\",\"output\":\"value\","
      "\"field\":\"duration\",\"numerator\":1,\"denominator\":1000}}");
  json_object_object_add(f_field(f_field(s, "inputs"), "value"), "contract",
                         hc_contract(NULL, NULL, NULL, NULL));
  json_object_object_add(
      f_field(s, "outputs"), "value",
      hc_declaration(hc_contract("s", NULL, NULL, NULL), NULL));
  json_object_object_add(f_field(m, "steps"), "convert", s);
  json_object *ref = f_parse("{\"step\":\"convert\",\"output\":\"value\"}");
  json_object_object_add(ref, "contract", hc_contract("s", NULL, NULL, NULL));
  json_object_object_add(f_field(hc_step(m, "consume"), "inputs"), "value",
                         ref);
  json_object *v = hc_value(1, NULL, "s");
  hc_json(hc.root, "converted.json", v);
  json_object_put(v);
}
static void composition_data(json_object *m) {
  json_object_object_add(f_field(hc_step(m, "produce"), "outputs"), "other",
                         hc_declaration(NULL, "other.json"));
  json_object *ref = f_parse("{\"step\":\"produce\",\"output\":\"other\"}");
  json_object_object_add(ref, "contract", hc_contract(NULL, NULL, NULL, NULL));
  json_object_object_add(f_field(hc_step(m, "consume"), "inputs"), "other",
                         ref);
  json_object_object_add(
      hc_step(m, "consume"), "invariants",
      f_parse_value("[{\"phase\":\"before\",\"op\":\"equal\",\"left\":{"
                    "\"input\":\"value\",\"field\":\"duration\"},\"right\":{"
                    "\"input\":\"other\",\"field\":\"duration\"}}]"));
  json_object *v = hc_value(999, NULL, NULL);
  hc_json(hc.root, "other.json", v);
  json_object_put(v);
}
static void case_data(size_t i) {
  json_object *m = hc_data(), *payload = hc_value(1000, NULL, NULL);
  switch (i) {
  case 1:
    json_object_object_del(payload, "duration");
    break;
  case 2:
    f_string_add(f_field(payload, "duration"), "unit", "s");
    break;
  case 3:
    f_string_add(payload, "candidate", "previous");
    break;
  case 4:
    lossy_data(m, payload);
    break;
  case 5:
    composition_data(m);
    break;
  default:
    break;
  }
  hc_json(hc.root, "payload.json", payload);
  json_object_put(payload);
  nt_json_write(hc.manifest, m);
  json_object_put(m);
}
static void case_flow(size_t i) {
  FILE *file = fopen(flow, "w");
  assert(file);
  assert(fprintf(file,
                 "version: 1\nid: contracts-%s\ndata: data.json\nresources:\n  "
                 "disk_mb: 1\nsteps:\n",
                 names[i]) > 0);
  const char *ids[] = {"produce", "convert", "consume"},
             *scripts[] = {produce, convert, consume},
             *needs[] = {"", "produce", i == 4 ? "convert" : "produce"};
  for (size_t n = 0; n < 3; n++) {
    if (n == 1 && i != 4)
      continue;
    assert(fprintf(file,
                   "  - id: %s\n    kind: exec\n    needs: [%s]\n    "
                   "idempotent: false\n    args:\n      head: "
                   "contract-worker\n      argv: [sh, %s, %s]\n",
                   ids[n], needs[n], scripts[n], hc.root) > 0);
  }
  assert(!fclose(file));
}
static void find_run(size_t i, char match[F_PATH]) {
  char pattern[F_PATH], needle[128];
  nt_path(pattern, hc.home, "state/v2/projects/*/workflows/runs/*");
  assert(snprintf(needle, sizeof needle, "contracts-%s", names[i]) > 0);
  glob_t paths = {0};
  assert(!glob(pattern, 0, NULL, &paths));
  size_t matches = 0;
  for (size_t j = 0; j < paths.gl_pathc; j++) {
    char p[F_PATH];
    nt_path(p, paths.gl_pathv[j], "resolved.yml");
    char *s = f_read(p, 1000000);
    if (s && strstr(s, needle)) {
      assert(!f_copy(match, F_PATH, paths.gl_pathv[j]));
      matches++;
    }
    free(s);
  }
  globfree(&paths);
  assert(matches == 1);
}
static void rejected_before_consumption(size_t i) {
  char match[F_PATH], consumer[F_PATH], pattern[F_PATH];
  find_run(i, match);
  nt_path(consumer, match, "steps/consume");
  assert(!exists(consumer, "command-pid"));
  nt_path(pattern, consumer, "attempt-*/stdout");
  glob_t paths = {0};
  int status = glob(pattern, 0, NULL, &paths);
  assert(status == GLOB_NOMATCH && paths.gl_pathc == 0);
  globfree(&paths);
  if (i != 5) {
    char failed[F_PATH];
    nt_path(failed, match, i == 4 ? "steps/convert" : "steps/produce");
    state_is(failed, "state", "failed\n");
    json_object *seal = hc_read(failed, "attempt-1/data-seal.json");
    assert(!strcmp(f_string(f_field(seal, "error"), "code"), "invalid_data"));
    json_object_put(seal);
  } else {
    state_is(match, "steps/produce/state", "succeeded\n");
    state_is(consumer, "state", "failed\n");
    json_object *preparation =
        hc_read(consumer, "attempt-1/data-preparation.json");
    assert(!strcmp(f_string(f_field(preparation, "error"), "code"),
                   "invalid_data"));
    json_object_put(preparation);
  }
}
void hc_runtime_cases(void) {
  runtime_setup();
  for (size_t i = 0; i < 6; i++) {
    case_data(i);
    case_flow(i);
    if (!access(marker, F_OK))
      assert(!unlink(marker));
    char *validate[] = {hc.hydra, "workflow", "validate", flow, NULL},
         *run[] = {hc.hydra, "workflow", "run", flow, NULL};
    struct f_capture c = hc_command(validate, true);
    f_capture_free(&c);
    c = hc_command(run, i == 0);
    f_capture_free(&c);
    assert((access(marker, F_OK) == 0) == (i == 0));
    if (i)
      rejected_before_consumption(i);
    printf("Supervised handoff %s passed\n", names[i]);
  }
  stop_head();
  hc_cleanup();
}
