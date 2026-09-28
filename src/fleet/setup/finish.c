#include "fleet/setup/setup.h"
#include "fleet/support/json.h"
#include <string.h>

/* Records a done step and returns a separate copy as the envelope data, so
 * later envelope decoration never reaches the persisted detail. */
static json_object *finished(struct setup_ctx *ctx, const char *step, json_object *detail) {
    json_object *copy = NULL;
    if (json_object_deep_copy(detail, &copy, NULL)) copy = NULL;
    if (!copy || setup_state_step(ctx, step, "done", detail)) {
        json_object_put(copy);
        return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    }
    return f_success(ctx->command, copy);
}
static json_object *provisioned(struct setup_ctx *ctx) {
    if (ctx->remote.hydra[0] == '/') return NULL;
    return setup_error(ctx, "prerequisite_missing", "the remote has no provisioned Hydra yet",
                       "complete hydra remote provision NAME first", NULL);
}
json_object *setup_step_verify(struct setup_ctx *ctx) {
    json_object *reply, *detail, *error = provisioned(ctx);
    const char *version, *code;
    if (error) return error;
    reply = f_observe(&ctx->remote, "handshake", ctx->seconds);
    code = f_string(f_field(reply, "error"), "code");
    version = f_string(f_field(reply, "data"), "hydra_version");
    if (code || !version || strcmp(version, F_VERSION)) {
        error = setup_error(ctx, code ? code : "version_mismatch",
                            code ? "the provisioned Hydra did not complete a strict handshake" : "the remote Hydra version differs from this Hydra",
                            "rerun hydra remote provision NAME, then hydra remote setup NAME", NULL);
        json_object_put(reply);
        (void)setup_state_step(ctx, "verify", "failed", NULL);
        return error;
    }
    json_object_put(reply);
    detail = json_object_new_object();
    f_string_add(detail, "hydra_version", F_VERSION);
    f_string_add(detail, "summary", "Hydra " F_VERSION " answers the fleet handshake");
    return finished(ctx, "verify", detail);
}
json_object *setup_step_alias(struct setup_ctx *ctx) {
    json_object *detail, *error = provisioned(ctx);
    if (error) return error;
    if (f_remote_enrolled(&ctx->remote, true)) {
        (void)setup_state_step(ctx, "alias", "blocked", NULL);
        return setup_error(ctx, "alias_conflict", "a different remote alias with this name already exists",
                           "inspect hydra remote list; remove the old alias deliberately or choose another NAME", NULL);
    }
    detail = json_object_new_object();
    f_string_add(detail, "alias", ctx->name);
    f_string_add(detail, "summary", "alias published");
    return finished(ctx, "alias", detail);
}
