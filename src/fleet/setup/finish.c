#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <stdio.h>
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
    /* An upgrade may keep a same-version install that the alias names by PATH. */
    if (ctx->remote.hydra[0] == '/' || (ctx->remote.hydra[0] && !strcmp(setup_state_status(ctx, "provision"), "skipped"))) return NULL;
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
static json_object *publish_alias(struct setup_ctx *ctx) {
    json_object *detail;
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

/* ---- Upgrade: update only hydra (and a newly recorded host key) ---- */
static bool same_text(const char *left, const char *right) { return !strcmp(left, right); }
static const char *new_host_key(const struct setup_ctx *ctx, const struct f_remote *alias) {
    return ctx->remote.accepted_host_key[0] && !alias->accepted_host_key[0] ? ctx->remote.accepted_host_key : NULL;
}
static json_object *upgrade_plan(struct setup_ctx *ctx, const struct f_remote *alias) {
    json_object *plan = json_object_new_object();
    const char *old_version = f_string(f_field(setup_state_detail(ctx, "preflight"), "alias_hydra"), "version");
    f_string_add(plan, "alias", alias->name); f_string_add(plan, "target", alias->target);
    f_string_add(plan, "old_hydra", alias->hydra); f_string_add(plan, "new_hydra", ctx->remote.hydra);
    f_string_add(plan, "old_version", old_version ? old_version : "unknown");
    f_string_add(plan, "new_version", F_VERSION);
    if (new_host_key(ctx, alias)) f_string_add(plan, "accepted_host_key", new_host_key(ctx, alias));
    f_string_add(plan, "previous_install", "stays installed; nothing is removed and no other alias field changes");
    return plan;
}
static bool alias_unchanged_since(const struct f_remote *before) {
    struct f_remote now;
    return !f_remote_load(before->name, &now) && same_text(now.target, before->target) && same_text(now.hydra, before->hydra) &&
        same_text(now.accepted_host_key, before->accepted_host_key);
}
static json_object *update_alias(struct setup_ctx *ctx, const struct f_remote *alias) {
    struct f_remote updated = *alias; json_object *detail; char summary[F_PATH * 2 + 32];
    const char *key = new_host_key(ctx, alias);
    if (!alias_unchanged_since(alias))
        return setup_error(ctx, "alias_conflict", "the alias changed while setup was running", "inspect hydra remote list, then rerun hydra remote setup NAME", NULL);
    f_copy(updated.hydra, sizeof(updated.hydra), ctx->remote.hydra);
    if (key) f_copy(updated.accepted_host_key, sizeof(updated.accepted_host_key), key);
    if (f_remote_save(&updated)) return setup_error(ctx, "io_failed", "cannot update the alias record", "rerun hydra remote setup NAME", NULL);
    detail = json_object_new_object();
    f_string_add(detail, "alias", ctx->name); f_string_add(detail, "previous_hydra", alias->hydra);
    f_string_add(detail, "hydra", updated.hydra);
    snprintf(summary, sizeof(summary), "alias updated: %s -> %s", alias->hydra, updated.hydra);
    f_string_add(detail, "summary", summary);
    return finished(ctx, "alias", detail);
}
static json_object *upgrade_alias(struct setup_ctx *ctx, const char *approve) {
    const char *const argv[] = {"setup", ctx->name, NULL};
    struct f_remote alias; json_object *plan, *gate, *detail;
    if (f_remote_load(ctx->name, &alias)) return publish_alias(ctx);
    if (!same_text(alias.target, ctx->remote.target) || !same_text(alias.ssh_config, ctx->remote.ssh_config))
        return setup_error(ctx, "alias_conflict", "the alias now points at a different destination", "inspect hydra remote list; setup never re-points an alias", NULL);
    if (same_text(alias.hydra, ctx->remote.hydra) && !new_host_key(ctx, &alias)) {
        detail = json_object_new_object();
        f_string_add(detail, "alias", ctx->name); f_string_add(detail, "summary", "alias already uses this install");
        if (setup_state_step(ctx, "alias", "skipped", detail)) return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
        return f_success(ctx->command, json_object_new_object());
    }
    plan = upgrade_plan(ctx, &alias);
    gate = setup_plan_gate(ctx, "alias", plan, approve, argv);
    json_object_put(plan);
    return gate ? gate : update_alias(ctx, &alias);
}
json_object *setup_step_alias(struct setup_ctx *ctx) {
    return setup_step_alias_approved(ctx, NULL);
}
json_object *setup_step_alias_approved(struct setup_ctx *ctx, const char *approve) {
    json_object *error = provisioned(ctx);
    if (error) return error;
    return setup_upgrade(ctx) ? upgrade_alias(ctx, approve) : publish_alias(ctx);
}
