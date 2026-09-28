#include "fleet/setup/setup.h"
#include "fleet/auth/agent_auth.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

json_object *setup_error(const struct setup_ctx *ctx, const char *code, const char *message,
                         const char *recovery, json_object *data) {
    json_object *result = f_error(ctx && ctx->command ? ctx->command : "remote-setup", code, message);
    f_string_add(f_field(result, "error"), "recovery",
                 recovery ? recovery : "inspect hydra remote setup status NAME, then rerun the reported next command");
    if (data) json_object_object_add(result, "data", data);
    return result;
}
static bool env_set(const char *name) {
    const char *value = getenv(name);
    return value && value[0];
}
bool setup_interactive(bool json) {
    return !json && isatty(STDIN_FILENO) && isatty(STDERR_FILENO) && !env_set("CI") && !env_set("HYDRA_NONINTERACTIVE");
}

static int compare_keys(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}
static json_object *canonical_copy(json_object *value);
static json_object *canonical_object(json_object *value) {
    size_t count = (size_t)json_object_object_length(value), n = 0, i;
    const char **keys = malloc((count ? count : 1) * sizeof(*keys));
    json_object *out;
    if (!keys) return NULL;
    json_object_object_foreach(value, key, child) { (void)child; if (n < count) keys[n++] = key; }
    qsort(keys, n, sizeof(*keys), compare_keys);
    out = json_object_new_object();
    for (i = 0; i < n; i++) json_object_object_add(out, keys[i], canonical_copy(f_field(value, keys[i])));
    free(keys);
    return out;
}
/* Sorted deep copy; scalars are shared by reference. */
static json_object *canonical_copy(json_object *value) {
    json_object *out; size_t i;
    if (json_object_is_type(value, json_type_object)) return canonical_object(value);
    if (!json_object_is_type(value, json_type_array)) return json_object_get(value);
    out = json_object_new_array();
    for (i = 0; i < json_object_array_length(value); i++)
        json_object_array_add(out, canonical_copy(json_object_array_get_idx(value, i)));
    return out;
}
int setup_plan_hash(const struct setup_ctx *ctx, const char *kind, json_object *plan,
                    json_object **canonical, char digest[65]) {
    json_object *copy = NULL, *sorted; int status;
    if (canonical) *canonical = NULL;
    if (!kind || !json_object_is_type(plan, json_type_object) || json_object_deep_copy(plan, &copy, NULL)) return -1;
    f_string_add(copy, "schema", "remote-setup-plan");
    json_object_object_add(copy, "schema_version", json_object_new_int(1));
    f_string_add(copy, "kind", kind); f_string_add(copy, "name", ctx->name);
    f_string_add(copy, "destination", ctx->remote.target);
    if (!f_field(copy, "peer_fingerprint") && ctx->remote.accepted_host_key[0])
        f_string_add(copy, "peer_fingerprint", ctx->remote.accepted_host_key);
    sorted = canonical_copy(copy); json_object_put(copy);
    if (!sorted) return -1;
    status = auth_hash(json_object_to_json_string_ext(sorted, JSON_C_TO_STRING_PLAIN), digest);
    if (!status && canonical) *canonical = sorted;
    else json_object_put(sorted);
    return status ? -1 : 0;
}

static json_object *next_argv(const struct setup_ctx *ctx, const char *const *argv, const char *flag, const char *token) {
    json_object *out = json_object_new_array(); size_t i;
    json_object_array_add(out, json_object_new_string("hydra"));
    json_object_array_add(out, json_object_new_string("remote"));
    for (i = 0; argv[i]; i++) json_object_array_add(out, json_object_new_string(argv[i]));
    json_object_array_add(out, json_object_new_string(flag));
    json_object_array_add(out, json_object_new_string(token));
    if (ctx->json) json_object_array_add(out, json_object_new_string("--json"));
    return out;
}
struct gate {
    const char *step, *token, *const *argv;
    bool host_key;
    json_object *plan; /* canonical, owned until handed to an envelope */
    char digest[65];
};
static json_object *approval_error(const struct setup_ctx *ctx, struct gate *gate, const char *code, const char *message) {
    json_object *data = json_object_new_object(), *next = json_object_new_object();
    json_object_object_add(data, "plan", gate->plan); gate->plan = NULL;
    f_string_add(data, "plan_sha256", gate->digest);
    f_string_add(next, "step", gate->step);
    json_object_object_add(next, "argv", next_argv(ctx, gate->argv, gate->host_key ? "--fingerprint" : "--approve", gate->token));
    f_string_add(next, "approval_sha256", gate->digest);
    json_object_object_add(data, "next", next);
    return setup_error(ctx, code, message,
                       gate->host_key ? "compare the fingerprint with one obtained from the host out of band, then rerun next.argv"
                                      : "review data.plan, then rerun next.argv to approve exactly this plan", data);
}
/* Terminal-safe rendering of remote-influenced text. */
static void print_safe(const char *text) {
    for (; *text; text++) fputc((unsigned char)*text < 0x20 || *text == 0x7f ? '?' : *text, stderr);
}
static void print_plan(json_object *plan, const char *digest) {
    fputs("Review this remote setup plan:\n", stderr);
    json_object_object_foreach(plan, key, value) {
        fprintf(stderr, "  %s: ", key);
        print_safe(json_object_is_type(value, json_type_string) ? json_object_get_string(value)
                                                                : json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN));
        fputc('\n', stderr);
    }
    fprintf(stderr, "  plan sha256: %s\n", digest);
}
static bool confirmed(bool host_key) {
    char line[64]; size_t n;
    fputs(host_key ? "Type yes to trust this host key: " : "Proceed? [y/N] ", stderr);
    fflush(stderr);
    if (!fgets(line, sizeof(line), stdin)) return false;
    n = strcspn(line, "\r\n"); line[n] = '\0';
    if (host_key) return !strcmp(line, "yes");
    return !strcmp(line, "y") || !strcmp(line, "Y") || !strcmp(line, "yes") || !strcmp(line, "YES");
}
static void record_waiting(struct setup_ctx *ctx, const char *step) {
    const char *status = setup_state_status(ctx, step);
    if (ctx->readonly || !strcmp(status, "in_progress") || !strcmp(status, "outcome_unknown")) return;
    (void)setup_state_step(ctx, step, "approval_required", NULL);
}
static int gate_prepare(struct setup_ctx *ctx, struct gate *gate, json_object *plan) {
    char kind[32]; size_t length = strcspn(gate->step, ":");
    if (!setup_step_id(gate->step) || !gate->argv || length >= sizeof(kind)) return -1;
    memcpy(kind, gate->step, length); kind[length] = '\0';
    gate->host_key = !strcmp(kind, "host_key");
    if (setup_plan_hash(ctx, kind, plan, &gate->plan, gate->digest)) return -1;
    gate->token = gate->host_key ? f_string(gate->plan, "fingerprint") : gate->digest;
    return gate->token && gate->token[0] ? 0 : -1;
}
json_object *setup_plan_gate(struct setup_ctx *ctx, const char *step, json_object *plan,
                             const char *approve, const char *const *argv) {
    struct gate gate = {.step = step, .argv = argv};
    json_object *result = NULL;
    if (!step || gate_prepare(ctx, &gate, plan)) {
        json_object_put(gate.plan);
        return setup_error(ctx, "invalid_plan", "cannot build the canonical setup plan", NULL, NULL);
    }
    if (approve) {
        if (strcmp(approve, gate.token))
            result = approval_error(ctx, &gate, "approval_mismatch", gate.host_key ? "the host key fingerprint does not match the approved value"
                                                                                  : "the approved hash does not match the current plan");
    } else if (ctx->interactive) {
        print_plan(gate.plan, gate.digest);
        if (!confirmed(gate.host_key))
            result = setup_error(ctx, "approval_declined", "the plan was not approved; nothing was changed", "rerun when you are ready to review the plan again", NULL);
    } else {
        record_waiting(ctx, step);
        result = approval_error(ctx, &gate, "approval_required", "this step changes the remote and needs explicit approval");
    }
    json_object_put(gate.plan);
    return result;
}
