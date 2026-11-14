#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define PROPOSAL_FIX "correct the draft against hydra workflow plan schema, then publish it again with hydra workflow plan propose"
static json_object *referenced_assets(json_object *proposal) {
    json_object *inputs = f_field(f_field(proposal, "data"), "inputs"), *names = json_object_new_object();
    if (!json_object_is_type(inputs, json_type_object)) return names;
    json_object_object_foreach(inputs, input, declaration) {
        const char *name = f_string(declaration, "asset"); (void)input;
        if (name) json_object_object_add(names, name, json_object_new_boolean(true));
    }
    return names;
}
/* Every published asset must be referenced by a data input and vice versa, so
 * a typo is caught when the agent publishes rather than when the user validates. */
static const char *asset_mismatch(json_object *proposal, const char *directory) {
    json_object *names = referenced_assets(proposal); DIR *dir = opendir(directory); struct dirent *entry;
    const char *problem = NULL; int present = 0;
    if (!dir) { json_object_put(names); return "could not read the staged assets"; }
    while (!problem && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!f_field(names, entry->d_name)) problem = "an --asset is not referenced by any data.inputs entry; declare {\"asset\": NAME, \"type\": \"file\", \"max_bytes\": N} or drop it";
        present++;
    }
    closedir(dir);
    if (!problem && present != json_object_object_length(names))
        problem = "a data.inputs entry names an asset that was not published; pass --asset NAME=FILE for each asset the draft references";
    json_object_put(names); return problem;
}
/* The members workflow plan schema requires of every plan; validation reports
 * any of them that is absent, so publication refuses such a draft as well. */
static json_object *missing_members(json_object *proposal) {
    static const char *const required[] = {"schema_version", "id", "objective", "context", "assumptions", "questions",
                                           "deliverables", "requirements", "checks", "steps", "data", "envelope"};
    char message[512] = "the draft lacks required plan members:";
    size_t i; bool missing = false;
    for (i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (f_field(proposal, required[i])) continue;
        (void)snprintf(message + strlen(message), sizeof(message) - strlen(message), "%s %s", missing ? "," : "", required[i]);
        missing = true;
    }
    if (!missing) return NULL;
    (void)snprintf(message + strlen(message), sizeof(message) - strlen(message), "; obtain the format from workflow plan schema");
    return plan_failure("workflow plan propose", "invalid_proposal", message, PROPOSAL_FIX);
}
/* A short-form draft is checked for its own shape; the policy is applied when it is validated. */
static json_object *simple_shape(json_object *proposal) {
    json_object *errors = json_object_new_array(), *lowered = plan_simple_lower(proposal, NULL, errors), *result = NULL;
    if (!lowered) {
        json_object *first = json_object_array_get_idx(errors, 0);
        char message[1024];
        (void)snprintf(message, sizeof(message), "%s: %s", f_string(first, "path") ? f_string(first, "path") : "$",
                       f_string(first, "message") ? f_string(first, "message") : "the draft is not a simple-1 plan");
        result = plan_failure("workflow plan propose", "invalid_proposal", message, "correct the draft against hydra plan schema, then publish it again");
    }
    json_object_put(lowered); json_object_put(errors);
    return result;
}
/* Store a strict, bounded proposal. This does not validate policy, compile,
 * approve or execute anything; those remain separate public operations. */
json_object *plan_proposal_copy(const char *input, const char *output, const char *assets) {
    json_object *proposal = plan_read(input), *result;
    const char *problem;
    if (!proposal || !f_string(proposal, "objective") ||
        !plan_list(f_field(proposal, "steps"), 1, PLAN_STEPS)) {
        json_object_put(proposal);
        return plan_failure("workflow plan propose", "invalid_proposal", "expected a bounded JSON plan with an objective and steps; obtain the format from workflow plan schema", PROPOSAL_FIX);
    }
    if (plan_simple(proposal)) result = simple_shape(proposal);
    else result = missing_members(proposal);
    if (result) {
        json_object_put(proposal);
        return result;
    }
    if (assets && (problem = asset_mismatch(proposal, assets))) {
        json_object_put(proposal);
        return plan_failure("workflow plan propose", "asset_mismatch", problem, "publish again with one --asset NAME=FILE for each asset the draft's data.inputs names, and no others");
    }
    const char *text = json_object_to_json_string_ext(proposal, JSON_C_TO_STRING_PLAIN);
    if (!text || strlen(text) > PLAN_LIMIT || f_write(output, text, strlen(text), false))
        result = plan_failure("workflow plan propose", "write_failed", "could not preserve the proposal in a new private file", "check free space and permissions under HYDRA_HOME, then publish again");
    else result = f_success("workflow plan propose", json_object_new_object());
    json_object_put(proposal);
    return result;
}
