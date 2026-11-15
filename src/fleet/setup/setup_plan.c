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
static const char *reason_recovery(const char *reason, const char *fallback) {
    static const struct { const char *text, *recovery; } causes[] = {
        {"Could not resolve hostname", "the destination's host name does not resolve: check its spelling, or give the SSH config file "
                                       "that defines this Host with --ssh-config (a setup bound to the wrong destination or config "
                                       "must be removed first: hydra remote setup remove NAME)"},
        {"Permission denied", "the host rejected your SSH credentials: check that ssh to this destination works in a terminal "
                              "(key, agent, user name)"},
        {"Connection refused", "nothing accepts SSH at that address and port: check the port and that the SSH server runs"},
        {"timed out", "the host did not answer: check your network or VPN and that the machine is running"},
        {"No route to host", "the host is unreachable: check your network or VPN and that the machine is running"},
        {"Network is unreachable", "the network is unreachable: check your connection or VPN"},
    };
    size_t i;
    for (i = 0; reason && i < sizeof(causes) / sizeof(causes[0]); i++)
        if (strstr(reason, causes[i].text)) return causes[i].recovery;
    return fallback;
}
json_object *setup_transport_error(const struct setup_ctx *ctx, const char *code, const char *what,
                                   const char *err, const char *recovery) {
    char reason[512], message[800]; json_object *data = NULL;
    f_ssh_reason(err ? err : "", reason, sizeof(reason));
    if (reason[0]) {
        snprintf(message, sizeof(message), "%s: %s", what, reason);
        data = json_object_new_object();
        f_string_add(data, "ssh_error", reason);
    } else snprintf(message, sizeof(message), "%s", what);
    return setup_error(ctx, code, message, reason_recovery(reason, recovery), data);
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
/* ---- The plan on a terminal ----
 * The same canonical plan the hash binds, as labelled rows: nested members
 * flattened (binary.sha256), lists joined, and the members that only
 * identify the plan (schema, kind, name, destination) left out. */
static const char *const plan_labels[][2] = {
    {"fingerprint", "Fingerprint"}, {"key_type", "Key type"}, {"host", "Host"}, {"known_hosts", "Will be added to"},
    {"hashed", "Hashed entry"}, {"change", "Change"}, {"platform", "Platform"}, {"hydra_version", "Hydra version"},
    {"binary.trust", "Trust"}, {"binary.source", "Binary source"}, {"binary.name", "Binary"}, {"binary.sha256", "Binary SHA-256"},
    {"package_sha256", "Package SHA-256"}, {"prefix", "Install location"}, {"changes", "Changes"}, {"agent", "Agent"},
    {"executable", "Executable"}, {"command", "Command"}, {"effect", "Runs"}, {"source", "Recipe"}, {"docs_url", "Provider docs"},
    {"verified_on", "Recipe checked"}, {"requires", "Requires"}, {"expected_dirs", "Installs into"}, {"peer_fingerprint", "Host key"},
};
static const char *const plan_values[][3] = {
    {"binary.trust", "pinned", "pinned: its digest is recorded in this Hydra release"},
    {"binary.trust", "unpinned", "unpinned: not a Hydra release binary; approve only if you trust where it came from"},
    {"binary.source", "release-asset", "Hydra release download, checked against the pinned digest"},
    {"binary.source", "local-helper", "this machine's own hydra-fleet helper"},
    {"binary.source", "binary", "the file passed with --binary"},
    {"source", "built-in", "built into this Hydra release"},
    {"source", "local", "LOCAL recipe from $HYDRA_HOME/fleet/agent-recipes.json"},
};
static bool plan_hidden(const char *key) {
    static const char *const hidden[] = {"schema", "schema_version", "kind", "name", "destination"};
    size_t i;
    for (i = 0; i < sizeof(hidden) / sizeof(hidden[0]); i++) if (!strcmp(key, hidden[i])) return true;
    return false;
}
static const char *plan_label(const char *key) {
    size_t i;
    for (i = 0; i < sizeof(plan_labels) / sizeof(plan_labels[0]); i++) if (!strcmp(key, plan_labels[i][0])) return plan_labels[i][1];
    return NULL;
}
/* A member by dotted path ("binary.sha256"); NULL when absent. */
static json_object *plan_member(json_object *plan, const char *path) {
    char head[64]; const char *dot = strchr(path, '.');
    if (!dot) return f_field(plan, path);
    if ((size_t)(dot - path) >= sizeof(head)) return NULL;
    memcpy(head, path, (size_t)(dot - path)); head[dot - path] = '\0';
    return plan_member(f_field(plan, head), dot + 1);
}
static void print_value(const char *key, json_object *value) {
    size_t i;
    if (json_object_is_type(value, json_type_array)) {
        for (i = 0; i < json_object_array_length(value); i++) {
            json_object *item = json_object_array_get_idx(value, i);
            if (i) fputs(", ", stderr);
            print_safe(json_object_is_type(item, json_type_string) ? json_object_get_string(item) : json_object_to_json_string(item));
        }
        return;
    }
    if (json_object_is_type(value, json_type_boolean)) { fputs(json_object_get_boolean(value) ? "yes" : "no", stderr); return; }
    for (i = 0; i < sizeof(plan_values) / sizeof(plan_values[0]); i++)
        if (!strcmp(key, plan_values[i][0]) && json_object_is_type(value, json_type_string) && !strcmp(json_object_get_string(value), plan_values[i][1])) {
            fputs(plan_values[i][2], stderr);
            return;
        }
    print_safe(json_object_is_type(value, json_type_string) ? json_object_get_string(value) : json_object_to_json_string(value));
}
static void print_row(const char *label, const char *path, json_object *value) {
    fprintf(stderr, "  %-18s", label);
    print_value(path, value);
    fputc('\n', stderr);
}
/* Members without a label of their own, after the labelled ones. */
static void print_rest(json_object *object, const char *prefix) {
    json_object_object_foreach(object, key, value) {
        char path[128];
        snprintf(path, sizeof(path), "%s%s%s", prefix, prefix[0] ? "." : "", key);
        if ((!prefix[0] && plan_hidden(key)) || plan_label(path)) continue;
        if (json_object_is_type(value, json_type_object)) print_rest(value, path);
        else print_row(path, path, value);
    }
}
static void print_rows(json_object *plan) {
    size_t i;
    for (i = 0; i < sizeof(plan_labels) / sizeof(plan_labels[0]); i++) {
        json_object *value = plan_member(plan, plan_labels[i][0]);
        if (value && !json_object_is_type(value, json_type_object)) print_row(plan_labels[i][1], plan_labels[i][0], value);
    }
    print_rest(plan, "");
}
static void print_title(json_object *plan) {
    const char *kind = f_string(plan, "kind"), *name = f_string(plan, "name");
    if (!kind || !name) { fputs("Review this remote setup plan:\n", stderr); return; }
    if (!strcmp(kind, "host_key")) fputs("Trust the host key of ", stderr);
    else if (!strcmp(kind, "provision")) fputs("Install Hydra on ", stderr);
    else if (!strcmp(kind, "install_agent")) { fputs("Install ", stderr); print_safe(f_string(plan, "agent") ? f_string(plan, "agent") : "an agent"); fputs(" on ", stderr); }
    else if (!strcmp(kind, "remove")) fputs("Remove the setup record of ", stderr);
    else { fputs("Review this plan for ", stderr); }
    print_safe(name);
    fputs(!strcmp(kind, "host_key") || !strcmp(kind, "remove") ? "?\n" : "\n", stderr);
}
/* How to read the presented key's fingerprint on the machine itself. */
static void print_key_check(json_object *plan) {
    const char *type = f_string(plan, "key_type");
    char lower[32]; size_t i;
    if (!type || strlen(type) >= sizeof(lower) || strspn(type, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") != strlen(type)) return;
    for (i = 0; type[i]; i++) lower[i] = (char)((type[i] >= 'A' && type[i] <= 'Z') ? type[i] - 'A' + 'a' : type[i]);
    lower[i] = '\0';
    fprintf(stderr, "Compare the fingerprint with what this prints on the machine itself:\n  ssh-keygen -lf /etc/ssh/ssh_host_%s_key.pub\n", lower);
}
static void print_plan(json_object *plan, const char *digest, bool host_key) {
    print_title(plan);
    print_rows(plan);
    if (host_key) print_key_check(plan);
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
    if (ctx->readonly || !setup_step_id(step) || !strcmp(status, "in_progress") || !strcmp(status, "outcome_unknown")) return;
    (void)setup_state_step(ctx, step, "approval_required", NULL);
}
static int gate_prepare(struct setup_ctx *ctx, struct gate *gate, json_object *plan) {
    char kind[32]; size_t length = strcspn(gate->step, ":");
    /* "remove" gates deleting the setup record itself; it is not a step. */
    if ((!setup_step_id(gate->step) && strcmp(gate->step, "remove")) || !gate->argv || length >= sizeof(kind)) return -1;
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
        print_plan(gate.plan, gate.digest, gate.host_key);
        if (!confirmed(gate.host_key))
            result = setup_error(ctx, "approval_declined", "the plan was not approved; nothing was changed", "rerun when you are ready to review the plan again", NULL);
    } else {
        record_waiting(ctx, step);
        result = approval_error(ctx, &gate, "approval_required", !strcmp(step, "remove")
                                ? "removing the setup record needs explicit approval; nothing on the remote changes"
                                : "this step changes the remote and needs explicit approval");
    }
    json_object_put(gate.plan);
    return result;
}
