#include "fleet/setup/agent_setup.h"
#include "fleet/agent/agent.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Agent inventory, selection and installers (design §6-7). */
static const struct { const char *profile, *executable; } known_agents[] = {
    {"claude", "claude"}, {"codex", "codex"}, {"cursor", "cursor-agent"}, {"agy", "agy"}, {"opencode", "opencode"},
    {"pi", "pi"}, {"copilot", "copilot"}, {"aider", "aider"}, {"gemini", "gemini"}, {NULL, NULL}
};
#define INSTALL_EFFECT "runs on the remote as your user over ssh -t; no sudo; Hydra adds no symlinks and no PATH changes"

/* ---- Small shared helpers ---- */
static void print_safe(const char *text) {
    for (; text && *text; text++) fputc((unsigned char)*text < 0x20 || *text == 0x7f ? '?' : *text, stderr);
}
static bool array(json_object *value) { return json_object_is_type(value, json_type_array); }
static bool env_set(const char *name) { const char *value = getenv(name); return value && *value; }
bool setup_agent_tty(void) {
    return isatty(STDIN_FILENO) && isatty(STDERR_FILENO) && !env_set("CI") && !env_set("HYDRA_NONINTERACTIVE");
}
bool setup_agent_confirm(const char *question) {
    char line[32]; size_t n;
    print_safe(question); fputs(" [y/N] ", stderr); fflush(stderr);
    if (!fgets(line, sizeof(line), stdin)) return false;
    n = strcspn(line, "\r\n"); line[n] = '\0';
    return !strcmp(line, "y") || !strcmp(line, "Y") || !strcmp(line, "yes");
}
static const char *known_executable(const char *agent) {
    size_t i;
    for (i = 0; known_agents[i].profile; i++) if (!strcmp(agent, known_agents[i].profile)) return known_agents[i].executable;
    return NULL;
}
static bool listed(json_object *list, const char *value) {
    size_t i;
    for (i = 0; array(list) && i < json_object_array_length(list); i++) {
        const char *item = f_text(json_object_array_get_idx(list, i));
        if (item && !strcmp(item, value)) return true;
    }
    return false;
}
/* Records status and detail, returning a separate copy as envelope data. */
static json_object *finished(struct setup_ctx *ctx, const char *step, const char *status, json_object *detail) {
    json_object *copy = NULL;
    if (json_object_deep_copy(detail, &copy, NULL)) copy = NULL;
    if (!copy || setup_state_step(ctx, step, status, detail)) {
        json_object_put(copy);
        return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    }
    return f_success(ctx->command, copy);
}
/* Records a failed/blocked/outcome_unknown step, then returns the error. */
static json_object *stopped(struct setup_ctx *ctx, const char *step, const char *status, const char *summary,
                            const char *code, const char *message, const char *recovery, json_object *data) {
    json_object *detail = json_object_new_object();
    f_string_add(detail, "summary", summary);
    (void)setup_state_step(ctx, step, status, detail);
    return setup_error(ctx, code, message, recovery, data);
}
static json_object *argv_json(const struct setup_ctx *ctx, const char *const *words) {
    json_object *argv = json_object_new_array();
    json_object_array_add(argv, json_object_new_string("hydra"));
    json_object_array_add(argv, json_object_new_string("remote"));
    for (; *words; words++) json_object_array_add(argv, json_object_new_string(*words));
    if (ctx->json) json_object_array_add(argv, json_object_new_string("--json"));
    return argv;
}

/* ---- Inventory ---- */
static const char *candidate_path(json_object *item) {
    const char *path = json_object_is_type(item, json_type_string) ? f_text(item) : f_string(item, "path");
    return path && path[0] == '/' && strlen(path) < F_PATH ? path : NULL;
}
static bool directory_on_path(const char *path_env, const char *file) {
    size_t length = (size_t)(strrchr(file, '/') - file);
    const char *cursor = path_env;
    while (cursor && *cursor) {
        const char *end = strchr(cursor, ':');
        size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
        if (n == length && !strncmp(cursor, file, n)) return true;
        cursor = end ? end + 1 : NULL;
    }
    return false;
}
static void snapshot_candidate(json_object *candidates, const char *path, bool on_path) {
    json_object *candidate = json_object_new_object();
    f_string_add(candidate, "path", path); f_string_add(candidate, "source", on_path ? "path" : "search");
    json_object_object_add(candidate, "version", NULL);
    json_object_object_add(candidate, "recordable", json_object_new_boolean(true));
    json_object_array_add(candidates, candidate);
}
static const char *snapshot_status(const char *hit, size_t off_path) {
    if (hit) return "on_path";
    if (off_path == 1) return "found_off_path";
    return off_path ? "ambiguous" : "missing";
}
/* One inventory row from the preflight's raw candidates (pre-2.9 receivers). */
static json_object *snapshot_row(json_object *snapshot, const char *profile, const char *executable) {
    json_object *row = json_object_new_object(), *candidates = json_object_new_array(), *list = f_field(snapshot, "agents");
    const char *path_env = f_string(snapshot, "path"), *hit = NULL; size_t i, off_path = 0;
    for (i = 0; array(list) && i < json_object_array_length(list); i++) {
        json_object *item = json_object_array_get_idx(list, i), *flag = f_field(item, "on_path");
        const char *path = candidate_path(item);
        bool on_path;
        if (!path || strcmp(strrchr(path, '/') + 1, executable)) continue;
        on_path = json_object_is_type(flag, json_type_boolean) ? json_object_get_boolean(flag) : path_env && directory_on_path(path_env, path);
        if (on_path && !hit) hit = path;
        else off_path++;
        snapshot_candidate(candidates, path, on_path);
    }
    f_string_add(row, "profile", profile); f_string_add(row, "executable", executable);
    f_string_add(row, "on_path", hit);
    json_object_object_add(row, "candidates", candidates);
    json_object_object_add(row, "recorded", NULL);
    f_string_add(row, "status", snapshot_status(hit, off_path));
    return row;
}
static json_object *snapshot_inventory(json_object *snapshot) {
    json_object *inventory = json_object_new_object(), *agents = json_object_new_array(); size_t i;
    f_string_add(inventory, "schema", "agent-inventory");
    json_object_object_add(inventory, "schema_version", json_object_new_int(1));
    f_string_add(inventory, "source", "preflight");
    json_object_object_add(inventory, "search_dirs", setup_search_dirs_json());
    for (i = 0; known_agents[i].profile; i++)
        json_object_array_add(agents, snapshot_row(snapshot, known_agents[i].profile, known_agents[i].executable));
    json_object_object_add(inventory, "agents", agents);
    return inventory;
}
/* live: installers and sign-in need the receiver, never the older snapshot. */
static json_object *inventory_of(struct setup_ctx *ctx, bool live, json_object **error) {
    json_object *inventory = NULL, *snapshot = setup_state_detail(ctx, "preflight");
    *error = NULL;
    if (ctx->remote.hydra[0] == '/' && (inventory = agent_remote_inventory(&ctx->remote, ctx->seconds))) {
        f_string_add(inventory, "source", "receiver");
        return inventory;
    }
    if (!live && array(f_field(snapshot, "agents"))) return snapshot_inventory(snapshot);
    *error = setup_error(ctx, live ? "capability_unavailable" : "prerequisite_missing",
                         "the remote Hydra did not answer an agent inventory",
                         "complete hydra remote preflight and provision for NAME (Hydra 2.9 or later), then rerun", NULL);
    return NULL;
}
json_object *setup_inventory(struct setup_ctx *ctx, json_object **error) { return inventory_of(ctx, false, error); }

/* ---- Selection ---- */
int setup_agent_select(struct setup_ctx *ctx, const char *agent) {
    json_object *detail = setup_state_detail(ctx, "agents"), *copy = NULL, *selected;
    char status[32];
    if (detail && json_object_deep_copy(detail, &copy, NULL)) return -1;
    if (!copy) copy = json_object_new_object();
    selected = f_field(copy, "selected");
    if (!array(selected)) { selected = json_object_new_array(); json_object_object_add(copy, "selected", selected); }
    if (listed(selected, agent)) { json_object_put(copy); return 0; }
    json_object_array_add(selected, json_object_new_string(agent));
    if (f_copy(status, sizeof(status), setup_state_status(ctx, "agents"))) { json_object_put(copy); return -1; }
    return setup_state_step(ctx, "agents", status, copy);
}
static bool has_recipe(const struct setup_ctx *ctx, const char *agent) {
    json_object *error = NULL, *recipe = setup_recipe(ctx, agent, &error);
    bool found = recipe != NULL;
    json_object_put(recipe); json_object_put(error);
    return found;
}
/* Terminal-safe bounded copy of remote-influenced text. */
static const char *safe_copy(char *out, size_t size, const char *text) {
    size_t i;
    for (i = 0; text && text[i] && i + 1 < size; i++) out[i] = (unsigned char)text[i] < 0x20 || text[i] == 0x7f ? '?' : text[i];
    out[i] = '\0';
    return out;
}
static void print_row(const struct setup_ctx *ctx, json_object *row) {
    const char *profile = f_string(row, "profile"), *status = f_string(row, "status"), *where = agent_inventory_location(row);
    char name[24], state[24];
    fprintf(stderr, "  %-10s %-15s ", safe_copy(name, sizeof(name), profile), safe_copy(state, sizeof(state), status));
    if (where) print_safe(where);
    else if (profile && status && !strcmp(status, "missing") && has_recipe(ctx, profile)) fputs("(installer available)", stderr);
    fputc('\n', stderr);
}
static void print_inventory(const struct setup_ctx *ctx, json_object *inventory) {
    json_object *agents = f_field(inventory, "agents"); size_t i;
    fputs("Agents on ", stderr); print_safe(ctx->name); fputs(" (", stderr); print_safe(f_string(inventory, "source")); fputs("):\n", stderr);
    for (i = 0; array(agents) && i < json_object_array_length(agents); i++) print_row(ctx, json_object_array_get_idx(agents, i));
}
static bool selectable(const struct setup_ctx *ctx, json_object *inventory, const char *agent) {
    const char *executable = f_name(agent) && strlen(agent) < 64 ? known_executable(agent) : NULL;
    const char *status = executable ? f_string(agent_inventory_row(inventory, executable), "status") : NULL;
    if (!f_name(agent) || strlen(agent) >= 64) return false;
    return has_recipe(ctx, agent) || (status && strcmp(status, "missing"));
}
static void choose(struct setup_ctx *ctx, json_object *inventory, json_object *selected) {
    char line[512], *word, *save = NULL;
    fputs("Agents to install or sign in (names separated by spaces; Enter for none): ", stderr); fflush(stderr);
    if (!fgets(line, sizeof(line), stdin)) return;
    for (word = strtok_r(line, " ,\t\r\n", &save); word; word = strtok_r(NULL, " ,\t\r\n", &save)) {
        if (!selectable(ctx, inventory, word)) { fputs("  ignored ", stderr); print_safe(word); fputs(": not installed and no installer recipe\n", stderr); continue; }
        if (!listed(selected, word)) json_object_array_add(selected, json_object_new_string(word));
    }
}
static const char *summary_of(json_object *inventory, json_object *selected, char *out, size_t size) {
    json_object *agents = f_field(inventory, "agents"); size_t i, present = 0, off = 0;
    for (i = 0; array(agents) && i < json_object_array_length(agents); i++) {
        const char *status = f_string(json_object_array_get_idx(agents, i), "status");
        if (status && (!strcmp(status, "on_path") || !strcmp(status, "recorded"))) present++;
        else if (status && strcmp(status, "missing")) off++;
    }
    snprintf(out, size, "%zu usable, %zu off PATH, %zu selected", present, off, array(selected) ? json_object_array_length(selected) : 0);
    return out;
}
static json_object *choices(const struct setup_ctx *ctx, json_object *inventory) {
    json_object *list = json_object_new_array(); size_t i;
    for (i = 0; known_agents[i].profile; i++) {
        const char *agent = known_agents[i].profile;
        const char *status = f_string(agent_inventory_row(inventory, known_agents[i].executable), "status");
        const char *const install[] = {"install-agent", ctx->name, "--agent", agent, NULL};
        const char *const sign_in[] = {"sign-in", ctx->name, "--agent", agent, NULL};
        json_object *choice;
        if (!has_recipe(ctx, agent)) continue;
        choice = json_object_new_object();
        f_string_add(choice, "agent", agent); f_string_add(choice, "status", status);
        json_object_object_add(choice, "install_argv", status && !strcmp(status, "missing") ? argv_json(ctx, install) : NULL);
        json_object_object_add(choice, "sign_in_argv", argv_json(ctx, sign_in));
        json_object_array_add(list, choice);
    }
    return list;
}
static json_object *agents_detail(struct setup_ctx *ctx, json_object *inventory) {
    json_object *detail = json_object_new_object(), *previous = f_field(setup_state_detail(ctx, "agents"), "selected"), *selected = NULL;
    char summary[128];
    if (!array(previous) || json_object_deep_copy(previous, &selected, NULL)) selected = json_object_new_array();
    if (ctx->interactive) choose(ctx, inventory, selected);
    f_string_add(detail, "summary", summary_of(inventory, selected, summary, sizeof(summary)));
    f_string_add(detail, "source", f_string(inventory, "source"));
    json_object_object_add(detail, "agents", json_object_get(f_field(inventory, "agents")));
    json_object_object_add(detail, "selected", selected);
    return detail;
}
static json_object *record_location(struct setup_ctx *ctx, const char *record) {
    char executable[128]; const char *path = strchr(record, '=') + 1; const char *args[] = {executable, path, NULL};
    json_object *reply, *result = NULL; const char *code, *message;
    if ((size_t)(path - record) > sizeof(executable)) return setup_error(ctx, "invalid_input", "executable name is too long", NULL, NULL);
    memcpy(executable, record, (size_t)(path - record - 1)); executable[path - record - 1] = '\0';
    if (ctx->remote.hydra[0] != '/' || !agent_remote_capability(&ctx->remote, "agent-locate-record", ctx->seconds))
        return setup_error(ctx, "capability_unavailable", "the remote Hydra cannot record agent locations",
                           "provision Hydra 2.9 or later with hydra remote provision NAME, then rerun", NULL);
    reply = agent_remote_request(&ctx->remote, "agent-locate-record", args, ctx->seconds > 30 ? ctx->seconds : 30);
    if (!json_object_get_boolean(f_field(reply, "ok"))) {
        code = f_string(f_field(reply, "error"), "code"); message = f_string(f_field(reply, "error"), "message");
        result = setup_error(ctx, code ? code : "location_invalid", message ? message : "the remote refused the location",
                             "choose an absolute path ending in /EXECUTABLE that you or root own and that is not group- or world-writable", NULL);
    }
    json_object_put(reply);
    return result;
}
json_object *setup_step_agents(struct setup_ctx *ctx, const char *record) {
    json_object *error = NULL, *inventory, *detail, *copy = NULL;
    if (record && (error = record_location(ctx, record))) return error;
    if (!(inventory = inventory_of(ctx, false, &error))) return error;
    if (!ctx->json) print_inventory(ctx, inventory);
    detail = agents_detail(ctx, inventory);
    if (json_object_deep_copy(detail, &copy, NULL)) copy = NULL;
    if (!copy || setup_state_step(ctx, "agents", "done", detail)) {
        json_object_put(copy); json_object_put(inventory);
        return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    }
    json_object_object_add(copy, "choices", choices(ctx, inventory));
    json_object_put(inventory);
    return f_success(ctx->command, copy);
}

/* ---- Installers ---- */
static json_object *no_recipe(const struct setup_ctx *ctx, const char *agent) {
    json_object *data = json_object_new_object();
    f_string_add(data, "agent", agent);
    return setup_error(ctx, "recipe_unavailable", "Hydra has no verified installer for this agent",
                       "install it on the remote by following its provider's documentation, then run hydra remote agents NAME "
                       "(add --record EXECUTABLE=/absolute/path when it lands off PATH)", data);
}
/* Missing required tools (caller-owned array), or NULL when SSH failed. */
static json_object *missing_tools(struct setup_ctx *ctx, json_object *recipe) {
    json_object *tools = f_field(recipe, "requires"), *missing; struct f_capture cap = {0};
    char script[1024] = "for tool in", command[2048], *quoted, *line, *save = NULL; size_t i;
    for (i = 0; i < json_object_array_length(tools); i++) {
        strncat(script, " ", sizeof(script) - strlen(script) - 1);
        strncat(script, f_text(json_object_array_get_idx(tools, i)), sizeof(script) - strlen(script) - 1);
    }
    strncat(script, "; do command -v \"$tool\" >/dev/null 2>&1 || printf '%s\\n' \"$tool\"; done", sizeof(script) - strlen(script) - 1);
    quoted = f_quote(script);
    if (!quoted || snprintf(command, sizeof(command), "exec /bin/sh -c %s", quoted) >= (int)sizeof(command) ||
        f_ssh(&ctx->remote, command, NULL, 0, ctx->seconds, false, &cap) || cap.status) {
        free(quoted); f_capture_free(&cap); return NULL;
    }
    free(quoted); missing = json_object_new_array();
    for (line = strtok_r(cap.out, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
        if (listed(tools, line)) json_object_array_add(missing, json_object_new_string(line));
    f_capture_free(&cap);
    return missing;
}
static json_object *install_plan(json_object *recipe) {
    static const char *const fields[] = {"agent", "executable", "command", "source", "docs_url", "verified_on", "requires", "expected_dirs", NULL};
    json_object *plan = json_object_new_object(); size_t i;
    for (i = 0; fields[i]; i++) json_object_object_add(plan, fields[i], json_object_get(f_field(recipe, fields[i])));
    f_string_add(plan, "effect", INSTALL_EFFECT);
    return plan;
}
static bool probe_ok(struct setup_ctx *ctx, const char *agent, const char *path, json_object *row) {
    const char *args[] = {"probe", agent, path, NULL}; bool ok;
    json_object *reply = agent_remote_request(&ctx->remote, "agent-inventory", args, 60), *evidence = f_field(f_field(reply, "data"), "evidence");
    const char *code = f_string(f_field(reply, "error"), "code");
    if (json_object_get_boolean(f_field(reply, "ok")))
        ok = f_string(evidence, "executable_version") && json_object_get_boolean(f_field(f_field(evidence, "probed"), "invocation_help"));
    else {
        /* Launch-only agents have no headless contract: require a version only. */
        json_object *candidates = f_field(row, "candidates"); size_t i;
        ok = false;
        for (i = 0; code && !strcmp(code, "invalid_profile") && array(candidates) && i < json_object_array_length(candidates); i++) {
            json_object *candidate = json_object_array_get_idx(candidates, i);
            if (!strcmp(f_string(candidate, "path") ? f_string(candidate, "path") : "", path) && f_string(candidate, "version")) ok = true;
        }
    }
    json_object_put(reply);
    return ok;
}
static bool offer_record(struct setup_ctx *ctx, const char *executable, const char *path) {
    char question[F_PATH + 160], record[F_PATH + 80]; json_object *error;
    if (!ctx->interactive) return false;
    snprintf(question, sizeof(question), "%s is at %s, outside the remote's non-interactive PATH. Record this location so Hydra uses it?", executable, path);
    if (!setup_agent_confirm(question)) return false;
    snprintf(record, sizeof(record), "%s=%s", executable, path);
    error = record_location(ctx, record);
    if (!error) return true;
    fputs("  not recorded: ", stderr); print_safe(f_string(f_field(error, "error"), "message")); fputc('\n', stderr);
    json_object_put(error);
    return false;
}
static json_object *record_next(const struct setup_ctx *ctx, const char *executable, const char *path) {
    char record[F_PATH + 80]; const char *const words[] = {"agents", ctx->name, "--record", record, NULL};
    json_object *next = json_object_new_object();
    snprintf(record, sizeof(record), "%s=%s", executable, path);
    f_string_add(next, "step", "agents");
    json_object_object_add(next, "argv", argv_json(ctx, words));
    json_object_object_add(next, "approval_sha256", NULL);
    return next;
}
static json_object *installed(struct setup_ctx *ctx, const char *step, json_object *recipe, json_object *row, const char *path) {
    const char *executable = f_string(recipe, "executable"), *status = f_string(row, "status");
    bool off_path = status && !strcmp(status, "found_off_path"), recorded = off_path && offer_record(ctx, executable, path);
    json_object *detail = json_object_new_object(), *result; char summary[F_PATH + 64];
    snprintf(summary, sizeof(summary), "installed at %s%s", path, off_path && !recorded ? " (off PATH, not recorded)" : "");
    f_string_add(detail, "summary", summary); f_string_add(detail, "agent", f_string(recipe, "agent"));
    f_string_add(detail, "executable", executable); f_string_add(detail, "path", path);
    json_object_object_add(detail, "installed_by_hydra", json_object_new_boolean(true));
    json_object_object_add(detail, "recorded", json_object_new_boolean(recorded || (status && !strcmp(status, "recorded"))));
    result = finished(ctx, step, "done", detail);
    if (off_path && !recorded && json_object_get_boolean(f_field(result, "ok")))
        json_object_object_add(f_field(result, "data"), "next", record_next(ctx, executable, path));
    return result;
}
/* After the installer or when reconciling an interrupted one: never re-run it. */
static json_object *install_verify(struct setup_ctx *ctx, const char *step, json_object *recipe, bool reconciling) {
    json_object *error = NULL, *inventory = inventory_of(ctx, true, &error), *row, *result;
    const char *path, *status;
    if (!inventory) return error;
    row = agent_inventory_row(inventory, f_string(recipe, "executable"));
    path = agent_inventory_location(row); status = f_string(row, "status");
    if (!path && (!status || !strcmp(status, "missing")))
        result = stopped(ctx, step, "failed", "installer left no executable", "install_failed",
                         reconciling ? "the interrupted installer did not leave the agent's executable" : "the installer finished but the agent's executable was not found",
                         "inspect the installer output, then rerun hydra remote install-agent NAME --agent AGENT", NULL);
    else if (!path)
        result = stopped(ctx, step, "blocked", "several or unsafe candidates", "agent_not_found", "the agent has several candidates or none Hydra may use",
                         "record one with hydra remote agents NAME --record EXECUTABLE=/absolute/path", NULL);
    else if (!probe_ok(ctx, f_string(recipe, "agent"), path, row))
        result = stopped(ctx, step, "failed", "version/help probe failed", "install_failed", "the installed executable did not pass the --version and help probe",
                         "run it on the remote to inspect the problem; if it is group-writable, chmod go-w it; then rerun", NULL);
    else result = installed(ctx, step, recipe, row, path);
    json_object_put(inventory);
    return result;
}
static json_object *installer_exit(struct setup_ctx *ctx, const char *step, int exit_status) {
    json_object *data = json_object_new_object();
    json_object_object_add(data, "exit_status", json_object_new_int(exit_status));
    if (exit_status == 255)
        return stopped(ctx, step, "outcome_unknown", "SSH ended during the installer", "outcome_unknown", "the SSH session ended; the installer may have run",
                       "rerun hydra remote install-agent NAME --agent AGENT to reconcile; it re-inventories and never re-runs the installer", data);
    if (exit_status > 128)
        return stopped(ctx, step, "outcome_unknown", "installer interrupted", "cancelled", "the installer was interrupted",
                       "rerun hydra remote install-agent NAME --agent AGENT to reconcile", data);
    return stopped(ctx, step, "failed", "installer exited with an error", "install_failed", "the installer exited with a nonzero status",
                   "inspect its output; rerun hydra remote install-agent NAME --agent AGENT to try again after review", data);
}
static json_object *progress_detail(json_object *recipe, const char *digest) {
    json_object *detail = json_object_new_object();
    f_string_add(detail, "summary", "installer started");
    f_string_add(detail, "command", f_string(recipe, "command"));
    f_string_add(detail, "plan_sha256", digest);
    return detail;
}
static json_object *prerequisites(struct setup_ctx *ctx, const char *step, json_object *recipe) {
    json_object *missing = missing_tools(ctx, recipe), *data;
    if (!missing) return setup_error(ctx, "offline", "cannot check the installer prerequisites on the remote", "check SSH access, then rerun", NULL);
    if (!json_object_array_length(missing)) { json_object_put(missing); return NULL; }
    data = json_object_new_object(); json_object_object_add(data, "missing", missing);
    return stopped(ctx, step, "blocked", "installer prerequisites missing", "prerequisite_missing", "the remote lacks tools the installer needs",
                   "install data.missing on the remote (for example sudo apt-get install curl), then rerun", data);
}
static json_object *install_run(struct setup_ctx *ctx, const char *step, json_object *recipe, const char *approve) {
    const char *const argv[] = {"install-agent", ctx->name, "--agent", f_string(recipe, "agent"), NULL};
    json_object *plan, *result = prerequisites(ctx, step, recipe);
    char digest[65] = "", *quoted, command[4200]; int exit_status = -1;
    if (result) return result;
    plan = install_plan(recipe);
    result = setup_plan_gate(ctx, step, plan, approve, argv);
    if (!result && setup_plan_hash(ctx, "install_agent", plan, NULL, digest)) digest[0] = '\0';
    json_object_put(plan);
    if (result) return result;
    quoted = f_quote(f_string(recipe, "command"));
    if (!quoted || snprintf(command, sizeof(command), "exec /bin/sh -c %s", quoted) >= (int)sizeof(command)) {
        free(quoted); return setup_error(ctx, "invalid_plan", "the installer command is too long", NULL, NULL);
    }
    free(quoted);
    if (setup_state_step(ctx, step, "in_progress", progress_detail(recipe, digest)))
        return setup_error(ctx, "state_unavailable", "cannot record setup progress; the installer was not started", NULL, NULL);
    if (f_ssh_interactive(&ctx->remote, command, ctx->seconds, &exit_status))
        return stopped(ctx, step, "failed", "ssh did not start", "install_failed", "cannot start SSH for the installer", "check ssh, then rerun", NULL);
    return exit_status ? installer_exit(ctx, step, exit_status) : install_verify(ctx, step, recipe, false);
}
static json_object *already_present(struct setup_ctx *ctx, const char *step, json_object *row) {
    json_object *detail = json_object_new_object(); const char *path = agent_inventory_location(row); char summary[F_PATH + 48];
    snprintf(summary, sizeof(summary), "already present (%s)%s%s", f_string(row, "status"), path ? " at " : "", path ? path : "");
    f_string_add(detail, "summary", summary); f_string_add(detail, "path", path);
    json_object_object_add(detail, "installed_by_hydra", json_object_new_boolean(false));
    return finished(ctx, step, "skipped", detail);
}
static json_object *install(struct setup_ctx *ctx, const char *step, json_object *recipe, const char *approve) {
    const char *status = setup_state_status(ctx, step), *row_status;
    json_object *inventory, *error = NULL, *row, *result;
    if (!strcmp(status, "in_progress") || !strcmp(status, "outcome_unknown")) return install_verify(ctx, step, recipe, true);
    if (!strcmp(status, "done") || !strcmp(status, "skipped")) {
        json_object *copy = NULL;
        if (json_object_deep_copy(setup_state_detail(ctx, step), &copy, NULL)) copy = NULL;
        return f_success(ctx->command, copy ? copy : json_object_new_object());
    }
    if (!(inventory = inventory_of(ctx, true, &error))) return error;
    row = agent_inventory_row(inventory, f_string(recipe, "executable"));
    row_status = f_string(row, "status");
    if (row_status && strcmp(row_status, "missing")) result = already_present(ctx, step, row);
    else result = install_run(ctx, step, recipe, approve);
    json_object_put(inventory);
    return result;
}
json_object *setup_step_install_agent(struct setup_ctx *ctx, const char *agent, const char *approve) {
    char step[80]; json_object *error = NULL, *recipe, *result;
    snprintf(step, sizeof(step), "install_agent:%s", agent);
    recipe = setup_recipe(ctx, agent, &error);
    if (!recipe) return error ? error : no_recipe(ctx, agent);
    if (setup_agent_select(ctx, agent)) { json_object_put(recipe); return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL); }
    result = install(ctx, step, recipe, approve);
    json_object_put(recipe);
    return result;
}
