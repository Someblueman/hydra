#include "fleet/setup/setup.h"
#include "fleet/support/json.h"

/* Native provider sign-in (design §7). Placeholder owned by the agents workstream. */
json_object *setup_step_sign_in(struct setup_ctx *ctx, const char *agent) {
    (void)agent;
    return setup_error(ctx, "not_implemented", "remote agent sign-in is not implemented yet",
                       "use hydra fleet auth login HOST --agent AGENT", NULL);
}
