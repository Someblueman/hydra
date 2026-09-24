#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include <string.h>

/* Store a strict, bounded proposal. This does not validate policy, compile,
 * approve or execute anything; those remain separate public operations. */
json_object *plan_proposal_copy(const char *input, const char *output) {
    json_object *proposal = plan_read(input), *result;
    if (!proposal || !f_string(proposal, "objective") ||
        !plan_list(f_field(proposal, "steps"), 1, PLAN_STEPS)) {
        json_object_put(proposal);
        return f_error("workflow plan propose", "invalid_proposal", "expected a bounded JSON plan with an objective and steps; obtain the format from workflow plan schema");
    }
    const char *text = json_object_to_json_string_ext(proposal, JSON_C_TO_STRING_PLAIN);
    if (!text || strlen(text) > PLAN_LIMIT || f_write(output, text, strlen(text), false))
        result = f_error("workflow plan propose", "write_failed", "could not preserve the proposal in a new private file");
    else result = f_success("workflow plan propose", json_object_new_object());
    json_object_put(proposal);
    return result;
}
