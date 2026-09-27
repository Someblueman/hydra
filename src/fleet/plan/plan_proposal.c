#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include <dirent.h>
#include <string.h>

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
/* Store a strict, bounded proposal. This does not validate policy, compile,
 * approve or execute anything; those remain separate public operations. */
json_object *plan_proposal_copy(const char *input, const char *output, const char *assets) {
    json_object *proposal = plan_read(input), *result;
    const char *problem;
    if (!proposal || !f_string(proposal, "objective") ||
        !plan_list(f_field(proposal, "steps"), 1, PLAN_STEPS)) {
        json_object_put(proposal);
        return f_error("workflow plan propose", "invalid_proposal", "expected a bounded JSON plan with an objective and steps; obtain the format from workflow plan schema");
    }
    if (assets && (problem = asset_mismatch(proposal, assets))) {
        json_object_put(proposal);
        return f_error("workflow plan propose", "asset_mismatch", problem);
    }
    const char *text = json_object_to_json_string_ext(proposal, JSON_C_TO_STRING_PLAIN);
    if (!text || strlen(text) > PLAN_LIMIT || f_write(output, text, strlen(text), false))
        result = f_error("workflow plan propose", "write_failed", "could not preserve the proposal in a new private file");
    else result = f_success("workflow plan propose", json_object_new_object());
    json_object_put(proposal);
    return result;
}
