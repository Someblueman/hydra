#include "fleet/setup/setup.h"
#include "fleet/support/json.h"

/* Agent inventory and installers (design §6-7). Placeholders owned by the agents workstream. */
json_object *setup_step_agents(struct setup_ctx *ctx, const char *record) {
    (void)record;
    return setup_error(ctx, "not_implemented", "remote agent inventory is not implemented yet",
                       "inspect the remote PATH manually", NULL);
}
json_object *setup_step_install_agent(struct setup_ctx *ctx, const char *agent, const char *approve) {
    (void)agent; (void)approve;
    return setup_error(ctx, "not_implemented", "remote agent installation is not implemented yet",
                       "install the agent on the remote host yourself", NULL);
}
