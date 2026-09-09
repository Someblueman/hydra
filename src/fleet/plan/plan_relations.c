#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <string.h>

static bool relation(json_object *plan, json_object *row) {
    const char *const keys[] = {"type", "from", "to", "enforcement", NULL};
    const char *type = f_string(row, "type"), *from = f_string(row, "from"), *to = f_string(row, "to");
    const char *enforcement = f_string(row, "enforcement");
    json_object *steps = f_field(plan, "steps"); int a = plan_index(steps, from), b = plan_index(steps, to);
    if (!task_keys(row, keys) || !type || !enforcement || a < 0 || b < 0 || a == b) return false;
    if (!strcmp(type, "provenance") || !strcmp(type, "resource")) return !strcmp(enforcement, "descriptive");
    if (strcmp(type, "data") && strcmp(type, "evidence") && strcmp(type, "effect") && strcmp(type, "order")) return false;
    return !strcmp(enforcement, "dependency") && plan_has(f_field(json_object_array_get_idx(steps, (size_t)b), "needs"), from);
}
int plan_relations(json_object *plan, json_object *errors) {
    json_object *relations = f_field(plan, "relations");
    if (!relations) return 0;
    if (!plan_list(relations, 1, 256)) {
        plan_error(errors, "relations", "invalid_relation", "expected 1 to 256 typed relations"); return -1;
    }
    for (size_t i = 0; i < json_object_array_length(relations); i++) {
        if (relation(plan, json_object_array_get_idx(relations, i))) continue;
        char path[64]; snprintf(path, sizeof(path), "relations[%zu]", i);
        plan_error(errors, path, "unsupported_relation", "data/evidence/effect/order require an explicit direct dependency; resource and provenance support descriptive enforcement only");
    }
    return json_object_array_length(errors) ? -1 : 0;
}
