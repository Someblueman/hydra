#define _POSIX_C_SOURCE 200809L
#include "setup_model.h"
#include "setup_json.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETUP_TOKENS 8192

static void copy_field(char *out, size_t size, const char *text) {
    size_t n = strlen(text);
    if (!size) return;
    if (n >= size) n = size - 1;
    memcpy(out, text, n); out[n] = '\0';
}

static void read_text(const struct sj_doc *d, int token, const char *path, char *out, size_t size) {
    (void)sj_path_text(d, token, path, out, size);
}

/* ---- Envelope members ---- */

static void parse_steps(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int steps = sj_find(d, data, "steps"), row, i;
    for (i = 0; (row = sj_item(d, steps, i)) >= 0 && e->step_count < SETUP_STEP_MAX; i++) {
        struct setup_step *s = &e->steps[e->step_count];
        read_text(d, row, "id", s->id, sizeof(s->id));
        if (!s->id[0]) continue;
        read_text(d, row, "status", s->status, sizeof(s->status));
        read_text(d, row, "detail", s->detail, sizeof(s->detail));
        read_text(d, row, "error.code", s->error_code, sizeof(s->error_code));
        read_text(d, row, "error.message", s->error_message, sizeof(s->error_message));
        if (!s->status[0]) copy_field(s->status, sizeof(s->status), "pending");
        e->step_count++;
    }
}

static void parse_next(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int next = sj_find(d, data, "next"), argv = sj_find(d, next, "argv"), word, i;
    read_text(d, next, "step", e->next_step, sizeof(e->next_step));
    read_text(d, next, "approval_sha256", e->approval, sizeof(e->approval));
    for (i = 0; (word = sj_item(d, argv, i)) >= 0; i++) {
        /* An argv that does not fit exactly is not offered at all. */
        if (e->argc >= SETUP_ARG_MAX || !sj_text(d, word, e->argv[e->argc], SETUP_ARG_TEXT)) { e->argc = 0; return; }
        e->argc++;
    }
}

static bool hidden_plan_key(const char *key) {
    return !strcmp(key, "schema") || !strcmp(key, "schema_version") || !strcmp(key, "kind");
}

static void join_items(const struct sj_doc *d, int array, char *out, size_t size) {
    char item[256];
    int token, i;
    out[0] = '\0';
    for (i = 0; (token = sj_item(d, array, i)) >= 0; i++) {
        size_t used = strlen(out);
        if (!sj_text(d, token, item, sizeof(item))) { snprintf(out, size, "%d entries", d->tokens[array].size); return; }
        snprintf(out + used, size - used, "%s%s", i ? ", " : "", item);
    }
}

/* One displayed plan row: arrays are joined, null reads "none". */
static void plan_value(struct setup_row *row, const struct sj_doc *d, int value, const char *path) {
    copy_field(row->key, sizeof(row->key), path);
    if (sj_type_of(d, value) == SJ_ARRAY) join_items(d, value, row->value, sizeof(row->value));
    else if (!sj_text(d, value, row->value, sizeof(row->value)) && sj_type_of(d, value) == SJ_NULL)
        copy_field(row->value, sizeof(row->value), "none");
}

static void flatten(struct setup_envelope *e, const struct sj_doc *d, int object, const char *prefix, int depth) {
    int key, value, i;
    for (i = 0; (value = sj_member(d, object, i, &key)) >= 0 && e->plan_count < SETUP_ROW_MAX; i++) {
        char name[64], path[136];
        if (!sj_text(d, key, name, sizeof(name)) || (!depth && hidden_plan_key(name))) continue;
        snprintf(path, sizeof(path), "%.63s%s%s", prefix, prefix[0] ? "." : "", name);
        if (sj_type_of(d, value) == SJ_OBJECT && depth < 3) flatten(e, d, value, path, depth + 1);
        else plan_value(&e->plan[e->plan_count++], d, value, path);
    }
}

static void parse_plan(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int plan = sj_find(d, data, "plan");
    read_text(d, data, "plan_sha256", e->plan_sha256, sizeof(e->plan_sha256));
    if (sj_type_of(d, plan) != SJ_OBJECT) return;
    read_text(d, plan, "kind", e->plan_kind, sizeof(e->plan_kind));
    read_text(d, plan, "fingerprint", e->fingerprint, sizeof(e->fingerprint));
    flatten(e, d, plan, "", 0);
}

static bool first_text(const struct sj_doc *d, int token, const char *const *keys, char *out, size_t size) {
    for (; *keys; keys++) if (sj_path_text(d, token, *keys, out, size) && out[0]) return true;
    out[0] = '\0';
    return false;
}

static void parse_requirement(struct setup_requirement *r, const struct sj_doc *d, int item, bool missing) {
    static const char *const commands[] = {"suggestion", "command", "fix", NULL};
    int blocking = sj_find(d, item, "blocking");
    if (sj_type_of(d, item) == SJ_STRING) { (void)sj_text(d, item, r->name, sizeof(r->name)); }
    else {
        read_text(d, item, "name", r->name, sizeof(r->name));
        read_text(d, item, "status", r->status, sizeof(r->status));
        read_text(d, item, "detail", r->detail, sizeof(r->detail));
        (void)first_text(d, item, commands, r->command, sizeof(r->command));
    }
    if (!r->status[0]) copy_field(r->status, sizeof(r->status), missing ? "missing" : "ok");
    r->blocking = blocking >= 0 ? sj_true(d, blocking) : missing;
}

/* Preflight rows: data.requirements on success, data.preflight.requirements
 * with prerequisite_missing, or at least the names in data.missing[]. */
static void parse_requirements(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int list = sj_find(d, data, "requirements"), item, i;
    bool missing;
    if (list < 0) list = sj_path(d, data, "preflight.requirements");
    missing = list < 0;
    if (missing) list = sj_find(d, data, "missing");
    for (i = 0; (item = sj_item(d, list, i)) >= 0 && e->requirement_count < SETUP_REQUIREMENT_MAX; i++) {
        struct setup_requirement *r = &e->requirements[e->requirement_count];
        parse_requirement(r, d, item, missing);
        if (r->name[0]) e->requirement_count++;
    }
}

static void parse_agent(struct setup_agent *a, const struct sj_doc *d, int item) {
    static const char *const names[] = {"executable", "profile", "agent", "name", NULL};
    static const char *const paths[] = {"recorded", "on_path", "path", NULL};
    int candidates = sj_find(d, item, "candidates");
    (void)first_text(d, item, names, a->name, sizeof(a->name));
    read_text(d, item, "status", a->status, sizeof(a->status));
    if (!first_text(d, item, paths, a->path, sizeof(a->path)))
        read_text(d, sj_item(d, candidates, 0), "path", a->path, sizeof(a->path));
    read_text(d, item, "version", a->version, sizeof(a->version));
    if (!a->version[0]) read_text(d, sj_item(d, candidates, 0), "version", a->version, sizeof(a->version));
}

static void parse_agents(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int list = sj_find(d, data, "agents"), item, i;
    for (i = 0; (item = sj_item(d, list, i)) >= 0 && e->agent_count < SETUP_AGENT_MAX; i++) {
        parse_agent(&e->agents[e->agent_count], d, item);
        if (e->agents[e->agent_count].name[0]) e->agent_count++;
    }
}

/* data.choices: the agents `remote agents` offers to install or sign in to. */
static void parse_choices(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int list = sj_find(d, data, "choices"), item, i;
    for (i = 0; (item = sj_item(d, list, i)) >= 0 && e->choice_count < SETUP_CHOICE_MAX; i++) {
        struct setup_choice *c = &e->choices[e->choice_count];
        read_text(d, item, "agent", c->agent, sizeof(c->agent));
        read_text(d, item, "status", c->status, sizeof(c->status));
        c->install = sj_type_of(d, sj_find(d, item, "install_argv")) == SJ_ARRAY;
        if (c->agent[0] && !setup_name_problem(c->agent)) e->choice_count++;
    }
}

/* data.setups of `remote setup list`. */
static void parse_listed(struct setup_envelope *e, const struct sj_doc *d, int data) {
    int list = sj_find(d, data, "setups"), item, i;
    for (i = 0; (item = sj_item(d, list, i)) >= 0 && e->listed_count < SETUP_LISTED_MAX; i++) {
        struct setup_listed *l = &e->listed[e->listed_count];
        memset(l, 0, sizeof(*l));
        read_text(d, item, "name", l->name, sizeof(l->name));
        read_text(d, item, "destination", l->destination, sizeof(l->destination));
        read_text(d, item, "status", l->status, sizeof(l->status));
        read_text(d, item, "next.step", l->next_step, sizeof(l->next_step));
        read_text(d, item, "error.code", l->error, sizeof(l->error));
        l->complete = sj_true(d, sj_find(d, item, "complete"));
        if (!setup_name_problem(l->name)) e->listed_count++;
    }
}

static void parse_key_change(struct setup_envelope *e, const struct sj_doc *d, int data) {
    static const char *const presented[] = {"presented_fingerprint", "fingerprint", NULL};
    static const char *const files[] = {"known_hosts", "file", NULL};
    static const char *const hosts[] = {"host", "hostname", NULL};
    (void)first_text(d, data, presented, e->presented, sizeof(e->presented));
    (void)first_text(d, data, files, e->key_file, sizeof(e->key_file));
    (void)first_text(d, data, hosts, e->key_host, sizeof(e->key_host));
}

static void parse_error(struct setup_envelope *e, const struct sj_doc *d) {
    read_text(d, 0, "error.code", e->code, sizeof(e->code));
    read_text(d, 0, "error.message", e->message, sizeof(e->message));
    read_text(d, 0, "error.recovery", e->recovery, sizeof(e->recovery));
    if (!e->ok && !e->code[0]) copy_field(e->code, sizeof(e->code), "unknown_error");
}

int setup_envelope_parse(struct setup_envelope *e, const char *text, size_t length, int exit_status) {
    struct sj_token *tokens = calloc(SETUP_TOKENS, sizeof(*tokens));
    struct sj_doc d;
    char version[8] = "";
    int data;
    memset(e, 0, sizeof(*e));
    e->exit_status = exit_status;
    if (!tokens) return -1;
    if (sj_parse(&d, text, length, tokens, SETUP_TOKENS) || sj_type_of(&d, 0) != SJ_OBJECT ||
        !sj_path_text(&d, 0, "schema_version", version, sizeof(version)) || strcmp(version, "1")) {
        free(tokens); return -1;
    }
    e->parsed = true;
    e->ok = sj_true(&d, sj_find(&d, 0, "ok"));
    read_text(&d, 0, "command", e->command, sizeof(e->command));
    parse_error(e, &d);
    data = sj_find(&d, 0, "data");
    read_text(&d, data, "name", e->name, sizeof(e->name));
    read_text(&d, data, "destination", e->destination, sizeof(e->destination));
    e->complete = sj_true(&d, sj_find(&d, data, "complete"));
    parse_steps(e, &d, data);
    parse_next(e, &d, data);
    parse_plan(e, &d, data);
    parse_requirements(e, &d, data);
    parse_agents(e, &d, data);
    parse_choices(e, &d, data);
    parse_listed(e, &d, data);
    if (!strcmp(e->code, "host_key_changed")) parse_key_change(e, &d, data);
    free(tokens);
    return 0;
}

/* ---- Screens ---- */

static enum setup_screen error_screen(const struct setup_envelope *e) {
    static const struct { const char *code; enum setup_screen screen; } map[] = {
        {"host_key_changed", SETUP_SCREEN_KEY_CHANGED}, {"outcome_unknown", SETUP_SCREEN_UNKNOWN},
        {"tty_required", SETUP_SCREEN_HANDOFF},
    };
    size_t i;
    if (!strcmp(e->code, "approval_required") && e->plan_count)
        return !strcmp(e->plan_kind, "host_key") ? SETUP_SCREEN_TRUST_KEY : SETUP_SCREEN_PLAN;
    if (!strcmp(e->code, "prerequisite_missing") && e->requirement_count) return SETUP_SCREEN_PREFLIGHT;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(e->code, map[i].code)) return map[i].screen;
    return SETUP_SCREEN_ERROR;
}

enum setup_screen setup_screen_for(const struct setup_envelope *e) {
    if (!e->parsed) return SETUP_SCREEN_ERROR;
    if (!e->ok) return error_screen(e);
    if (e->complete) return SETUP_SCREEN_DONE;
    if (!strcmp(e->command, "remote-preflight")) return SETUP_SCREEN_PREFLIGHT;
    if (!strcmp(e->command, "remote-agents")) return SETUP_SCREEN_AGENTS;
    return e->step_count && !e->next_step[0] ? SETUP_SCREEN_DONE : SETUP_SCREEN_STEPS;
}

/* ---- Plain language ---- */

static const struct { const char *code; struct setup_explanation text; } explanations[] = {
    {"host_key_unknown", {"SSH still does not accept the host", "Hydra added the key you approved, but a strict SSH connection still rejects the host. Check known_hosts and your SSH configuration (UserKnownHostsFile, HostKeyAlias), then check again.", "Enter checks again"}},
    {"ssh_config_invalid", {"SSH cannot read the configuration for this host", "ssh -G could not evaluate the destination and its known_hosts files. Check the destination and the SSH config file, then try again.", "Enter tries again"}},
    {"io_failed", {"Hydra could not use a private scratch directory", "A local temporary directory could not be created. Nothing was changed on the remote.", "Enter tries again"}},
    {"invalid_response", {"The remote answered with something Hydra cannot read", "The remote output was missing, too large or malformed, so Hydra did not rely on it.", "Enter tries again"}},
    {"host_key_changed", {"The host key changed: Hydra will not connect", "The machine answered with a different SSH key than the one recorded for it. That happens when a server is reinstalled, but it is also what an intercepted connection looks like. Hydra never accepts a changed key and wrote nothing.", "Enter checks again after you fix known_hosts"}},
    {"host_key_ambiguous", {"known_hosts already has a different entry", "Your known_hosts file has another key for this host (for example of a different type). Hydra does not choose between them and wrote nothing. Check the entry with ssh-keygen -F, remove it deliberately if it is wrong, then check again.", "Enter checks again"}},
    {"known_hosts_unwritable", {"Hydra cannot add the key to known_hosts", "The known_hosts file is not safely writable (wrong owner or permissions). Nothing was written. Fix the file's ownership or mode, then continue.", "Enter tries again"}},
    {"authentication_failed", {"SSH could not log in", "The machine answered, but it did not accept your SSH credentials. Check that ssh DESTINATION works in a terminal (keys, agent, user name), then continue.", "Enter tries again"}},
    {"offline", {"The machine cannot be reached", "SSH could not connect. Check the name, your network or VPN, and that the machine is running.", "Enter tries again"}},
    {"timeout", {"The machine did not answer in time", "SSH gave up waiting. The step may be retried safely; Hydra reconciles anything it had started.", "Enter tries again"}},
    {"prerequisite_missing", {"The machine is missing requirements", "Some tools Hydra needs are not installed. Hydra never uses sudo; install them yourself, then check again.", "Enter checks again"}},
    {"platform_unsupported", {"This machine's platform is not supported", "Hydra ships remote runtimes for Linux x86_64 and aarch64. Other platforms need a binary you build and supply with --binary.", NULL}},
    {"asset_unavailable", {"No release runtime matches this machine", "This Hydra has no pinned runtime for the machine's platform (typical for development builds). Build one with make build-fleet-static and supply it with hydra remote setup NAME --binary FILE.", NULL}},
    {"asset_download_failed", {"The runtime download failed", "Hydra could not download the pinned runtime. Nothing was installed. Check your network, then try again.", "Enter tries again"}},
    {"hash_mismatch", {"The downloaded runtime did not match its pinned digest", "Hydra refused the bytes and did not cache or install them. Try again later; if it persists, report it.", "Enter tries again"}},
    {"approval_required", {"Your approval is needed", "This step changes the remote. Review the plan before approving it.", "Enter reviews the plan"}},
    {"approval_mismatch", {"The plan changed after you reviewed it", "What Hydra would do is no longer the plan you approved, so nothing ran. Review the new plan.", "Enter reviews the new plan"}},
    {"approval_declined", {"Not approved", "The plan was not approved, so nothing was changed.", "Enter reviews the plan again"}},
    {"platform_mismatch", {"The runtime does not match the machine", "The remote reported a different operating system or CPU than the runtime was built for, so Hydra did not run it.", "Enter checks again"}},
    {"install_failed", {"Installation failed", "The installation did not complete. Hydra kept its record of the attempt; continuing checks what is on the remote before doing anything again.", "Enter tries again"}},
    {"outcome_unknown", {"Hydra cannot tell whether the last step finished", "The connection dropped or the command was interrupted while the remote was changing. Hydra will not repeat it blindly: continuing first checks the remote and reconciles the earlier attempt.", "Enter reconciles"}},
    {"setup_busy", {"Setup for this host is already running", "Another Hydra process holds this host's setup lock. Wait for it to finish, then continue.", "Enter checks again"}},
    {"setup_not_started", {"Setup has not started for this name", "There is no setup record for this host yet. Add the host to start.", NULL}},
    {"setup_binding_changed", {"This name is already used for another destination", "A setup record with this name points at a different SSH destination or config. Choose another name, or finish that setup.", NULL}},
    {"state_invalid", {"The setup record is unsafe or unreadable", "Hydra refuses to use a setup record with the wrong owner, mode or content. Inspect $HYDRA_HOME/fleet/setup before continuing.", NULL}},
    {"state_unavailable", {"Hydra cannot save setup progress", "The private setup directory cannot be created or written. Check $HYDRA_HOME.", "Enter tries again"}},
    {"alias_conflict", {"A different remote already uses this name", "Hydra will not overwrite an existing remote alias. Remove the old alias deliberately with hydra remote, or set up the host under another name.", NULL}},
    {"recipe_unavailable", {"Hydra has no installer for this agent", "Install the agent on the remote yourself; Hydra then finds it.", "Enter checks again"}},
    {"agent_not_found", {"The agent was not found after installation", "Hydra could not find the agent's executable on the remote. Check the installer output, then check again.", "Enter checks again"}},
    {"sign_in_failed", {"Sign-in did not finish", "The provider's sign-in ended with an error. Nothing was copied from this machine. You can sign in again.", "Enter signs in again"}},
    {"sign_in_unverified", {"Sign-in could not be confirmed", "The sign-in command finished, but Hydra could not verify that the agent is signed in. Sign in again and confirm when asked.", "Enter signs in again"}},
    {"tty_required", {"This step needs your terminal", "The provider asks questions or opens a browser link, so Hydra hands this terminal to it and returns when it finishes.", "Enter hands over the terminal"}},
    {"cancelled", {"The step was interrupted", "Setup stopped before this step finished. Continuing resumes from the recorded state.", "Enter continues"}},
    {"version_mismatch", {"The remote runs a different Hydra version", "Remote setup needs the same Hydra version on both machines. Continuing provisions a matching runtime alongside any other installs.", "Enter continues"}},
    {"not_implemented", {"This step is not available in this Hydra build", "The setup step is not implemented in this version yet. Nothing was changed.", NULL}},
    {"invalid_input", {"Hydra rejected the setup request", "A name, destination or option was not accepted.", NULL}},
    {"no_result", {"Hydra returned no readable result", "The setup command ended without a result Hydra could read, so its outcome is unknown. Checking status is read-only.", "Enter checks status"}},
    {"refused_command", {"Hydra refused an unexpected next command", "The setup result named a next command that is not a remote setup step for this host, so the control centre did not run it.", NULL}},
};

void setup_explain(const char *code, struct setup_explanation *out) {
    size_t i;
    for (i = 0; i < sizeof(explanations) / sizeof(explanations[0]); i++) {
        if (!strcmp(code, explanations[i].code)) { *out = explanations[i].text; return; }
    }
    out->title = "Setup stopped"; out->body = "The setup command reported a problem.";
    out->action = "Enter tries again";
}

/* ---- Labels ---- */

void setup_step_label(const char *id, char *out, size_t size) {
    static const char *const names[][2] = {
        {"host_key", "Trust the host key"}, {"preflight", "Check requirements"}, {"provision", "Install Hydra"},
        {"agents", "Find agents"}, {"verify", "Verify the connection"}, {"alias", "Add the host to Hydra"},
    };
    const char *agent = strchr(id, ':');
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(id, names[i][0])) { copy_field(out, size, names[i][1]); return; }
    if (agent && !strncmp(id, "install_agent:", 14)) snprintf(out, size, "Install %s", agent + 1);
    else if (agent && !strncmp(id, "sign_in:", 8)) snprintf(out, size, "Sign in to %s", agent + 1);
    else copy_field(out, size, id);
}

/* What a running child is doing, as a present-tense phrase ("" is the
 * read-only status check). */
void setup_running_label(const char *id, char *out, size_t size) {
    static const char *const names[][2] = {
        {"", "Checking setup status"}, {"setup", "Checking setup status"}, {"host_key", "Checking the host key"},
        {"preflight", "Checking requirements"}, {"provision", "Installing Hydra"}, {"agents", "Looking for agents"},
        {"verify", "Verifying the connection"}, {"alias", "Adding the host to Hydra"},
    };
    const char *agent = strchr(id, ':');
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(id, names[i][0])) { copy_field(out, size, names[i][1]); return; }
    if (agent && !strncmp(id, "install_agent:", 14)) snprintf(out, size, "Preparing to install %s", agent + 1);
    else if (agent && !strncmp(id, "sign_in:", 8)) snprintf(out, size, "Checking the %s sign-in", agent + 1);
    else setup_step_label(id, out, size);
}

static const char *const statuses[][3] = {
    /* status, label, mark (unicode) */
    {"done", "done", "\xe2\x9c\x93"}, {"skipped", "skipped", "-"}, {"failed", "failed", "\xe2\x9c\x97"},
    {"outcome_unknown", "outcome unknown", "?"}, {"approval_required", "needs your approval", "!"},
    {"blocked", "blocked", "!"}, {"in_progress", "in progress", "~"}, {"running", "running", ">"},
    {"pending", "not started", " "},
};

static int status_index(const char *status) {
    size_t i;
    for (i = 0; status && i < sizeof(statuses) / sizeof(statuses[0]); i++) if (!strcmp(status, statuses[i][0])) return (int)i;
    return -1;
}

const char *setup_status_label(const char *status) {
    int i = status_index(status);
    return i < 0 ? status : statuses[i][1];
}

const char *setup_status_mark(const char *status, bool ascii) {
    int i = status_index(status);
    if (i < 0) return " ";
    if (ascii && !strcmp(status, "done")) return "+";
    if (ascii && !strcmp(status, "failed")) return "x";
    return statuses[i][2];
}

bool setup_status_finished(const char *status) {
    return !strcmp(status, "done") || !strcmp(status, "skipped");
}

const struct setup_requirement *setup_heads_requirement(const struct setup_envelope *e) {
    size_t i;
    for (i = 0; i < e->requirement_count; i++) {
        const struct setup_requirement *r = &e->requirements[i];
        if (!strcmp(r->name, "tmux") && strcmp(r->status, "ok")) return r;
    }
    return NULL;
}

const struct setup_step *setup_open_step(const struct setup_envelope *e) {
    size_t i;
    for (i = 0; i < e->step_count; i++) if (!setup_status_finished(e->steps[i].status)) return &e->steps[i];
    return NULL;
}

void setup_summary(const struct setup_envelope *e, char *out, size_t size) {
    const struct setup_step *open;
    char label[96];
    if (!e->parsed) { copy_field(out, size, "status unavailable"); return; }
    if (!e->ok) { snprintf(out, size, "status unavailable: %s", e->code); return; }
    open = setup_open_step(e);
    if (!open) { copy_field(out, size, "set up"); return; }
    setup_step_label(open->id, label, sizeof(label));
    if (!strcmp(open->status, "pending")) snprintf(out, size, "next: %s", label);
    else snprintf(out, size, "%s: %s", label, setup_status_label(open->status));
}

void setup_listed_summary(const struct setup_listed *l, char *out, size_t size) {
    char label[96];
    if (l->error[0]) { snprintf(out, size, "status unavailable: %s", l->error); return; }
    if (l->complete) { copy_field(out, size, "set up"); return; }
    setup_step_label(l->next_step[0] ? l->next_step : "setup", label, sizeof(label));
    if (!strcmp(l->status, "pending")) snprintf(out, size, "next: %s", label);
    else snprintf(out, size, "%s: %s", label, setup_status_label(l->status));
}

/* ---- Next command validation ---- */

static bool setup_word(const char *word) {
    static const char *const words[] = {"setup", "trust-key", "preflight", "provision", "agents", "install-agent", "sign-in"};
    size_t i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) if (!strcmp(word, words[i])) return true;
    return false;
}

static bool alias_text(const char *value) {
    return value[0] && isalnum((unsigned char)value[0]) && strlen(value) < 128 &&
        strspn(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") == strlen(value);
}

/* Consumes the option at argv[*i]; false for anything the TUI would not run. */
static bool option_valid(const struct setup_envelope *e, size_t *i) {
    const char *flag = e->argv[*i], *value = *i + 1 < e->argc ? e->argv[*i + 1] : NULL;
    if (!strcmp(flag, "--json")) { (*i)++; return true; }
    if (!value) return false;
    *i += 2;
    if (!strcmp(flag, "--agent")) return alias_text(value) && strlen(value) < 64;
    if (!strcmp(flag, "--approve")) return e->plan_sha256[0] && !strcmp(value, e->plan_sha256) && strlen(value) == 64;
    if (!strcmp(flag, "--fingerprint")) return e->fingerprint[0] && !strcmp(value, e->fingerprint) && !strncmp(value, "SHA256:", 7);
    return false;
}

bool setup_argv_valid(const struct setup_envelope *e, const char *name) {
    size_t i = 4;
    if (e->argc < 4 || strcmp(e->argv[0], "hydra") || strcmp(e->argv[1], "remote") ||
        !setup_word(e->argv[2]) || strcmp(e->argv[3], name)) return false;
    while (i < e->argc) if (!option_valid(e, &i)) return false;
    return true;
}

bool setup_step_needs_terminal(const char *step) {
    return !strncmp(step, "install_agent:", 14) || !strncmp(step, "sign_in:", 8);
}

/* ---- Form ---- */

const char *setup_name_problem(const char *name) {
    if (!name[0]) return "Enter a name for this host";
    if (!alias_text(name)) return "Use letters, digits, '-', '_' and '.', starting with a letter or digit";
    if (!strcmp(name, "status") || !strcmp(name, "list")) return "\"status\" and \"list\" are reserved; choose another name";
    return NULL;
}

const char *setup_destination_problem(const char *destination) {
    if (!destination[0]) return "Enter an SSH destination, such as user@host or a Host from ~/.ssh/config";
    if (!isalnum((unsigned char)destination[0]) || strlen(destination) >= 256 ||
        strspn(destination, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-@:") != strlen(destination))
        return "Use [user@]host or a Host alias: letters, digits and . _ - @ : only";
    return NULL;
}

/* A leading "~" or "~/" becomes home (the CLI takes absolute paths only);
 * nothing else is expanded, "$VARS" and "~user" included. False when home
 * is unusable or the result does not fit. */
bool setup_config_expand(const char *path, const char *home, char *out, size_t size) {
    int n;
    if (strcmp(path, "~") && strncmp(path, "~/", 2)) n = snprintf(out, size, "%s", path);
    else if (!home || home[0] != '/') return false;
    else n = snprintf(out, size, "%s%s", home, path + 1);
    return n >= 0 && (size_t)n < size;
}

const char *setup_config_problem(const char *path) {
    if (!path[0]) return NULL;
    if (path[0] != '/') return "The SSH config file must be an absolute path or ~/... (or leave it empty)";
    if (strlen(path) >= 1024) return "The SSH config path is too long";
    return NULL;
}

void setup_default_name(const char *destination, char *out, size_t size) {
    const char *host = strrchr(destination, '@');
    size_t used = 0;
    host = host ? host + 1 : destination;
    for (; *host && *host != ':' && used + 1 < size && used < 63; host++) {
        if (isalnum((unsigned char)*host) || (used && strchr("_.-", *host))) out[used++] = *host;
    }
    if (size) out[used] = '\0';
}
