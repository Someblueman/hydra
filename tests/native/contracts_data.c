#include "contracts.h"
static json_object *input_contract(json_object *v) {
  return f_field(f_field(f_field(hc_step(v, "consume"), "inputs"), "value"),
                 "contract");
}
static void output_value(const char *attempt, json_object *v) {
  hc_json(attempt, "outputs/value.json", v);
  json_object_put(v);
}
static void compatible(void) {
  json_object *v = hc_data();
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  v = hc_value(1000, NULL, NULL);
  hc_json(p, "outputs/value.json", v);
  hc_seal("produce", p, true);
  hc_prepare("consume", c, true);
  json_object *actual = hc_read(c, "inputs/value");
  assert(json_object_equal(v, actual));
  json_object_put(v);
  json_object_put(actual);
  hc_native("verify-output", hc.run, "produce", p, NULL, true);
}
static void semantic(void) {
  const char *names[] = {
      "empty",   "semantic",         "units",       "stale", "unknown",
      "minimum", "integer-overflow", "empty-array", "many"};
  for (size_t i = 0; i < 9; i++) {
    char c[F_PATH];
    nt_path(c, hc.root, names[i]);
    assert(!f_mkdirs(c));
    json_object *payload = hc_value(1000, NULL, NULL);
    if (i == 0) {
      json_object_put(payload);
      payload = json_object_new_object();
    } else if (i == 1) {
      json_object_object_del(payload, "candidate");
      json_object_object_add(f_field(payload, "duration"), "value",
                             json_object_new_int(1));
    } else if (i == 2)
      f_string_add(f_field(payload, "duration"), "unit", "s");
    else if (i == 3)
      f_string_add(payload, "candidate", "old");
    else if (i == 4)
      json_object_object_add(payload, "extra", json_object_new_int(1));
    else if (i == 5)
      json_object_object_add(f_field(payload, "duration"), "value",
                             json_object_new_int(-1));
    else if (i >= 7) {
      json_object_put(payload);
      payload = json_object_new_array();
      if (i == 8) {
        json_object_array_add(payload, hc_value(1000, NULL, NULL));
        json_object_array_add(payload, hc_value(1000, NULL, NULL));
      }
    }
    if (i == 6)
      hc_write(c, "value.json",
               "{\"candidate\":\"current\",\"duration\":{\"value\":"
               "18446744073709551616,\"unit\":\"ms\"}}");
    else
      hc_json(c, "value.json", payload);
    json_object_put(payload);
    json_object *manifest =
        f_parse("{\"schema_version\":2,\"inputs\":{},\"steps\":{}}");
    json_object_object_add(
        f_field(manifest, "inputs"), "value",
        hc_declaration(
            hc_contract(NULL, i >= 7 ? "array" : "object", NULL, NULL), NULL));
    nt_json_write(hc.manifest, manifest);
    json_object_put(manifest);
    hc_write(c, "graph.tsv",
             "step\tproduce\texec\t-\nstep\tconsume\texec\tproduce\n");
    hc_native("init", c, c, hc.root, "data.json", false);
  }
}
static void incompatible(void) {
  for (size_t i = 0; i < 5; i++) {
    json_object *v = hc_data(), *c = input_contract(v);
    if (i == 0)
      f_string_add(f_field(f_field(c, "fields"), "duration"), "unit", "s");
    else if (i == 1)
      json_object_object_add(c, "version", json_object_new_int(2));
    else if (i == 2)
      f_string_add(c, "type", "array");
    else if (i == 3) {
      f_string_add(f_field(f_field(c, "fields"), "candidate"), "regex", ".*");
      json_object_object_add(
          f_field(f_field(hc_step(v, "produce"), "outputs"), "value"),
          "contract", nt_clone(c));
    } else
      json_object_object_del(
          f_field(f_field(hc_step(v, "consume"), "inputs"), "value"),
          "contract");
    hc_validate(v, false);
    json_object_put(v);
  }
}
static void conversion(bool lossy) {
  json_object *v = hc_data(), *outputs = json_object_new_object();
  json_object_object_add(
      outputs, "converted",
      hc_declaration(hc_contract("s", NULL, lossy ? "seconds" : NULL, NULL),
                     NULL));
  json_object_object_add(hc_step(v, "consume"), "outputs", outputs);
  json_object_object_add(
      hc_step(v, "consume"), "conversion",
      f_parse("{\"input\":\"value\",\"output\":\"converted\",\"field\":"
              "\"duration\",\"numerator\":1,\"denominator\":1000}"));
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  output_value(p, hc_value(lossy ? 1500 : 2000, NULL, NULL));
  hc_seal("produce", p, true);
  hc_prepare("consume", c, true);
  output_value(c, hc_value(lossy ? 1 : 2, NULL, "s"));
  hc_seal("consume", c, !lossy);
}
static void lossy(void) { conversion(true); }
static void lossless(void) { conversion(false); }
static void shared(void) {
  json_object *v = hc_data();
  json_object_object_add(f_field(hc_step(v, "produce"), "outputs"), "other",
                         hc_declaration(NULL, "other.json"));
  json_object *ref = f_parse("{\"step\":\"produce\",\"output\":\"other\"}");
  json_object_object_add(ref, "contract", hc_contract(NULL, NULL, NULL, NULL));
  json_object_object_add(f_field(hc_step(v, "consume"), "inputs"), "other",
                         ref);
  json_object_object_add(
      hc_step(v, "consume"), "invariants",
      f_parse_value("[{\"phase\":\"before\",\"op\":\"equal\",\"left\":{"
                    "\"input\":\"value\",\"field\":\"duration\"},\"right\":{"
                    "\"input\":\"other\",\"field\":\"duration\"}}]"));
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  output_value(p, hc_value(1, NULL, NULL));
  v = hc_value(2, NULL, NULL);
  hc_json(p, "outputs/other.json", v);
  json_object_put(v);
  hc_seal("produce", p, true);
  hc_prepare("consume", c, false);
}
static void postcondition(void) {
  json_object *v = hc_data(), *outputs = json_object_new_object();
  json_object_object_add(outputs, "total", hc_declaration(NULL, NULL));
  json_object_object_add(hc_step(v, "consume"), "outputs", outputs);
  json_object_object_add(
      hc_step(v, "consume"), "invariants",
      f_parse_value(
          "[{\"phase\":\"after\",\"op\":\"sum\",\"left\":[{\"input\":\"value\","
          "\"field\":\"duration\"},{\"input\":\"value\",\"field\":\"duration\"}"
          "],\"right\":{\"output\":\"total\",\"field\":\"duration\"}}]"));
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  output_value(p, hc_value(1, NULL, NULL));
  hc_seal("produce", p, true);
  hc_prepare("consume", c, true);
  output_value(c, hc_value(3, NULL, NULL));
  hc_seal("consume", c, false);
}
static void missing_input(void) {
  json_object *v = hc_data();
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  output_value(p, hc_value(1000, NULL, NULL));
  hc_seal("produce", p, true);
  hc_prepare("consume", c, true);
  hc_write(c, "inputs/value", "{}");
  hc_seal("consume", c, false);
}
static void legacy(void) {
  json_object *v = hc_data();
  json_object_object_add(v, "schema_version", json_object_new_int(1));
  hc_validate(v, false);
  json_object_put(v);
}
static void null_duplicate(void) {
  const char *keys[] = {"invariants", "candidates", "conversion"};
  for (size_t i = 0; i < 3; i++) {
    json_object *v = hc_data();
    json_object_object_add(hc_step(v, "consume"), keys[i], NULL);
    hc_validate(v, false);
    json_object_put(v);
  }
  json_object *v = hc_data();
  json_object_object_add(
      f_field(f_field(f_field(f_field(f_field(hc_step(v, "produce"), "outputs"),
                                      "value"),
                              "contract"),
                      "fields"),
              "candidate"),
      "equals", NULL);
  hc_validate(v, false);
  json_object_put(v);
  v = hc_data();
  char *text = nt_replace(
      json_object_to_json_string_ext(v, JSON_C_TO_STRING_PLAIN),
      "\"schema_version\":2", "\"schema_version\":2,\"schema_version\":2");
  nt_write(hc.manifest, text);
  free(text);
  hc_native("validate", hc.root, "data.json", hc.graph, NULL, false);
  json_object_put(v);
  v = hc_data();
  json_object_object_add(
      f_field(f_field(hc_step(v, "consume"), "inputs"), "value"), "provenance",
      json_object_new_boolean(false));
  hc_validate(v, false);
  json_object_put(v);
  v = hc_data();
  json_object_object_add(
      f_field(f_field(hc_step(v, "consume"), "inputs"), "value"), "step",
      json_object_new_string_len("produce\0stale", 13));
  hc_validate(v, false);
  json_object_put(v);
  v = hc_data();
  text = nt_replace(json_object_to_json_string_ext(v, JSON_C_TO_STRING_PLAIN),
                    "\"schema\":", "\"schema\\u0000alias\":");
  nt_write(hc.manifest, text);
  free(text);
  json_object_put(v);
  hc_native("validate", hc.root, "data.json", hc.graph, NULL, false);
}
static void duplicate_value(void) {
  json_object *v = hc_data();
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH];
  hc_prepare("produce", p, true);
  hc_write(p, "outputs/value.json",
           "{\"candidate\":\"previous\",\"candidate\":\"current\",\"duration\":"
           "{\"value\":1,\"unit\":\"ms\"}}");
  hc_seal("produce", p, false);
}
static void evidence(bool missing) {
  json_object *v = hc_data(),
              *schema =
                  hc_contract(NULL, missing ? "object" : "array", NULL,
                              f_parse("{\"evidence\":{\"type\":\"strings\","
                                      "\"equals\":[\"test\",\"source\"]}}"));
  json_object_object_add(f_field(hc_step(v, "produce"), "outputs"), "value",
                         hc_declaration(nt_clone(schema), NULL));
  json_object_object_add(
      f_field(f_field(hc_step(v, "consume"), "inputs"), "value"), "contract",
      schema);
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  hc_write(p, "outputs/value.json",
           missing ? "{\"evidence\":[\"test\"]}"
                   : "[{\"evidence\":[\"test\",\"source\"]}]");
  hc_seal("produce", p, !missing);
  if (!missing)
    hc_prepare("consume", c, true);
}
static void array_evidence(void) { evidence(false); }
static void missing_evidence(void) { evidence(true); }
static void provenance(void) {
  json_object *v = hc_data();
  json_object_object_add(
      f_field(hc_step(v, "consume"), "inputs"), "source",
      f_parse("{\"provenance\":\"step\",\"step\":\"produce\"}"));
  const char *rows = "step\tproduce\ttask\t-\nstep\tconsume\texec\tproduce\n";
  nt_write(hc.graph, rows);
  hc_write(hc.run, "graph.tsv", rows);
  hc_initialize(v);
  json_object_put(v);
  char p[F_PATH], c[F_PATH];
  hc_prepare("produce", p, true);
  output_value(p, hc_value(1000, NULL, NULL));
  hc_seal("produce", p, true);
  hc_prepare("consume", c, false);
}
void hc_data_cases(void) {
  void (*cases[])(void) = {
      compatible,       semantic,       incompatible,    lossy,
      lossless,         shared,         postcondition,   missing_input,
      legacy,           null_duplicate, duplicate_value, array_evidence,
      missing_evidence, provenance};
  for (size_t i = 0; i < 14; i++) {
    hc_setup();
    cases[i]();
    hc_cleanup();
    printf("Handoff data case %zu passed\n", i + 1);
  }
}
