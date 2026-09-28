#include "fleet/setup/agent_setup.h"
#include "fleet/agent/agent.h"
#include "fleet/auth/agent_auth.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Native provider sign-in (design §7): the provider's own login runs on the
 * remote over ssh -t with the resolved absolute executable, so a
 * non-interactive PATH never hides it. Credentials stay on the remote; Hydra
 * never copies them here (`hydra fleet auth preview/copy` is the separate,
 * explicit copy path). Verification: the recipe's documented status command,
 * else the file-based `fleet auth status`, else the user's confirmation.
 */
enum verdict { SIGNED_IN, SIGNED_OUT, UNKNOWN };

static const char *hint(const char *agent) {
    if (!strcmp(agent, "pi")) return "In pi, enter /login and follow the prompts, then /quit.";
    if (!strcmp(agent, "agy")) return "agy prints a sign-in URL: open it on this computer and paste the code back.";
    return NULL;
}
/* "exec 'PATH' 'ARG'...": the command runs through the remote login shell. */
static int append(char *out, size_t size, const char *prefix, const char *text, bool quote) {
    char *quoted = quote ? f_quote(text) : NULL; size_t used = strlen(out);
    int status = (quote && !quoted) || snprintf(out + used, size - used, "%s%s", prefix, quote ? quoted : text) >= (int)(size - used) ? -1 : 0;
    free(quoted);
    return status;
}
static int command_text(const char *path, json_object *args, const char *suffix, char *out, size_t size) {
    size_t i, count = json_object_is_type(args, json_type_array) ? json_object_array_length(args) : 0;
    out[0] = '\0';
    if (append(out, size, "exec ", path, true)) return -1;
    for (i = 0; i < count; i++)
        if (append(out, size, " ", f_text(json_object_array_get_idx(args, i)), true)) return -1;
    return suffix ? append(out, size, "", suffix, false) : 0;
}
static enum verdict status_command(struct setup_ctx *ctx, json_object *recipe, const char *path) {
    json_object *args = f_field(recipe, "auth_status_argv"); char command[F_PATH * 3]; struct f_capture cap = {0};
    enum verdict verdict = UNKNOWN;
    if (!args || command_text(path, args, " </dev/null >/dev/null 2>&1", command, sizeof(command))) return UNKNOWN;
    if (!f_ssh(&ctx->remote, command, NULL, 0, ctx->seconds, false, &cap) && !cap.timeout)
        verdict = !cap.status ? SIGNED_IN : cap.status == 1 ? SIGNED_OUT : UNKNOWN;
    f_capture_free(&cap);
    return verdict;
}
/* A present credential file proves sign-in; absence may mean a keyring. */
static enum verdict status_file(struct setup_ctx *ctx, const char *agent) {
    json_object *request, *reply; const char *state; enum verdict verdict;
    if (!auth_agent(agent) || !agent_remote_capability(&ctx->remote, "agent-auth", ctx->seconds)) return UNKNOWN;
    request = json_object_new_object();
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL));
    json_object_object_add(request, "auth_protocol", json_object_new_int(1));
    f_string_add(request, "action", "auth"); f_string_add(request, "operation", "status"); f_string_add(request, "agent", agent);
    reply = f_request(&ctx->remote, request, ctx->seconds);
    state = f_string(f_field(reply, "data"), "credential_state");
    verdict = json_object_get_boolean(f_field(reply, "ok")) && state && !strcmp(state, "present") ? SIGNED_IN : UNKNOWN;
    json_object_put(request); json_object_put(reply);
    return verdict;
}
static enum verdict verify(struct setup_ctx *ctx, json_object *recipe, const char *path, const char **method) {
    enum verdict verdict = status_command(ctx, recipe, path);
    *method = "provider status command";
    if (verdict != UNKNOWN) return verdict;
    *method = "credential file";
    return status_file(ctx, f_string(recipe, "agent"));
}
/* Records the step; *copy (optional) receives a separate copy of the detail. */
static int record(struct setup_ctx *ctx, const char *step, const char *status, const char *summary, const char *path,
                  const char *method, json_object **copy) {
    json_object *detail = json_object_new_object();
    f_string_add(detail, "summary", summary); f_string_add(detail, "path", path);
    if (method) f_string_add(detail, "verified_by", method);
    if (copy && json_object_deep_copy(detail, copy, NULL)) { json_object_put(detail); *copy = NULL; return -1; }
    if (!setup_state_step(ctx, step, status, detail)) return 0;
    if (copy) { json_object_put(*copy); *copy = NULL; }
    return -1;
}
/* A failed sign-in is recorded and can simply be retried. */
static json_object *failure(struct setup_ctx *ctx, const char *step, const char *summary, const char *path,
                            const char *code, const char *message, json_object *data) {
    (void)record(ctx, step, "failed", summary, path, NULL, NULL);
    return setup_error(ctx, code, message, "rerun hydra remote sign-in NAME --agent AGENT in a terminal to try again", data);
}
static json_object *success(struct setup_ctx *ctx, const char *step, const char *path, const char *method) {
    char summary[128]; json_object *detail = NULL;
    snprintf(summary, sizeof(summary), "signed in (verified by %s)", method);
    if (record(ctx, step, "done", summary, path, method, &detail))
        return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    f_string_add(detail, "credentials", "stay on the remote; Hydra copied nothing");
    return f_success(ctx->command, detail);
}
static json_object *conclude(struct setup_ctx *ctx, const char *step, json_object *recipe, const char *path) {
    const char *method; enum verdict verdict = verify(ctx, recipe, path, &method);
    if (verdict == SIGNED_IN) return success(ctx, step, path, method);
    if (verdict == SIGNED_OUT)
        return failure(ctx, step, "provider reports signed out", path, "sign_in_failed", "the provider reports that the agent is not signed in", NULL);
    if (setup_agent_tty() && setup_agent_confirm("Hydra cannot verify this sign-in on the remote. Did it complete?"))
        return success(ctx, step, path, "user confirmation");
    return failure(ctx, step, "sign-in not verified", path, "sign_in_unverified",
                   "Hydra cannot verify the sign-in and it was not confirmed", NULL);
}
static json_object *login(struct setup_ctx *ctx, const char *step, json_object *recipe, const char *path) {
    char command[F_PATH * 3]; int exit_status = -1; json_object *data, *progress; const char *text = hint(f_string(recipe, "agent"));
    if (!setup_agent_tty())
        return setup_error(ctx, "tty_required", "sign-in needs an interactive terminal",
                           "run hydra remote sign-in NAME --agent AGENT in a terminal without CI or HYDRA_NONINTERACTIVE", NULL);
    if (command_text(path, f_field(recipe, "login_args"), NULL, command, sizeof(command)))
        return setup_error(ctx, "invalid_plan", "the sign-in command is too long", NULL, NULL);
    progress = json_object_new_object(); f_string_add(progress, "summary", "sign-in started"); f_string_add(progress, "path", path);
    if (setup_state_step(ctx, step, "in_progress", progress))
        return setup_error(ctx, "state_unavailable", "cannot record setup progress; sign-in was not started", NULL, NULL);
    fputs("Signing in on the remote; credentials stay there and Hydra copies nothing.\n", stderr);
    if (text) { fputs(text, stderr); fputc('\n', stderr); }
    if (f_ssh_interactive(&ctx->remote, command, ctx->seconds, &exit_status))
        return failure(ctx, step, "ssh did not start", path, "sign_in_failed", "cannot start SSH for sign-in", NULL);
    if (!exit_status) return conclude(ctx, step, recipe, path);
    data = json_object_new_object(); json_object_object_add(data, "exit_status", json_object_new_int(exit_status));
    if (exit_status > 128 && exit_status != 255)
        return failure(ctx, step, "sign-in cancelled", path, "cancelled", "sign-in was interrupted", data);
    return failure(ctx, step, "sign-in exited with an error", path, "sign_in_failed", "the provider's sign-in exited with a nonzero status", data);
}
static json_object *sign_in(struct setup_ctx *ctx, const char *step, json_object *recipe) {
    const char *status = setup_state_status(ctx, step), *path;
    bool reconciling = !strcmp(status, "in_progress") || !strcmp(status, "outcome_unknown");
    json_object *error = NULL, *inventory = setup_inventory(ctx, &error), *result;
    if (!inventory) return error;
    path = strcmp(f_string(inventory, "source"), "receiver") ? NULL
        : agent_inventory_location(agent_inventory_row(inventory, f_string(recipe, "executable")));
    if (!path) {
        json_object *detail = json_object_new_object();
        f_string_add(detail, "summary", "agent not installed");
        (void)setup_state_step(ctx, step, "blocked", detail);
        result = setup_error(ctx, "agent_not_found", "the remote Hydra cannot resolve this agent's executable",
                             "install it with hydra remote install-agent NAME --agent AGENT, or record it with hydra remote agents NAME --record EXECUTABLE=/absolute/path", NULL);
    } else result = reconciling ? conclude(ctx, step, recipe, path) : login(ctx, step, recipe, path);
    json_object_put(inventory);
    return result;
}
json_object *setup_step_sign_in(struct setup_ctx *ctx, const char *agent) {
    char step[80]; json_object *error = NULL, *recipe, *result;
    snprintf(step, sizeof(step), "sign_in:%s", agent);
    recipe = setup_recipe(ctx, agent, &error);
    if (!recipe)
        return error ? error : setup_error(ctx, "recipe_unavailable", "Hydra has no sign-in recipe for this agent",
                                           "sign in on the remote with the provider's own instructions", NULL);
    if (setup_agent_select(ctx, agent)) { json_object_put(recipe); return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL); }
    result = sign_in(ctx, step, recipe);
    json_object_put(recipe);
    return result;
}
