#include "fleet/setup/setup.h"
#include "fleet/support/json.h"

/* Host key trust (design §3). Placeholder until the host-key work lands. */
json_object *setup_step_trust_key(struct setup_ctx *ctx, const char *fingerprint) {
    (void)fingerprint;
    return setup_error(ctx, "not_implemented", "host key trust is not implemented yet",
                       "add the host key to known_hosts yourself, then use hydra remote add", NULL);
}
