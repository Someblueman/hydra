/* `hydra remote setup remove NAME`: deletes the local setup record and its
 * lock after approval, so a setup bound to the wrong destination or SSH
 * config can be started again. It never connects to the remote: what earlier
 * steps installed there stays and is named in the plan. A finished setup
 * whose alias is published is refused; that alias is removed with `hydra
 * remote remove NAME`. The caller holds the setup lock, so a running step
 * makes the command fail with setup_busy before this file is reached. */
#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* Statuses after which a step may have changed the remote. */
static bool touched(const char *status) {
    return !strcmp(status, "done") || !strcmp(status, "in_progress") || !strcmp(status, "outcome_unknown");
}
static void left_provision(json_object *left, const char *status, json_object *detail) {
    const char *where = f_string(detail, "prefix") ? f_string(detail, "prefix") : f_string(detail, "hydra");
    char text[F_PATH + 96];
    snprintf(text, sizeof(text), "%s at %s", strcmp(status, "done") ? "a possibly partial Hydra install" : "the Hydra install",
             where ? where : "its recorded prefix");
    json_object_array_add(left, json_object_new_string(text));
}
static void left_agent(json_object *left, const char *step, json_object *detail) {
    const char *agent = strchr(step, ':') + 1, *path = f_string(detail, "path");
    char text[F_PATH + 160];
    if (!strncmp(step, "sign_in:", 8)) snprintf(text, sizeof(text), "%s's sign-in (the provider's credentials on the remote)", agent);
    else snprintf(text, sizeof(text), "the %s agent from its provider's installer%s%s", agent, path ? " at " : "", path ? path : "");
    json_object_array_add(left, json_object_new_string(text));
}
bool setup_remote_changed(struct setup_ctx *ctx) {
    json_object_object_foreach(f_field(ctx->state, "steps"), step, record) {
        const char *status = f_string(record, "status");
        if (status && touched(status) && (!strcmp(step, "provision") || strchr(step, ':'))) return true;
    }
    return false;
}
/* What earlier steps changed on the remote; removing the record undoes none of it. */
static json_object *remote_left(struct setup_ctx *ctx) {
    json_object *left = json_object_new_array();
    json_object_object_foreach(f_field(ctx->state, "steps"), step, record) {
        const char *status = f_string(record, "status");
        json_object *detail = f_field(record, "detail");
        /* A skipped provision reused an install that was already there. */
        if (!status || !touched(status)) continue;
        if (!strcmp(step, "provision")) left_provision(left, status, detail);
        else if (strchr(step, ':')) left_agent(left, step, detail);
    }
    return left;
}
static json_object *local_left(struct setup_ctx *ctx) {
    json_object *left = json_object_new_array(), *detail = setup_state_detail(ctx, "host_key");
    const char *file = f_string(detail, "known_hosts"), *result = f_string(detail, "result");
    char text[F_PATH + 96];
    if (file && result && !strcmp(result, "trusted")) {
        snprintf(text, sizeof(text), "the host key line Hydra appended to %s", file);
        json_object_array_add(left, json_object_new_string(text));
    }
    return left;
}
static json_object *plan_json(struct setup_ctx *ctx) {
    json_object *plan = json_object_new_object(), *left = remote_left(ctx);
    bool changed = json_object_array_length(left) > 0;
    f_string_add(plan, "change", "delete this host's local setup record and its lock; nothing on the remote is changed or undone");
    f_string_add(plan, "record", ctx->path);
    f_string_add(plan, "remote", changed ? "these stay on the remote and are not removed: see remote_left"
                                         : "no step changed the remote");
    json_object_object_add(plan, "remote_left", left);
    json_object_object_add(plan, "local_left", local_left(ctx));
    return plan;
}
/* NAME.json, then NAME.lock (still held), then the directory entry is synced. */
static int delete_record(const struct setup_ctx *ctx) {
    char lock[F_PATH], dir[F_PATH], *slash; size_t length = strlen(ctx->path); int fd, status;
    if (length < 5 || f_copy(lock, sizeof(lock), ctx->path) || f_copy(dir, sizeof(dir), ctx->path)) return -1;
    memcpy(lock + length - 5, ".lock", 5);
    if ((unlink(ctx->path) && errno != ENOENT) || (unlink(lock) && errno != ENOENT)) return -1;
    if (!(slash = strrchr(dir, '/'))) return -1;
    *slash = '\0';
    if ((fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0) return -1;
    status = fsync(fd); close(fd);
    return status;
}
static json_object *published(struct setup_ctx *ctx) {
    char recovery[400];
    snprintf(recovery, sizeof(recovery), "this setup finished and published the remote alias %s; remove the alias with hydra "
             "remote remove %s instead (the finished record then can be removed too)", ctx->name, ctx->name);
    return setup_error(ctx, "setup_complete", "a finished setup whose alias is in use cannot be removed", recovery, NULL);
}
static json_object *removed(struct setup_ctx *ctx, json_object *plan) {
    json_object *data = json_object_new_object(); char summary[512];
    snprintf(summary, sizeof(summary), "removed the setup record for %s; nothing on %s was changed", ctx->name, ctx->remote.target);
    json_object_object_add(data, "removed", json_object_new_boolean(true));
    f_string_add(data, "name", ctx->name); f_string_add(data, "destination", ctx->remote.target);
    f_string_add(data, "record", ctx->path); f_string_add(data, "summary", summary);
    json_object_object_add(data, "remote_left", json_object_get(f_field(plan, "remote_left")));
    json_object_object_add(data, "local_left", json_object_get(f_field(plan, "local_left")));
    return f_success(ctx->command, data);
}
json_object *setup_remove(struct setup_ctx *ctx, const char *approve) {
    const char *const argv[] = {"setup", "remove", ctx->name, NULL};
    struct f_remote alias; char step[80]; json_object *plan, *result;
    if (!setup_first_open(ctx, step) && !f_remote_load(ctx->name, &alias)) return published(ctx);
    plan = plan_json(ctx);
    if ((result = setup_plan_gate(ctx, "remove", plan, approve, argv))) { json_object_put(plan); return result; }
    if (delete_record(ctx)) {
        json_object_put(plan);
        return setup_error(ctx, "state_unavailable", "cannot delete the setup record", "check $HYDRA_HOME/fleet/setup, then rerun", NULL);
    }
    /* The record is gone: the envelope must not describe its steps. */
    json_object_put(ctx->state); ctx->state = NULL;
    result = removed(ctx, plan);
    json_object_put(plan);
    return result;
}
