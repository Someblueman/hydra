#include "fleet/plan/plan_inspect.h"
#include <string.h>

void pi_reach(json_object *steps, bool reach[PLAN_STEPS][PLAN_STEPS]) {
    size_t count = json_object_array_length(steps);
    for (size_t i = 0; i < count; i++) {
        json_object *needs = f_field(json_object_array_get_idx(steps, i), "needs");
        for (size_t j = 0; j < json_object_array_length(needs); j++) {
            int before = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
            if (before >= 0) reach[i][before] = true;
        }
    }
    for (size_t k = 0; k < count; k++)
        for (size_t i = 0; i < count; i++)
            for (size_t j = 0; j < count; j++)
                reach[i][j] = reach[i][j] || (reach[i][k] && reach[k][j]);
}
static json_object *affected(json_object *steps, json_object *records, size_t node,
                              bool reach[PLAN_STEPS][PLAN_STEPS]) {
    json_object *out = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i);
        int target = plan_index(steps, f_string(record, "step"));
        if (target >= 0 && ((size_t)target == node || reach[target][node]))
            json_object_array_add(out, json_object_get(f_field(record, "id")));
    }
    return out;
}
static void input_reasons(json_object *out, json_object *inputs, const char *before) {
    if (!json_object_is_type(inputs, json_type_object)) return;
    json_object_object_foreach(inputs, name, input) {
        const char *producer = f_string(input, "step");
        if (producer && !strcmp(producer, before)) {
            json_object *reason = json_object_new_object();
            f_string_add(reason, "kind", "artifact_input"); f_string_add(reason, "input", name);
            json_object_object_add(reason, "output", json_object_get(f_field(input, "output")));
            json_object_array_add(out, reason);
        }
    }
}
static json_object *edge_reasons(json_object *plan, json_object *step, json_object *before) {
    json_object *out = json_object_new_array();
    const char *head = f_string(f_field(step, "args"), "head");
    const char *branch = f_string(f_field(before, "args"), "branch");
    json_object *inputs = f_field(f_field(f_field(f_field(plan, "data"), "steps"), f_string(step, "id")), "inputs");
    if (head && branch && !strcmp(head, branch))
        json_object_array_add(out, json_object_new_string("creates_execution_head"));
    input_reasons(out, inputs, f_string(before, "id"));
    if (!json_object_array_length(out))
        json_object_array_add(out, json_object_new_string("declared_order; purpose_not_formalized"));
    return out;
}
static json_object *node_explanation(json_object *plan, size_t index,
                                      bool reach[PLAN_STEPS][PLAN_STEPS]) {
    json_object *steps = f_field(plan, "steps"), *step = json_object_array_get_idx(steps, index);
    json_object *out = json_object_new_object(), *edges = json_object_new_array();
    json_object *needs = f_field(step, "needs");
    const char *fields[] = {"id", "kind", "role", "writes", NULL};
    for (size_t i = 0; fields[i]; i++)
        json_object_object_add(out, fields[i], json_object_get(f_field(step, fields[i])));
    json_object_object_add(out, "deliverables", affected(steps, f_field(plan, "deliverables"), index, reach));
    json_object_object_add(out, "checks", affected(steps, f_field(plan, "checks"), index, reach));
    json_object_object_add(out, "inputs", json_object_get(f_field(f_field(f_field(f_field(plan, "data"), "steps"), f_string(step, "id")), "inputs")));
    for (size_t i = 0; i < json_object_array_length(needs); i++) {
        const char *id = f_text(json_object_array_get_idx(needs, i));
        json_object *edge = json_object_new_object();
        f_string_add(edge, "depends_on", id);
        json_object_object_add(edge, "reasons", edge_reasons(plan, step,
            json_object_array_get_idx(steps, (size_t)plan_index(steps, id))));
        json_object_array_add(edges, edge);
    }
    f_string_add(out, "outcome_link", json_object_array_length(f_field(out, "checks")) ||
        json_object_array_length(f_field(out, "deliverables")) ? "reachable" : "unconnected; review necessity");
    json_object_object_add(out, "dependencies", edges);
    return out;
}
json_object *pi_explain(json_object *compiled, const char *digest) {
    json_object *out = json_object_new_object(), *plan = f_field(compiled, "plan");
    json_object *steps = f_field(plan, "steps"), *nodes = json_object_new_array();
    bool reach[PLAN_STEPS][PLAN_STEPS] = {{false}};
    pi_reach(steps, reach);
    json_object_object_add(out, "schema_version", json_object_new_int(1));
    f_string_add(out, "plan_sha256", digest); f_string_add(out, "compiler", PLAN_COMPILER);
    f_string_add(out, "analysis", "offline structural inspection; source freshness, semantic adequacy and execution acceptance are not established");
    json_object_object_add(out, "requirements", json_object_get(f_field(plan, "requirements")));
    json_object_object_add(out, "obligations", plan_obligations_projection(plan));
    json_object_object_add(out, "semantic_reviews", plan_obligations_reviews(plan));
    for (size_t i = 0; i < json_object_array_length(steps); i++)
        json_object_array_add(nodes, node_explanation(plan, i, reach));
    json_object_object_add(out, "nodes", nodes);
    return out;
}
