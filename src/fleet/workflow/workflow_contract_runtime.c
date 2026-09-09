#include "fleet/workflow/workflow_contract.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <string.h>

static json_object *read_value(const char *attempt, const char *directory, const char *name) {
    char path[F_PATH];
    if (!wd_name(name) || snprintf(path, sizeof(path), "%s/%s/%s", attempt, directory, name) >= (int)sizeof(path)) return NULL;
    return f_read_json(path, WD_LIMIT);
}
static json_object *selected(const char *attempt, json_object *sel) {
    const char *input = f_string(sel, "input");
    json_object *obj = read_value(attempt, input ? "inputs" : "artifacts", input ? input : f_string(sel, "output"));
    json_object *value = json_object_get(f_field(obj, f_string(sel, "field")));
    json_object_put(obj); return value;
}
static json_object *magnitude(json_object *value) {
    return json_object_is_type(value, json_type_object) ? f_field(value, "value") : value;
}
static bool invariant(const char *attempt, json_object *rule) {
    json_object *right = selected(attempt, f_field(rule, "right")), *left = f_field(rule, "left"); bool ok = false;
    if (!right) return false;
    if (!strcmp(f_string(rule, "op"), "equal")) {
        json_object *value = selected(attempt, left);
        ok = value && json_object_equal(value, right); json_object_put(value);
    } else {
        int64_t sum = 0; ok = true;
        for (size_t i = 0; i < json_object_array_length(left); i++) {
            json_object *value = selected(attempt, json_object_array_get_idx(left, i));
            if (!value || __builtin_add_overflow(sum, json_object_get_int64(magnitude(value)), &sum)) ok = false;
            json_object_put(value);
        }
        ok = ok && sum == json_object_get_int64(magnitude(right));
    }
    json_object_put(right); return ok;
}
static bool conversion(const char *attempt, json_object *rule) {
    json_object *in = read_value(attempt, "inputs", f_string(rule, "input"));
    json_object *out = read_value(attempt, "artifacts", f_string(rule, "output")); bool ok = false;
    const char *field = f_string(rule, "field"); int64_t scaled;
    if (!in || !out) goto done;
    int64_t before = json_object_get_int64(magnitude(f_field(in, field)));
    int64_t after = json_object_get_int64(magnitude(f_field(out, field)));
    int64_t divisor = json_object_get_int64(f_field(rule, "denominator"));
    if (__builtin_mul_overflow(before, json_object_get_int64(f_field(rule, "numerator")), &scaled) ||
        scaled % divisor || scaled / divisor != after) goto done;
    json_object_object_foreach(in, name, value) {
        if (strcmp(name, field) && !json_object_equal(value, f_field(out, name))) goto done;
    }
    ok = true;
done:
    json_object_put(in); json_object_put(out); return ok;
}
static bool candidate_files(const char *attempt, json_object *manifest, json_object *bindings) {
    json_object_object_foreach(bindings, field, binding) {
        char path[F_PATH], digest[65]; const char *input = f_string(binding, "input"), *expected = f_string(manifest, field);
        if (!expected || snprintf(path, sizeof(path), "%s/%s/%s", attempt, input ? "inputs" : "artifacts", input ? input : f_string(binding, "output")) >= (int)sizeof(path) ||
            f_hash(path, digest) || strcmp(expected, digest)) return false;
    }
    return true;
}
static bool candidate(const char *attempt, json_object *rule, bool after) {
    json_object *manifest = read_value(attempt, after ? "artifacts" : "inputs", f_string(rule, "manifest"));
    json_object *source = read_value(attempt, "inputs", f_string(rule, "source")); bool ok = false;
    const char *fields[] = {"source_commit", "source_sha256"};
    if (!manifest || !source) goto done;
    for (size_t i = 0; i < 2; i++) if (!f_field(source, fields[i]) || !json_object_equal(f_field(manifest, fields[i]), f_field(source, fields[i]))) goto done;
    ok = candidate_files(attempt, manifest, f_field(rule, "bindings"));
done:
    json_object_put(manifest); json_object_put(source); return ok;
}
/* Fill a caller-owned receipt with hashes of all selected input bytes. */
static bool input_hashes(json_object *inputs, const char *attempt, json_object *receipt) {
    if (!inputs) return true;
    json_object_object_foreach(inputs, name, ref) {
        char path[F_PATH], digest[65]; (void)ref;
        if (snprintf(path, sizeof(path), "%s/inputs/%s", attempt, name) >= (int)sizeof(path) || f_hash(path, digest)) return false;
        f_string_add(receipt, name, digest);
    }
    return true;
}
/* Retain hashes of every materialized input, including generated provenance.
 * Composition checks must not trust inputs modified by the executing command. */
static bool input_receipt(json_object *step, const char *attempt, bool after) {
    json_object *receipt = json_object_new_object(), *stored = NULL; bool ok = false;
    if (!input_hashes(f_field(step, "inputs"), attempt, receipt)) goto done;
    if (after) {
        stored = task_read_record(attempt, "contract-inputs.json"); ok = stored && json_object_equal(receipt, stored);
    } else ok = !task_write_json(attempt, "contract-inputs.json", receipt, false);
done:
    json_object_put(stored); json_object_put(receipt); return ok;
}
static bool check_rules(json_object *step, const char *attempt, bool after, bool candidates) {
    json_object *rules = f_field(step, candidates ? "candidates" : "invariants");
    if (!rules) return true;
    for (size_t i = 0; i < json_object_array_length(rules); i++) {
        json_object *rule = json_object_array_get_idx(rules, i);
        if (strcmp(f_string(rule, "phase"), after ? "after" : "before")) continue;
        if (!(candidates ? candidate(attempt, rule, after) : invariant(attempt, rule))) return false;
    }
    return true;
}
int wc_rules(json_object *manifest, const char *name, const char *attempt, bool after) {
    if (!f_number_is(manifest, "schema_version", 2)) return 0;
    json_object *step = f_field(f_field(manifest, "steps"), name);
    if (!input_receipt(step, attempt, after)) return -1;
    if (!check_rules(step, attempt, after, false) || !check_rules(step, attempt, after, true)) return -1;
    json_object *rule = f_field(step, "conversion");
    return after && rule && !conversion(attempt, rule) ? -1 : 0;
}
