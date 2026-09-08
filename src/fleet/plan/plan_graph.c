#include "fleet/support/json.h"
#include "fleet/plan/plan.h"
#include <string.h>

static json_object *record(json_object *array, const char *id) {
    int i = plan_index(array, id); return i < 0 ? NULL : json_object_array_get_idx(array, (size_t)i);
}
static json_object *output(json_object *plan, const char *step, const char *name) {
    if (!step || !name) return NULL;
    return f_field(f_field(f_field(f_field(f_field(plan, "data"), "steps"), step), "outputs"), name);
}
static bool overlap(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b), n = na < nb ? na : nb;
    const char *ca = strchr(a, ':'), *cb = strchr(b, ':');
    if (ca && ca[1] == '*' && ca[2] == '\0') return !strncmp(a, b, (size_t)(ca-a)+1);
    if (cb && cb[1] == '*' && cb[2] == '\0') return !strncmp(b, a, (size_t)(cb-b)+1);
    return !strncmp(a, b, n) && (na == nb || (na < nb ? b[n] : a[n]) == '/');
}
static bool delivery_role(json_object *plan, json_object *d, const char *role) {
    const char *destination = f_string(d, "destination");
    if (!destination || !role) return false;
    if (f_number_is(plan, "schema_version", 2) && !strcmp(destination, "intermediate")) return true;
    return !strcmp(role, "compose") && !strcmp(destination, "run-artifact");
}
static bool executable_recipe(json_object *plan, json_object *step, const char *method) {
    return f_number_is(plan, "schema_version", 2) || !method || strcmp(method, "executable") || f_field(f_field(step, "args"), "argv");
}
static void check_ownership(json_object *requirements, const char *check, json_object *errors) {
    if (!check) return; /* Missing IDs are rejected by structural validation. */
    for (size_t i = 0; i < json_object_array_length(requirements); i++) {
        const char *owner = f_string(json_object_array_get_idx(requirements, i), "check");
        if (owner && !strcmp(owner, check)) return;
    }
    plan_error(errors, "checks", "orphan_check", "each check must own at least one requirement so its report can claim valid coverage");
}
static void coverage(json_object *plan, json_object *errors) {
    json_object *deliverables = f_field(plan, "deliverables"), *requirements = f_field(plan, "requirements"), *checks = f_field(plan, "checks"), *steps = f_field(plan, "steps");
    size_t i, j;
    for (i = 0; i < json_object_array_length(deliverables); i++) {
        json_object *d = json_object_array_get_idx(deliverables, i), *producer = record(steps, f_string(d, "step")); bool covered = false;
        const char *role = f_string(producer, "role");
        if (!plan_text(f_field(d, "description")) || !plan_id(f_string(d, "output")) || !delivery_role(plan, d, role) ||
            !output(plan, f_string(d, "step"), f_string(d, "output")))
            plan_error(errors, "deliverables", "missing_composition", "each final deliverable needs a compose step, declared output and run-artifact destination");
        for (j = 0; j < json_object_array_length(requirements); j++) {
            const char *id = f_string(json_object_array_get_idx(requirements, j), "deliverable");
            if (id && !strcmp(id, f_string(d, "id"))) covered = true;
        }
        if (!covered) plan_error(errors, "deliverables", "uncovered_deliverable", "each deliverable needs a requirement and check");
    }
    for (i = 0; i < json_object_array_length(checks); i++) {
        json_object *c = json_object_array_get_idx(checks, i), *d = record(deliverables, f_string(c, "deliverable")), *step = record(steps, f_string(c, "step"));
        const char *method = f_string(c, "method"), *role = f_string(step, "role"), *input = f_string(c, "input");
        json_object *ref = input && f_string(c, "step") ? f_field(f_field(f_field(f_field(f_field(plan, "data"), "steps"), f_string(c, "step")), "inputs"), input) : NULL;
        json_object *report = output(plan, f_string(c, "step"), f_string(c, "report"));
        const char *producer = f_string(ref, "step"), *name = f_string(ref, "output");
        if (!method || (strcmp(method, "executable") && strcmp(method, "assessment")))
            plan_error(errors, "checks", "unsupported_evaluation", "evaluation methods are executable and assessment; human judgment needs a separate authorized stage");
        if (!d || !role || strcmp(role, "verify") || !plan_text(f_field(c, "definition")) || !plan_id(input) || !plan_id(f_string(c, "report")) ||
            !producer || !name || !f_string(d, "step") || !f_string(d, "output") || strcmp(producer, f_string(d, "step")) || strcmp(name, f_string(d, "output")) ||
            !f_string(report, "type") || strcmp(f_string(report, "type"), "object") ||
            json_object_array_length(f_field(step, "writes")))
            plan_error(errors, "checks", "invalid_verification", "a verify step must consume the exact composed artifact, declare an object report and no repository writes");
        if (!executable_recipe(plan, step, method))
            plan_error(errors, "checks", "invalid_evaluator", "executable checks require an argv recipe; agent assessment is a distinct method");
        check_ownership(requirements, f_string(c, "id"), errors);
    }
    for (i = 0; i < json_object_array_length(requirements); i++) {
        json_object *r = json_object_array_get_idx(requirements, i), *d = record(deliverables, f_string(r, "deliverable")), *c = record(checks, f_string(r, "check"));
        if (!plan_text(f_field(r, "criterion")) || !d || !c || !f_string(c, "deliverable") || strcmp(f_string(c, "deliverable"), f_string(d, "id")))
            plan_error(errors, "requirements", "uncovered_requirement", "every requirement needs an explicit criterion and a check for its final deliverable");
    }
}
static void dependencies(json_object *steps, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors) {
    size_t n = json_object_array_length(steps), i, j, k;
    for (i = 0; i < n; i++) {
        json_object *needs = f_field(json_object_array_get_idx(steps, i), "needs");
        for (j = 0; j < json_object_array_length(needs); j++) {
            int index = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
            if (index < 0) plan_error(errors, "steps.needs", "missing_dependency", "dependency does not name a step");
            else reach[i][index] = true;
        }
    }
    for (k = 0; k < n; k++) for (i = 0; i < n; i++) for (j = 0; j < n; j++) reach[i][j] = reach[i][j] || (reach[i][k] && reach[k][j]);
}
static void write_conflicts(json_object *writes, json_object *other_writes, json_object *errors) {
    size_t a, b;
    for (a = 0; a < json_object_array_length(writes); a++) for (b = 0; b < json_object_array_length(other_writes); b++)
        if (overlap(f_text(json_object_array_get_idx(writes, a)), f_text(json_object_array_get_idx(other_writes, b))))
            plan_error(errors, "steps.writes", "write_conflict", "overlapping mutable targets require dependency ordering");
}
int plan_graph(json_object *plan, json_object *errors) {
    bool reach[PLAN_STEPS][PLAN_STEPS] = {{false}};
    json_object *steps = f_field(plan, "steps"); size_t n = json_object_array_length(steps), i, j;
    dependencies(steps, reach, errors);
    for (i = 0; i < n; i++) {
        json_object *s = json_object_array_get_idx(steps, i), *writes = f_field(s, "writes");
        const char *head = f_string(f_field(s, "args"), "head"); unsigned producers = 0;
        if (f_string(f_field(s, "args"), "profile")) {
            json_object *args = f_field(s, "args"), *decl = output(plan, f_string(s, "id"), f_string(args, "result_file"));
            json_object *inputs = f_field(f_field(f_field(f_field(plan, "data"), "steps"), f_string(s, "id")), "inputs");
            if (!f_field(inputs, f_string(args, "prompt_input")) || !f_string(decl, "path") || strcmp(f_string(decl, "path"), f_string(args, "result_file")))
                plan_error(errors, "steps.args.profile", "invalid_profile_handoff", "profile prompt_input must name a declared input; result_file must name a declared output with the same path");
        }
        if (reach[i][i]) plan_error(errors, "steps.needs", "cycle", "dependency graph contains a cycle");
        for (j = 0; j < n; j++) {
            json_object *t = json_object_array_get_idx(steps, j); const char *branch = f_string(f_field(t, "args"), "branch");
            if (head && branch && !strcmp(head, branch)) {
                producers++; if (!reach[i][j]) plan_error(errors, "steps.args.head", "missing_head_dependency", "exec must depend on its head's spawn step");
            }
            if (j <= i) continue;
            if (branch && f_string(f_field(s, "args"), "branch") && !strcmp(branch, f_string(f_field(s, "args"), "branch")))
                plan_error(errors, "steps.args.branch", "duplicate_head", "each head has exactly one spawn recipe");
            if (reach[i][j] || reach[j][i]) continue;
            write_conflicts(writes, f_field(t, "writes"), errors);
        }
        if (head && producers != 1) plan_error(errors, "steps.args.head", "unbound_head", "initial plans create every execution head from the bound source");
    }
    coverage(plan, errors);
    plan_evidence_graph(plan, reach, errors);
    return json_object_array_length(errors) ? -1 : 0;
}
