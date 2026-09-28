#include "fleet/setup/setup.h"
#include "fleet/support/json.h"

/* Approved provisioning (design §5). Placeholder owned by the provisioning workstream. */
json_object *setup_step_provision(struct setup_ctx *ctx, const char *binary, const char *approve) {
    (void)binary; (void)approve;
    return setup_error(ctx, "not_implemented", "remote provisioning is not implemented yet",
                       "use hydra fleet package and hydra fleet bootstrap", NULL);
}
