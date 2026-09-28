#include "fleet/setup/setup.h"
#include "fleet/support/json.h"

/* Read-only remote preflight (design §4). Placeholder until the preflight work lands. */
json_object *setup_step_preflight(struct setup_ctx *ctx) {
    return setup_error(ctx, "not_implemented", "remote preflight is not implemented yet",
                       "check the remote prerequisites manually", NULL);
}
