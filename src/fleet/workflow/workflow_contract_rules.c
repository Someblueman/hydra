#include "fleet/workflow/workflow_contract.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <string.h>

json_object *wc_declaration(json_object *manifest, json_object *ref) {
    const char *input = f_string(ref, "input");
    if (!input && (!wd_name(f_string(ref, "step")) || !wd_name(f_string(ref, "output")))) return NULL;
    if (input) return f_field(f_field(manifest, "inputs"), input);
    return f_field(f_field(f_field(f_field(manifest, "steps"), f_string(ref, "step")), "outputs"), f_string(ref, "output"));
}
static bool contracts(json_object *map) {
    if (!map) return true;
    json_object_object_foreach(map, name, decl) {
        (void)name;
        if (!wc_schema(f_field(decl, "contract"), f_string(decl, "type"))) return false;
    }
    return true;
}
static bool compatible(json_object *manifest, json_object *inputs) {
    if (!inputs) return true;
    json_object_object_foreach(inputs, name, ref) {
        (void)name;
        if (f_field(ref, "validation") || f_field(ref, "repair") || f_field(ref, "provenance")) continue;
        json_object *decl = wc_declaration(manifest, ref);
        if (!json_object_equal(f_field(ref, "contract"), f_field(decl, "contract"))) return false;
    }
    return true;
}
static json_object *selection(json_object *manifest, json_object *step, json_object *sel, bool after) {
    const char *const keys[] = {"input", "output", "field", NULL};
    const char *input = f_string(sel, "input"), *output = f_string(sel, "output");
    if (!task_keys(sel, keys) || json_object_object_length(sel) != 2 || (!!input == !!output) || !wd_name(input ? input : output) || !wd_name(f_string(sel, "field"))) return NULL;
    json_object *decl = input ? wc_declaration(manifest, f_field(f_field(step, "inputs"), input)) : f_field(f_field(step, "outputs"), output);
    if ((output && !after) || !f_string(decl, "type") || strcmp(f_string(decl, "type"), "object")) return NULL;
    return f_field(f_field(f_field(decl, "contract"), "fields"), f_string(sel, "field"));
}
static bool same_dimension(json_object *left, json_object *right) {
    return left && right && json_object_equal(f_field(left, "type"), f_field(right, "type")) &&
        json_object_equal(f_field(left, "unit"), f_field(right, "unit"));
}
static bool invariant(json_object *manifest, json_object *step, json_object *rule) {
    const char *const keys[] = {"phase", "op", "left", "right", NULL};
    const char *phase = f_string(rule, "phase"), *op = f_string(rule, "op");
    if (!task_keys(rule, keys) || !phase || (strcmp(phase, "before") && strcmp(phase, "after")) || !op) return false;
    bool after = !strcmp(phase, "after");
    json_object *right = selection(manifest, step, f_field(rule, "right"), after), *left = f_field(rule, "left");
    if (!strcmp(op, "equal")) return same_dimension(selection(manifest, step, left, after), right);
    if (strcmp(op, "sum") || !right || strcmp(f_string(right, "type"), "integer") || !plan_list(left, 2, 16)) return false;
    for (size_t i = 0; i < json_object_array_length(left); i++)
        if (!same_dimension(selection(manifest, step, json_object_array_get_idx(left, i), after), right)) return false;
    return true;
}
static bool conversion_fields(json_object *a, json_object *b, const char *name) {
    if (json_object_object_length(a) != json_object_object_length(b) || !f_field(a, name)) return false;
    json_object_object_foreach(a, field, spec) {
        json_object *other = f_field(b, field);
        if (strcmp(field, name)) { if (!json_object_equal(spec, other)) return false; }
        else if (!other || strcmp(f_string(spec, "type"), "integer") || strcmp(f_string(other, "type"), "integer") ||
            !f_string(spec, "unit") || !f_string(other, "unit")) return false;
    }
    return true;
}
static bool conversion(json_object *manifest, json_object *step, json_object *rule) {
    const char *const keys[] = {"input", "output", "field", "numerator", "denominator", NULL};
    const char *name = f_string(rule, "field");
    if (!task_keys(rule, keys) || !wd_name(name) || !wd_name(f_string(rule, "input")) || !wd_name(f_string(rule, "output"))) return false;
    const char *ratios[] = {"numerator", "denominator"};
    for (size_t i = 0; i < 2; i++) if (!json_object_is_type(f_field(rule, ratios[i]), json_type_int) ||
        json_object_get_int64(f_field(rule, ratios[i])) < 1 || json_object_get_int64(f_field(rule, ratios[i])) > 1000000) return false;
    json_object *in = wc_declaration(manifest, f_field(f_field(step, "inputs"), f_string(rule, "input")));
    json_object *out = f_field(f_field(step, "outputs"), f_string(rule, "output"));
    if (!f_string(in, "type") || !f_string(out, "type") || strcmp(f_string(in, "type"), "object") || strcmp(f_string(out, "type"), "object")) return false;
    json_object *a = f_field(f_field(in, "contract"), "fields"), *b = f_field(f_field(out, "contract"), "fields");
    return conversion_fields(a, b, name);
}
static bool candidate_binding(json_object *step, json_object *binding, bool after) {
    const char *const keys[] = {"kind", "input", "output", NULL};
    const char *kind = f_string(binding, "kind"), *in = f_string(binding, "input"), *out = f_string(binding, "output");
    if (!task_keys(binding, keys) || json_object_object_length(binding) != 2 || !kind || (!!in == !!out) || !wd_name(in ? in : out)) return false;
    if (strcmp(kind, "dependency") && strcmp(kind, "configuration") && strcmp(kind, "artifact")) return false;
    if (out && (!after || strcmp(kind, "artifact"))) return false;
    return in ? f_field(f_field(step, "inputs"), in) != NULL : f_field(f_field(step, "outputs"), out) != NULL;
}
static unsigned binding_count(json_object *bindings, const char *name, const char *side) {
    unsigned count = 0;
    json_object_object_foreach(bindings, field, binding) {
        (void)field; const char *target = f_string(binding, side);
        if (target && !strcmp(target, name)) count++;
    }
    return count;
}
static bool covered_map(json_object *map, json_object *bindings, const char *side, const char *source, const char *manifest) {
    if (!map) return true;
    json_object_object_foreach(map, name, ref) {
        (void)ref;
        if (source && !strcmp(name, source)) continue;
        if (manifest && !strcmp(name, manifest)) continue;
        if (binding_count(bindings, name, side) != 1) return false;
    }
    return true;
}
static bool candidate_coverage(json_object *step, json_object *rule, bool after) {
    const char *source = f_string(rule, "source"), *manifest = f_string(rule, "manifest");
    json_object *bindings = f_field(rule, "bindings");
    if (!covered_map(f_field(step, "inputs"), bindings, "input", source, after ? NULL : manifest)) return false;
    return !after || covered_map(f_field(step, "outputs"), bindings, "output", NULL, manifest);
}
static bool candidate_fields(json_object *fields, json_object *bindings) {
    if (!json_object_is_type(bindings, json_type_object) || json_object_object_length(fields) != json_object_object_length(bindings) + 2) return false;
    const char *source_fields[] = {"source_commit", "source_sha256"};
    for (size_t i = 0; i < 2; i++) {
        json_object *field = f_field(fields, source_fields[i]);
        if (!field || strcmp(f_string(field, "type"), "string") || f_field(bindings, source_fields[i])) return false;
    }
    json_object_object_foreach(bindings, field, binding) {
        (void)binding; json_object *spec = f_field(fields, field);
        if (!spec || strcmp(f_string(spec, "type"), "string")) return false;
    }
    return true;
}
static bool candidate_bindings(json_object *step, json_object *rule, bool after) {
    const char *source = f_string(rule, "source"), *name = f_string(rule, "manifest");
    json_object_object_foreach(f_field(rule, "bindings"), field, binding) {
        (void)field;
        if (!candidate_binding(step, binding, after)) return false;
        const char *in = f_string(binding, "input"), *out = f_string(binding, "output");
        if (in && (!strcmp(in, source) || (!after && !strcmp(in, name)))) return false;
        if (out && !strcmp(out, name)) return false;
    }
    return true;
}
static bool candidate(json_object *manifest, json_object *step, json_object *rule) {
    const char *const keys[] = {"phase", "manifest", "source", "bindings", NULL};
    const char *phase = f_string(rule, "phase"), *name = f_string(rule, "manifest"), *source = f_string(rule, "source");
    if (!task_keys(rule, keys) || !phase || (strcmp(phase, "before") && strcmp(phase, "after")) || !wd_name(name) || !wd_name(source)) return false;
    bool after = !strcmp(phase, "after");
    if (!f_field(f_field(f_field(step, "inputs"), source), "provenance")) return false;
    json_object *decl = after ? f_field(f_field(step, "outputs"), name) : wc_declaration(manifest, f_field(f_field(step, "inputs"), name));
    json_object *fields = f_field(f_field(decl, "contract"), "fields"), *bindings = f_field(rule, "bindings");
    if (!f_string(decl, "type") || strcmp(f_string(decl, "type"), "object") || !candidate_fields(fields, bindings)) return false;
    return candidate_bindings(step, rule, after) && candidate_coverage(step, rule, after);
}
static bool step_rules(json_object *manifest, json_object *step, bool candidates) {
    json_object *rows = f_field(step, candidates ? "candidates" : "invariants");
    if (!rows) return true;
    if (!plan_list(rows, 1, 16)) return false;
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!(candidates ? candidate(manifest, step, row) : invariant(manifest, step, row))) return false;
    }
    return true;
}
static bool output_contracts(json_object *steps) {
    json_object_object_foreach(steps, id, row) {
        (void)id; if (!contracts(f_field(row, "outputs"))) return false;
    }
    return true;
}
static bool step_contracts(json_object *manifest, json_object *step) {
    if (!compatible(manifest, f_field(step, "inputs"))) return false;
    if (!step_rules(manifest, step, false) || !step_rules(manifest, step, true)) return false;
    json_object *rule = f_field(step, "conversion");
    return !rule || conversion(manifest, step, rule);
}
static bool defined_values(json_object *value);
static bool defined_array(json_object *value) {
    for (size_t i = 0; i < json_object_array_length(value); i++)
        if (!defined_values(json_object_array_get_idx(value, i))) return false;
    return true;
}
/* Null has no meaning in this opt-in contract language, even for optional keys. */
static bool defined_values(json_object *value) {
    if (!value) return false;
    if (json_object_is_type(value, json_type_string)) return strlen(f_text(value)) == (size_t)json_object_get_string_len(value);
    if (json_object_is_type(value, json_type_object)) {
        json_object_object_foreach(value, name, child) {
            (void)name; if (!defined_values(child)) return false;
        }
        return true;
    }
    if (json_object_is_type(value, json_type_array)) return defined_array(value);
    return true;
}
bool wc_manifest(json_object *manifest) {
    if (!f_number_is(manifest, "schema_version", 2)) return true;
    json_object *steps = f_field(manifest, "steps");
    if (!defined_values(manifest) || !contracts(f_field(manifest, "inputs")) || !output_contracts(steps)) return false;
    json_object_object_foreach(steps, name, step) {
        (void)name; if (!step_contracts(manifest, step)) return false;
    }
    return true;
}
