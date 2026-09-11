#include "outcome_support.h"
static void compilation(struct outcome_fixture *f) {
  char paths[2][F_PATH];
  const char *names[] = {"serial", "forkjoin"};
  for (size_t i = 0; i < 2; i++) {
    char name[64];
    assert(snprintf(name, sizeof name, "%s.compiled.json", names[i]) > 0);
    nt_path(paths[i], f->folder, name);
    assert(snprintf(name, sizeof name, "%s.json", names[i]) > 0);
    json_object_put(
        outcome_cli(f, "compile", name, "policy.json", paths[i], 0));
    json_object *v = outcome_cli(f, "explain", paths[i], NULL, NULL, 0);
    assert(json_object_array_length(f_field(f_field(v, "data"), "nodes")) == 8);
    json_object_put(v);
  }
  json_object *v = outcome_cli(f, "compare", paths[0], paths[1], NULL, 0),
              *d = f_field(v, "data");
  nt_equal(f_field(d, "matched_scope"), "true");
  assert(json_object_get_int(f_field(f_field(d, "left"), "edges")) ==
         json_object_get_int(f_field(f_field(d, "right"), "edges")) + 1);
  assert(!strcmp(f_string(d, "modeled_preference"), "unresolved"));
  json_object_put(v);
  v = f_read_json("serial.json", 1000000);
  assert(v);
  f_string_add(
      f_field(f_field(f_field(f_field(f_field(v, "data"), "steps"), "compose"),
                      "inputs"),
              "a"),
      "output", "missing");
  nt_json_write("serial.json", v);
  json_object_put(v);
  json_object_put(
      outcome_cli(f, "validate", "serial.json", "policy.json", NULL, 1));
}
static void artifacts(struct outcome_fixture *f) {
  const char *contents[] = {"A:validated input\nB:validated input\n",
                            "A:validated input\nB:wrong\n",
                            "A:validated input\n",
                            "B:validated input\nA:validated input\n",
                            "A:validated input\nB:validated input",
                            "A:validated input\nB:validated input\nextra\n",
                            ""};
  char subject[F_PATH], check[F_PATH];
  nt_path(subject, f->inputs, "subject");
  nt_path(check, f->outputs, "check");
  for (size_t i = 0; i < 7; i++) {
    nt_write(subject, contents[i]);
    outcome_check("pattern-check", i ? 1 : 0);
    json_object *v = f_read_json(check, 1000000);
    assert(v);
    assert(!strcmp(f_string(v, "verdict"), i ? "fail" : "pass") &&
           !strcmp(f_string(v, "evidence_status"), "valid"));
    nt_equal(
        f_field(json_object_array_get_idx(f_field(v, "evidence_records"), 0),
                "counts"),
        i ? "{\"executed\":1,\"failed\":1,\"skipped\":0}"
          : "{\"executed\":1,\"failed\":0,\"skipped\":0}");
    json_object_put(v);
  }
}
int main(void) {
  struct outcome_fixture f;
  outcome_setup(&f, "patterns");
  compilation(&f);
  artifacts(&f);
  outcome_finish(&f, "Patterns: all three original case groups passed");
  return 0;
}
