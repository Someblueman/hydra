#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdlib.h>
#include <string.h>

#define SETUP_USAGE "remote setup NAME [DEST] [--ssh-config /abs] [--binary FILE] " \
    "[--approve PLAN_SHA256] [--timeout N] [--json]; remote setup status NAME; remote setup list; " \
    "remote setup remove NAME [--approve PLAN_SHA256]; " \
    "remote trust-key NAME [--fingerprint SHA256:...]; " \
    "remote preflight NAME; remote provision NAME [--approve PLAN_SHA256] [--binary FILE]; " \
    "remote agents NAME [--record EXECUTABLE=/abs/path]; remote install-agent NAME --agent A [--approve PLAN_SHA256]; " \
    "remote sign-in NAME --agent A (every command accepts --timeout N and --json)"

/* Value options; the index is the bit in spec.flags and the slot in values[]. */
enum { OPT_SSH_CONFIG, OPT_BINARY, OPT_FINGERPRINT, OPT_APPROVE, OPT_RECORD, OPT_AGENT, OPT_COUNT };
static const char *const option_names[OPT_COUNT] = {
    "--ssh-config", "--binary", "--fingerprint", "--approve", "--record", "--agent"
};
#define OPT(bit) (1U << (bit))
enum setup_kind { K_SETUP, K_STATUS, K_LIST, K_REMOVE, K_TRUST_KEY, K_PREFLIGHT, K_PROVISION, K_AGENTS, K_INSTALL_AGENT, K_SIGN_IN };
struct spec { const char *word, *command; enum setup_kind kind; unsigned flags; int positionals; };
static const struct spec specs[] = {
    {"setup", "remote-setup", K_SETUP, OPT(OPT_SSH_CONFIG) | OPT(OPT_BINARY) | OPT(OPT_APPROVE), 2},
    {"trust-key", "remote-trust-key", K_TRUST_KEY, OPT(OPT_FINGERPRINT), 1},
    {"preflight", "remote-preflight", K_PREFLIGHT, 0, 1},
    {"provision", "remote-provision", K_PROVISION, OPT(OPT_APPROVE) | OPT(OPT_BINARY), 1},
    {"agents", "remote-agents", K_AGENTS, OPT(OPT_RECORD), 1},
    {"install-agent", "remote-install-agent", K_INSTALL_AGENT, OPT(OPT_AGENT) | OPT(OPT_APPROVE), 1},
    {"sign-in", "remote-sign-in", K_SIGN_IN, OPT(OPT_AGENT), 1},
};
static const struct spec status_spec = {"status", "remote-setup-status", K_STATUS, 0, 1};
static const struct spec list_spec = {"list", "remote-setup-list", K_LIST, 0, 0};
static const struct spec remove_spec = {"remove", "remote-setup-remove", K_REMOVE, OPT(OPT_APPROVE), 1};

struct setup_options {
    const struct spec *spec;
    const char *positional[2], *values[OPT_COUNT];
    int count;
    unsigned seconds;
    bool json;
};

bool setup_command(const char *word) {
    size_t i;
    for (i = 0; word && i < sizeof(specs) / sizeof(specs[0]); i++) if (!strcmp(word, specs[i].word)) return true;
    return false;
}
static const struct spec *find_spec(const char *word) {
    size_t i;
    for (i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) if (!strcmp(word, specs[i].word)) return &specs[i];
    return NULL;
}
static json_object *usage_error(const char *command, const char *message) {
    json_object *result = f_error(command, "invalid_input", message);
    f_string_add(f_field(result, "error"), "recovery", "usage: " SETUP_USAGE);
    return result;
}

/* ---- Parsing ---- */
static int option_index(const char *arg, unsigned flags) {
    int i;
    for (i = 0; i < OPT_COUNT; i++) if ((flags & OPT(i)) && !strcmp(arg, option_names[i])) return i;
    return -1;
}
static bool parse_timeout(const char *text, unsigned *seconds) {
    char *end; unsigned long value;
    if (!text || !*text) return false;
    value = strtoul(text, &end, 10);
    if (*end || !value || value > 300) return false;
    *seconds = (unsigned)value;
    return true;
}
/* Returns the number of consumed arguments (1 or 2) or 0 for an error. */
static int parse_flag(struct setup_options *o, int argc, char **argv, int i) {
    const char *value = i + 1 < argc ? argv[i + 1] : NULL; int index;
    if (!strcmp(argv[i], "--json")) { o->json = true; return 1; }
    if (!value || value[0] == '-') return 0;
    if (!strcmp(argv[i], "--timeout")) return parse_timeout(value, &o->seconds) ? 2 : 0;
    index = option_index(argv[i], o->spec->flags);
    if (index < 0 || o->values[index]) return 0;
    o->values[index] = value;
    return 2;
}
static bool parse_arguments(struct setup_options *o, int argc, char **argv, int first) {
    int i = first, used;
    while (i < argc) {
        if (argv[i][0] != '-') {
            if (o->count >= o->spec->positionals) return false;
            o->positional[o->count++] = argv[i++];
            continue;
        }
        used = parse_flag(o, argc, argv, i);
        if (!used) return false;
        i += used;
    }
    return o->count >= 1 || o->spec->kind == K_LIST;
}
static bool hex_digest(const char *value) {
    return strlen(value) == 64 && strspn(value, "0123456789abcdef") == 64;
}
static bool fingerprint_text(const char *value) {
    size_t length = strlen(value);
    return !strncmp(value, "SHA256:", 7) && length > 7 && length < 256 &&
        strspn(value + 7, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") == length - 7;
}
static bool record_text(const char *value) {
    const char *equals = strchr(value, '='); char executable[128]; size_t length;
    if (!equals || equals[1] != '/' || strlen(equals + 1) >= F_PATH) return false;
    length = (size_t)(equals - value);
    if (length >= sizeof(executable)) return false;
    memcpy(executable, value, length); executable[length] = '\0';
    return f_name(executable);
}
static bool absolute_text(const char *value) { return value[0] == '/' && strlen(value) < F_PATH; }
static bool agent_text(const char *value) { return f_name(value) && strlen(value) < 64; }
static bool binary_text(const char *value) { return strlen(value) < F_PATH; }
static const char *validate_values(const struct setup_options *o) {
    static const struct { int option; bool (*valid)(const char *); const char *message; } rules[] = {
        {OPT_SSH_CONFIG, absolute_text, "--ssh-config requires an absolute path"},
        {OPT_FINGERPRINT, fingerprint_text, "--fingerprint requires SHA256:BASE64"},
        {OPT_APPROVE, hex_digest, "--approve requires the 64-digit plan_sha256"},
        {OPT_RECORD, record_text, "--record requires EXECUTABLE=/absolute/path"},
        {OPT_AGENT, agent_text, "--agent requires an agent name"},
        {OPT_BINARY, binary_text, "--binary path is too long"},
    };
    size_t i;
    for (i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
        const char *value = o->values[rules[i].option];
        if (value && !rules[i].valid(value)) return rules[i].message;
    }
    if ((o->spec->kind == K_INSTALL_AGENT || o->spec->kind == K_SIGN_IN) && !o->values[OPT_AGENT]) return "--agent is required";
    return NULL;
}
static json_object *parse(struct setup_options *o, int argc, char **argv) {
    const char *problem; int first = 1;
    memset(o, 0, sizeof(*o));
    o->seconds = 10;
    o->spec = argc >= 1 ? find_spec(argv[0]) : NULL;
    if (!o->spec) return usage_error("remote-setup", "unknown setup command");
    if (o->spec->kind == K_SETUP && argc >= 2 && !strcmp(argv[1], "status")) { o->spec = &status_spec; first = 2; }
    else if (o->spec->kind == K_SETUP && argc >= 2 && !strcmp(argv[1], "list")) { o->spec = &list_spec; first = 2; }
    else if (o->spec->kind == K_SETUP && argc >= 2 && !strcmp(argv[1], "remove")) { o->spec = &remove_spec; first = 2; }
    if (!parse_arguments(o, argc, argv, first)) return usage_error(o->spec->command, "missing NAME, unexpected argument, or invalid option");
    problem = validate_values(o);
    return problem ? usage_error(o->spec->command, problem) : NULL;
}

/* ---- Step order and next commands ---- */
struct step_list { char ids[2 + 2 * 16 + 4][80]; size_t count; };
static void list_add(struct step_list *list, const char *prefix, const char *agent) {
    if (list->count >= sizeof(list->ids) / sizeof(list->ids[0])) return;
    snprintf(list->ids[list->count++], sizeof(list->ids[0]), "%s%s%s", prefix, agent ? ":" : "", agent ? agent : "");
}
static void step_list(struct setup_ctx *ctx, struct step_list *list) {
    static const char *const head[] = {"host_key", "preflight", "provision", "agents"};
    json_object *selected = f_field(setup_state_detail(ctx, "agents"), "selected");
    size_t i, n = json_object_is_type(selected, json_type_array) ? json_object_array_length(selected) : 0;
    list->count = 0;
    for (i = 0; i < sizeof(head) / sizeof(head[0]); i++) list_add(list, head[i], NULL);
    for (i = 0; i < n && i < 16; i++) {
        const char *agent = f_text(json_object_array_get_idx(selected, i));
        if (!agent || !f_name(agent) || strlen(agent) >= 64) continue;
        list_add(list, "install_agent", agent); list_add(list, "sign_in", agent);
    }
    list_add(list, "verify", NULL); list_add(list, "alias", NULL);
}
static bool finished_status(const char *status) {
    return !strcmp(status, "done") || !strcmp(status, "skipped");
}
static const char *first_open_step(struct setup_ctx *ctx, struct step_list *list) {
    size_t i;
    step_list(ctx, list);
    for (i = 0; i < list->count; i++) if (!finished_status(setup_state_status(ctx, list->ids[i]))) return list->ids[i];
    return NULL;
}
bool setup_first_open(struct setup_ctx *ctx, char step[80]) {
    struct step_list list; const char *open = first_open_step(ctx, &list);
    return open && !f_copy(step, 80, open);
}
static void add_words(json_object *argv, const char *const *words) {
    for (; *words; words++) json_object_array_add(argv, json_object_new_string(*words));
}
static json_object *step_argv(const struct setup_ctx *ctx, const char *step) {
    const char *agent = strchr(step, ':'), *word = "setup";
    json_object *argv = json_object_new_array();
    const char *const prefix[] = {"hydra", "remote", NULL};
    if (!strcmp(step, "host_key")) word = "trust-key";
    else if (!strcmp(step, "preflight") || !strcmp(step, "provision") || !strcmp(step, "agents")) word = step;
    else if (agent) word = !strncmp(step, "sign_in:", 8) ? "sign-in" : "install-agent";
    add_words(argv, prefix);
    json_object_array_add(argv, json_object_new_string(word));
    json_object_array_add(argv, json_object_new_string(ctx->name));
    if (agent) { const char *const flag[] = {"--agent", agent + 1, NULL}; add_words(argv, flag); }
    if (ctx->json) json_object_array_add(argv, json_object_new_string("--json"));
    return argv;
}
json_object *setup_next_json(struct setup_ctx *ctx) {
    struct step_list list; const char *step = first_open_step(ctx, &list);
    json_object *next;
    if (!step) return NULL;
    next = json_object_new_object();
    f_string_add(next, "step", step);
    json_object_object_add(next, "argv", step_argv(ctx, step));
    json_object_object_add(next, "approval_sha256", NULL);
    return next;
}
/* steps[].error: the recorded failure of an interactive step while it is
 * failed or outcome_unknown (a later attempt replaces or retires it). */
static void add_step_error(struct setup_ctx *ctx, json_object *row, const char *step) {
    json_object *error = f_field(setup_state_detail(ctx, step), "error"), *copy;
    const char *status = setup_state_status(ctx, step);
    if (!f_string(error, "code") || (strcmp(status, "failed") && strcmp(status, "outcome_unknown"))) return;
    copy = json_object_new_object();
    f_string_add(copy, "code", f_string(error, "code"));
    f_string_add(copy, "message", f_string(error, "message") ? f_string(error, "message") : "");
    json_object_object_add(row, "error", copy);
}
static json_object *steps_json(struct setup_ctx *ctx) {
    struct step_list list; json_object *rows = json_object_new_array(); size_t i;
    step_list(ctx, &list);
    for (i = 0; i < list.count; i++) {
        json_object *row = json_object_new_object();
        const char *summary = f_string(setup_state_detail(ctx, list.ids[i]), "summary");
        f_string_add(row, "id", list.ids[i]);
        f_string_add(row, "status", setup_state_status(ctx, list.ids[i]));
        f_string_add(row, "detail", summary ? summary : "");
        add_step_error(ctx, row, list.ids[i]);
        json_object_array_add(rows, row);
    }
    return rows;
}
/* Adds the shared setup view to every envelope produced with open state. */
static void decorate(struct setup_ctx *ctx, json_object *result) {
    json_object *data = f_field(result, "data");
    f_string_add(result, "command", ctx->command);
    if (!ctx->state) return;
    if (!json_object_is_type(data, json_type_object)) {
        json_object *wrapped = json_object_new_object();
        if (data) json_object_object_add(wrapped, "result", json_object_get(data));
        json_object_object_add(result, "data", wrapped);
        data = wrapped;
    }
    json_object_object_add(data, "setup_schema", json_object_new_int(SETUP_SCHEMA));
    f_string_add(data, "name", ctx->name);
    f_string_add(data, "destination", ctx->remote.target);
    json_object_object_add(data, "steps", steps_json(ctx));
    if (!f_field(data, "next")) json_object_object_add(data, "next", setup_next_json(ctx));
}

/* ---- Execution ---- */
static json_object *run_agent_step(struct setup_ctx *ctx, const char *step) {
    const char *agent = strchr(step, ':') + 1;
    if (!strncmp(step, "sign_in:", 8)) return setup_step_sign_in(ctx, agent);
    return setup_step_install_agent(ctx, agent, NULL);
}
/* Upgrade: the install behind the alias already is this version. */
static bool reusable_install(struct setup_ctx *ctx) {
    json_object *found = f_field(setup_state_detail(ctx, "preflight"), "alias_hydra");
    const char *path = f_string(setup_upgrade(ctx), "hydra"), *checked = f_string(found, "path");
    return path && checked && !strcmp(path, checked) && json_object_get_boolean(f_field(found, "reusable"));
}
static json_object *reuse_install(struct setup_ctx *ctx) {
    json_object *detail = json_object_new_object();
    const char *path = f_string(setup_upgrade(ctx), "hydra");
    f_string_add(detail, "hydra", path);
    f_string_add(detail, "summary", "existing Hydra " F_VERSION " behind the alias is reused");
    if (f_copy(ctx->remote.hydra, sizeof(ctx->remote.hydra), path) || setup_state_step(ctx, "provision", "skipped", detail))
        return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    return f_success(ctx->command, json_object_new_object());
}
/* approve applies only to the final alias step of `remote setup --approve`. */
static json_object *run_step(struct setup_ctx *ctx, const char *step, const char *approve) {
    if (!strcmp(step, "host_key")) return setup_step_trust_key(ctx, NULL);
    if (!strcmp(step, "preflight")) return setup_step_preflight(ctx);
    if (!strcmp(step, "provision")) return reusable_install(ctx) ? reuse_install(ctx) : setup_step_provision(ctx, ctx->binary, NULL);
    if (!strcmp(step, "agents")) return setup_step_agents(ctx, NULL);
    if (!strcmp(step, "verify")) return setup_step_verify(ctx);
    if (!strcmp(step, "alias")) return setup_step_alias_approved(ctx, approve);
    return run_agent_step(ctx, step);
}
static json_object *cancelled(struct setup_ctx *ctx) {
    json_object *data = json_object_new_object();
    json_object_object_add(data, "exit_status", json_object_new_int(128 + (int)f_stopped));
    return setup_error(ctx, "cancelled", "setup was interrupted", "rerun hydra remote setup NAME to resume", data);
}
/* Runs every open step in order; state decides where a rerun resumes. */
static json_object *guided(struct setup_ctx *ctx, const char *approve) {
    struct step_list list; const char *step; json_object *result, *data;
    char current[80];
    while ((step = first_open_step(ctx, &list))) {
        if (f_stopped) return cancelled(ctx);
        f_copy(current, sizeof(current), step);
        result = run_step(ctx, current, approve);
        if (!json_object_get_boolean(f_field(result, "ok"))) return result;
        json_object_put(result);
        if (!finished_status(setup_state_status(ctx, current)) && setup_state_step(ctx, current, "done", NULL))
            return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    }
    data = json_object_new_object();
    json_object_object_add(data, "complete", json_object_new_boolean(true));
    return f_success(ctx->command, data);
}
static json_object *dispatch(struct setup_ctx *ctx, const struct setup_options *o) {
    const char *const *v = o->values;
    switch (o->spec->kind) {
    case K_SETUP: return guided(ctx, v[OPT_APPROVE]);
    case K_STATUS: return f_success(ctx->command, json_object_new_object());
    case K_LIST: break;
    case K_REMOVE: return setup_remove(ctx, v[OPT_APPROVE]);
    case K_TRUST_KEY: return setup_step_trust_key(ctx, v[OPT_FINGERPRINT]);
    case K_PREFLIGHT: return setup_step_preflight(ctx);
    case K_PROVISION: return setup_step_provision(ctx, v[OPT_BINARY], v[OPT_APPROVE]);
    case K_AGENTS: return setup_step_agents(ctx, v[OPT_RECORD]);
    case K_INSTALL_AGENT: return setup_step_install_agent(ctx, v[OPT_AGENT], v[OPT_APPROVE]);
    case K_SIGN_IN: return setup_step_sign_in(ctx, v[OPT_AGENT]);
    }
    return usage_error(ctx->command, "unknown setup command");
}
/* ---- Failed interactive steps ----
 * An installer or sign-in runs on the user's terminal, where its envelope is
 * not kept; record its error code and message in the step detail so `remote
 * setup status NAME --json` can explain the failure afterwards. */
static bool interactive_step(struct setup_ctx *ctx, const struct setup_options *o, char step[80]) {
    const char *agent = o->values[OPT_AGENT];
    if (o->spec->kind == K_INSTALL_AGENT || o->spec->kind == K_SIGN_IN)
        return snprintf(step, 80, "%s:%s", o->spec->kind == K_SIGN_IN ? "sign_in" : "install_agent", agent) < 80;
    if (o->spec->kind != K_SETUP || !setup_first_open(ctx, step)) return false;
    return !strncmp(step, "install_agent:", 14) || !strncmp(step, "sign_in:", 8);
}
static void record_failure(struct setup_ctx *ctx, const struct setup_options *o, json_object *result) {
    json_object *error = f_field(result, "error"), *detail = NULL, *record;
    char step[80], status[32];
    if (!f_string(error, "code") || !interactive_step(ctx, o, step) ||
        f_copy(status, sizeof(status), setup_state_status(ctx, step)) ||
        (strcmp(status, "failed") && strcmp(status, "outcome_unknown")))
        return;
    if (setup_state_detail(ctx, step) && json_object_deep_copy(setup_state_detail(ctx, step), &detail, NULL)) return;
    if (!detail) detail = json_object_new_object();
    record = json_object_new_object();
    f_string_add(record, "code", f_string(error, "code"));
    f_string_add(record, "message", f_string(error, "message") ? f_string(error, "message") : "");
    json_object_object_add(detail, "error", record);
    (void)setup_state_step(ctx, step, status, detail);
}
static unsigned open_flags(enum setup_kind kind) {
    if (kind == K_SETUP) return SETUP_CREATE;
    return kind == K_STATUS ? SETUP_READONLY : 0U;
}
/* ---- Upgrade mode: an enrolled alias without setup state ---- */
static json_object *upgrade_conflict(struct setup_ctx *ctx, const struct f_remote *alias) {
    char recovery[1024];
    snprintf(recovery, sizeof(recovery),
             "alias %s already points at %s and setup never re-points an alias; rerun hydra remote setup %s without DEST or "
             "--ssh-config to upgrade it, or choose another NAME", alias->name, alias->target, alias->name);
    return setup_error(ctx, "alias_conflict", "NAME is an existing alias with a different destination or SSH config", recovery, NULL);
}
static void seed_from_alias(struct f_remote *remote, const struct f_remote *alias) {
    f_copy(remote->home, sizeof(remote->home), alias->home);
    f_copy(remote->principal, sizeof(remote->principal), alias->principal);
    f_copy(remote->project, sizeof(remote->project), alias->project);
    f_copy(remote->accepted_host_key, sizeof(remote->accepted_host_key), alias->accepted_host_key);
    remote->multiplex = alias->multiplex;
}
static json_object *open_upgrade(struct setup_ctx *ctx, const struct setup_options *o, const struct f_remote *alias) {
    const char *dest = o->positional[1], *config = o->values[OPT_SSH_CONFIG];
    json_object *result, *upgrade;
    if ((dest && strcmp(dest, alias->target)) || (config && strcmp(config, alias->ssh_config))) return upgrade_conflict(ctx, alias);
    result = setup_state_open(ctx, alias->name, alias->target, alias->ssh_config[0] ? alias->ssh_config : NULL, SETUP_CREATE);
    if (result) return result;
    seed_from_alias(&ctx->remote, alias);
    upgrade = json_object_new_object();
    f_string_add(upgrade, "hydra", alias->hydra); f_string_add(upgrade, "target", alias->target);
    if (setup_state_set(ctx, "upgrade", upgrade))
        return setup_error(ctx, "state_unavailable", "cannot record the upgrade in setup state", NULL, NULL);
    return NULL;
}
/* Existing setup state always wins; otherwise an enrolled NAME is upgraded. */
static json_object *open_context(struct setup_ctx *ctx, const struct setup_options *o) {
    const char *name = o->positional[0], *dest = o->positional[1], *config = o->values[OPT_SSH_CONFIG], *code;
    struct f_remote alias; json_object *result;
    if (o->spec->kind != K_SETUP || f_remote_load(name, &alias)) return setup_state_open(ctx, name, dest, config, open_flags(o->spec->kind));
    result = setup_state_open(ctx, name, dest, config, 0);
    code = f_string(f_field(result, "error"), "code");
    if (!code || strcmp(code, "setup_not_started")) return result;
    json_object_put(result);
    setup_state_close(ctx);
    return open_upgrade(ctx, o, &alias);
}
json_object *setup_cli(int argc, char **argv) {
    struct setup_options options; struct setup_ctx ctx; json_object *result;
    if (argc >= 2 && (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help"))) {
        json_object *data = json_object_new_object();
        f_string_add(data, "usage", SETUP_USAGE);
        return f_success("remote-setup-help", data);
    }
    if ((result = parse(&options, argc, argv))) return result;
    if (!options.spec) return usage_error("remote-setup", "unknown setup command");
    if (options.spec->kind == K_LIST) return setup_list(options.json);
    ctx.command = options.spec->command;
    result = open_context(&ctx, &options);
    if (!result) {
        ctx.json = options.json; ctx.interactive = setup_interactive(options.json); ctx.seconds = options.seconds;
        ctx.binary = options.values[OPT_BINARY];
        result = dispatch(&ctx, &options);
        record_failure(&ctx, &options, result);
        decorate(&ctx, result);
    }
    setup_state_close(&ctx);
    return result;
}

/* ---- Output and exit status ---- */
static void print_safe(const char *text) {
    for (; text && *text; text++) fputc((unsigned char)*text < 0x20 || *text == 0x7f ? '?' : *text, stderr);
}
static const char *status_mark(const char *status) {
    static const char *const marks[][2] = {
        {"done", "\xe2\x9c\x93"}, {"skipped", "-"}, {"failed", "\xe2\x9c\x97"}, {"outcome_unknown", "?"},
        {"approval_required", "!"}, {"blocked", "!"}, {"in_progress", "~"}
    };
    size_t i;
    for (i = 0; status && i < sizeof(marks) / sizeof(marks[0]); i++) if (!strcmp(status, marks[i][0])) return marks[i][1];
    return " ";
}
static void print_steps(json_object *steps) {
    size_t i;
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *row = json_object_array_get_idx(steps, i); char label[80]; char *p;
        if (f_copy(label, sizeof(label), f_string(row, "id"))) continue;
        for (p = label; *p; p++) if (*p == '_') *p = ' ';
        fprintf(stderr, "%s %-22s ", status_mark(f_string(row, "status")), label);
        print_safe(f_string(row, "detail")[0] ? f_string(row, "detail") : f_string(row, "status"));
        fputc('\n', stderr);
    }
}
static bool shell_plain(const char *word) {
    return word[0] && strspn(word, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_@%+=:,./-") == strlen(word);
}
static void print_next(json_object *next) {
    json_object *argv = f_field(next, "argv"); size_t i;
    if (!json_object_is_type(argv, json_type_array)) return;
    fputs("next:", stderr);
    for (i = 0; i < json_object_array_length(argv); i++) {
        const char *word = f_text(json_object_array_get_idx(argv, i)); char *quoted;
        if (!word) continue;
        quoted = shell_plain(word) ? NULL : f_quote(word);
        fputc(' ', stderr); print_safe(quoted ? quoted : word); free(quoted);
    }
    fputc('\n', stderr);
}
static void print_error(json_object *error, json_object *data) {
    fputs("error: ", stderr); print_safe(f_string(error, "message"));
    fputs(" (", stderr); print_safe(f_string(error, "code")); fputs(")\n  ", stderr);
    print_safe(f_string(error, "recovery")); fputc('\n', stderr);
    if (f_string(data, "plan_sha256")) { fputs("plan sha256: ", stderr); print_safe(f_string(data, "plan_sha256")); fputc('\n', stderr); }
}
static void print_removed(json_object *data) {
    json_object *left = f_field(data, "remote_left"); size_t i;
    print_safe(f_string(data, "summary")); fputc('\n', stderr);
    for (i = 0; i < json_object_array_length(left); i++) {
        fputs("  still on the remote: ", stderr); print_safe(f_text(json_object_array_get_idx(left, i))); fputc('\n', stderr);
    }
}
static void print_setups(json_object *setups) {
    size_t i;
    if (!json_object_array_length(setups)) { fputs("no remote setups; start one with hydra remote setup NAME [USER@]HOST\n", stderr); return; }
    fprintf(stderr, "%-20s %-32s %-18s %s\n", "NAME", "DESTINATION", "STATUS", "NEXT");
    for (i = 0; i < json_object_array_length(setups); i++) {
        json_object *row = json_object_array_get_idx(setups, i);
        const char *destination = f_string(row, "destination"), *next = f_string(f_field(row, "next"), "step");
        char name[128], where[256], status[32];
        f_copy(name, sizeof(name), f_string(row, "name")); f_copy(where, sizeof(where), destination ? destination : "-");
        f_copy(status, sizeof(status), f_string(row, "status"));
        fprintf(stderr, "%-20s %-32s %-18s ", name, where, status);
        print_safe(next ? next : f_string(f_field(row, "error"), "code") ? f_string(f_field(row, "error"), "code") : "-");
        fputc('\n', stderr);
    }
}
int setup_emit(json_object *result, bool json) {
    json_object *data = f_field(result, "data"), *error = f_field(result, "error");
    bool ok = json_object_get_boolean(f_field(result, "ok"));
    if (json) return f_emit(result);
    if (json_object_is_type(f_field(data, "setups"), json_type_array)) print_setups(f_field(data, "setups"));
    if (ok && json_object_get_boolean(f_field(data, "removed"))) print_removed(data);
    if (json_object_is_type(f_field(data, "steps"), json_type_array)) print_steps(f_field(data, "steps"));
    if (!ok) print_error(error, data);
    else if (f_string(data, "usage")) { fputs("usage: ", stderr); print_safe(f_string(data, "usage")); fputc('\n', stderr); }
    if (f_field(data, "next")) print_next(f_field(data, "next"));
    else if (ok && f_field(data, "steps")) fputs("remote setup complete\n", stderr);
    fflush(stderr);
    return ok ? 0 : 1;
}
int setup_exit_status(json_object *result, int status) {
    const char *code = f_string(f_field(result, "error"), "code");
    json_object *exit_status = f_field(f_field(result, "data"), "exit_status");
    int value;
    if (!code) return status;
    if (!strcmp(code, "approval_required")) return 3;
    if (!strcmp(code, "outcome_unknown")) return 4;
    if (strcmp(code, "cancelled")) return status;
    value = json_object_is_type(exit_status, json_type_int) ? json_object_get_int(exit_status) : 0;
    if (value > 128 && value < 256) return value;
    return f_stopped ? 128 + (int)f_stopped : 130;
}
