/* Remote setup in the native control centre (U10): the bounded JSON reader,
 * recorded `hydra remote setup --json` envelopes, the state-to-screen model,
 * next-command validation, and the rendered screens at 80x24 and 140x40.
 * Screen captures are written to the evidence directory given as argv[2]. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../../src/tui/internal.h"
#include "../../src/tui/setup_flow.h"
#include "../../src/tui/setup_json.h"

static int failures;
static const char *fixtures, *evidence;

static void check(bool ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

/* ---- JSON reader ---- */

static bool parses(const char *text) {
    struct sj_token tokens[64];
    struct sj_doc d;
    return !sj_parse(&d, text, strlen(text), tokens, 64);
}

static int parse_text(struct sj_doc *d, const char *text, struct sj_token *tokens, int capacity) {
    return sj_parse(d, text, strlen(text), tokens, capacity);
}

static void json_cases(void) {
    static const char *const rejected[] = {
        "", "{", "{\"a\":1,}", "[1,]", "{\"a\" 1}", "\"open", "\"tab\tin\"", "{} x", "01x", "tru", "{\"a\":\"\\q\"}",
        "\"\\u12G4\"", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
    };
    struct sj_token tokens[64];
    struct sj_doc d;
    char text[128];
    size_t i;
    bool all = true;
    for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) all = all && !parses(rejected[i]);
    check(all, "malformed, truncated, over-deep and trailing JSON is rejected");
    check(parses(" {\"a\":[1,-2.5e3,true,false,null,{}],\"b\":\"x\"} "), "a well-formed document parses");
    check(!parse_text(&d, "{\"s\":\"a\\/b \\u00e9 \\ud83d\\ude00 \\u001b[31m\"}", tokens, 64) &&
          sj_path_text(&d, 0, "s", text, sizeof(text)) && !strcmp(text, "a/b \xc3\xa9 \xf0\x9f\x98\x80 ?[31m"),
          "escapes decode to UTF-8 and control characters become ?");
    check(!parse_text(&d, "{\"a\":{\"b\":[\"x\",\"y\"]},\"c\":7}", tokens, 64) &&
          sj_item(&d, sj_path(&d, 0, "a.b"), 1) >= 0 && sj_path_text(&d, 0, "c", text, sizeof(text)) && !strcmp(text, "7") &&
          sj_path(&d, 0, "a.missing") < 0, "dotted paths, items and absent members");
    check(!parse_text(&d, "\"0123456789\"", tokens, 64) && !sj_text(&d, 0, text, 5) && !strcmp(text, "0123"),
          "truncated text is reported, not silently accepted");
    check(parse_text(&d, "[1,2,3]", tokens, 2) < 0, "the token bound is enforced");
}

/* ---- Envelopes ---- */

static bool load(const char *name, struct setup_envelope *e, int exit_status) {
    char path[4096], *text = malloc(1024 * 1024);
    FILE *f;
    size_t n;
    bool ok;
    snprintf(path, sizeof(path), "%s/%s", fixtures, name);
    f = fopen(path, "rb");
    if (!f || !text) { if (f) fclose(f); free(text); return false; }
    n = fread(text, 1, 1024 * 1024 - 1, f);
    fclose(f);
    ok = !setup_envelope_parse(e, text, n, exit_status);
    free(text);
    return ok;
}

struct expectation {
    const char *file, *code;
    enum setup_screen screen;
    const char *next_step, *plan_kind;
    bool argv_valid;
    int exit_status;
};

static const struct expectation expectations[] = {
    {"fresh.json", "", SETUP_SCREEN_STEPS, "host_key", "", true, 0},
    {"host-key-approval.json", "approval_required", SETUP_SCREEN_TRUST_KEY, "host_key", "host_key", true, 3},
    {"host-key-trusted.json", "", SETUP_SCREEN_STEPS, "preflight", "", true, 0},
    {"host-key-changed.json", "host_key_changed", SETUP_SCREEN_KEY_CHANGED, "host_key", "", true, 1},
    {"host-key-ambiguous.json", "host_key_ambiguous", SETUP_SCREEN_ERROR, "host_key", "", true, 1},
    {"status-provision-pending.json", "", SETUP_SCREEN_STEPS, "provision", "", true, 0},
    {"preflight-blocked.json", "prerequisite_missing", SETUP_SCREEN_PREFLIGHT, "preflight", "", true, 1},
    {"preflight-ok.json", "", SETUP_SCREEN_PREFLIGHT, "provision", "", true, 0},
    {"preflight-tmux.json", "", SETUP_SCREEN_PREFLIGHT, "provision", "", true, 0},
    {"provision-approval.json", "approval_required", SETUP_SCREEN_PLAN, "provision", "provision", true, 3},
    {"provision-approval-unpinned.json", "approval_required", SETUP_SCREEN_PLAN, "provision", "provision", true, 3},
    {"provision-done.json", "", SETUP_SCREEN_STEPS, "agents", "", true, 0},
    {"outcome-unknown.json", "outcome_unknown", SETUP_SCREEN_UNKNOWN, "provision", "", true, 4},
    {"agents.json", "", SETUP_SCREEN_AGENTS, "verify", "", true, 0},
    {"install-approval.json", "approval_required", SETUP_SCREEN_PLAN, "install_agent:claude", "install_agent", true, 3},
    {"sign-in-tty.json", "tty_required", SETUP_SCREEN_HANDOFF, "sign_in:claude", "", true, 1},
    {"sign-in-failed.json", "sign_in_failed", SETUP_SCREEN_ERROR, "sign_in:claude", "", true, 1},
    {"status-sign-failed.json", "", SETUP_SCREEN_STEPS, "sign_in:claude", "", true, 0},
    {"status-installed.json", "", SETUP_SCREEN_STEPS, "sign_in:claude", "", true, 0},
    {"done.json", "", SETUP_SCREEN_DONE, "", "", false, 0},
    {"not-started.json", "setup_not_started", SETUP_SCREEN_ERROR, "", "", false, 1},
    {"remove-approval.json", "approval_required", SETUP_SCREEN_PLAN, "remove", "remove", true, 3},
};

static bool expected(const struct expectation *x, const struct setup_envelope *e) {
    return e->parsed && !strcmp(e->code, x->code) && setup_screen_for(e) == x->screen &&
        !strcmp(e->next_step, x->next_step) && !strcmp(e->plan_kind, x->plan_kind) &&
        setup_argv_valid(e, "ovh") == x->argv_valid && (!e->ok || !strcmp(e->name, "ovh"));
}

static void envelope_cases(struct setup_envelope *e) {
    size_t i;
    char name[160];
    for (i = 0; i < sizeof(expectations) / sizeof(expectations[0]); i++) {
        snprintf(name, sizeof(name), "%s: code, screen, next step and command", expectations[i].file);
        check(load(expectations[i].file, e, expectations[i].exit_status) && expected(&expectations[i], e), name);
    }
    check(load("host-key-approval.json", e, 3) && !strcmp(e->fingerprint, "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8") &&
          e->argc == 7 && !strcmp(e->argv[5], e->fingerprint) && !strcmp(e->argv[6], "--json"), "host key plan exposes the fingerprint it binds");
    check(load("preflight-blocked.json", e, 1) && e->requirement_count == 11 && e->requirements[1].blocking &&
          !strcmp(e->requirements[1].status, "missing") && strstr(e->requirements[1].detail, "sudo apt-get install git") &&
          !strcmp(e->requirements[1].command, "sudo apt-get install git") && !e->requirements[0].command[0] &&
          !e->requirements[7].blocking && !strcmp(e->requirements[7].status, "warning"),
          "recorded preflight rows keep status, blocking and the suggestion field");
    check(load("preflight-tmux.json", e, 0) && setup_heads_requirement(e) &&
          !strcmp(setup_heads_requirement(e)->command, "sudo apt-get install tmux") &&
          load("preflight-ok.json", e, 0) && !setup_heads_requirement(e),
          "only a tmux requirement that is not ok means heads cannot run on the host");
    check(load("agents.json", e, 0) && e->agent_count == 9 && !strcmp(e->agents[2].name, "cursor-agent") &&
          !strcmp(e->agents[1].path, "/usr/local/bin/codex") && !strcmp(e->agents[2].path, "/home/deploy/.local/bin/cursor-agent") &&
          !strcmp(e->agents[2].status, "recorded") && !strcmp(e->agents[1].version, "codex-cli 0.46.0"),
          "agent inventory rows keep executable, status, location and version");
    check(load("agents.json", e, 0) && e->choice_count == 6 && !strcmp(e->choices[0].agent, "claude") && e->choices[0].install &&
          !strcmp(e->choices[1].agent, "codex") && !e->choices[1].install, "installer choices say whether to install or sign in");
    check(load("status-sign-failed.json", e, 0) && e->step_count == 8 && !strcmp(e->steps[5].id, "sign_in:claude") &&
          !strcmp(e->steps[5].error_code, "sign_in_failed") && strstr(e->steps[5].error_message, "nonzero status") &&
          !e->steps[4].error_code[0], "a failed terminal step keeps the error the CLI recorded");
    check(load("host-key-changed.json", e, 1) && !strcmp(e->presented, "SHA256:Q2hhbmdlZEtleUZvclRlc3RpbmdPbmx5MDEyMzQ1Njc4OQ") &&
          !strcmp(e->key_file, "/Users/you/.ssh/known_hosts"), "a changed key reports the presented key and its file");
    check(setup_envelope_parse(e, "{\"ok\":true}", 11, 0) < 0 && !e->parsed, "an envelope without schema_version 1 is not trusted");
}

/* `remote setup list` rows as the Hosts tab summarises them. */
static void list_cases(struct setup_envelope *e) {
    char summary[160] = "";
    bool listed = load("list-progress.json", e, 0) && e->listed_count == 1 && !strcmp(e->listed[0].name, "ovh") &&
        !strcmp(e->listed[0].status, "outcome_unknown") && !e->listed[0].complete;
    if (listed) setup_listed_summary(&e->listed[0], summary, sizeof(summary));
    check(listed && !strcmp(summary, "Install Hydra: outcome unknown"), "setup list rows summarise the next step");
    listed = load("list-done.json", e, 0) && e->listed_count == 1 && e->listed[0].complete;
    if (listed) setup_listed_summary(&e->listed[0], summary, sizeof(summary));
    check(listed && !strcmp(summary, "set up"), "a finished setup lists as set up");
    check(load("list-empty.json", e, 0) && e->ok && !e->listed_count, "no setups lists nothing");
    check(load("list-early.json", e, 0) && e->listed_count == 1 && !e->listed[0].remote_changed && !e->listed[0].ssh_config[0] &&
          !strcmp(e->listed[0].destination, "deploy@ovh.example.net"), "a setup that changed nothing on the remote can be edited");
    check(load("list-progress.json", e, 0) && e->listed[0].remote_changed && !strcmp(e->listed[0].ssh_config, "/Users/you/.ssh/config"),
          "list rows carry the SSH config and whether the remote changed");
}

/* Every tampered next command is refused; only the CLI's own argv runs. */
static void argv_cases(struct setup_envelope *e) {
    static const struct { int index; const char *value; const char *name; } tampered[] = {
        {0, "sh", "argv[0] must be hydra"}, {1, "fleet", "only remote setup commands"}, {2, "remove", "only setup steps"},
        {3, "other", "only this host"}, {5, "0000000000000000000000000000000000000000000000000000000000000000", "only the reviewed plan hash"},
        {6, "--binary", "no options the reviewer did not see"},
    };
    size_t i;
    for (i = 0; i < sizeof(tampered) / sizeof(tampered[0]); i++) {
        bool ok = load("provision-approval.json", e, 3) && setup_argv_valid(e, "ovh");
        copy_text(e->argv[tampered[i].index], SETUP_ARG_TEXT, tampered[i].value);
        check(ok && !setup_argv_valid(e, "ovh"), tampered[i].name);
    }
    check(load("host-key-approval.json", e, 3) && (copy_text(e->fingerprint, sizeof(e->fingerprint), "SHA256:other"), !setup_argv_valid(e, "ovh")),
          "only the fingerprint shown to the user");
    check(load("remove-approval.json", e, 3) && setup_argv_valid(e, "ovh") && !setup_argv_valid(e, "other"),
          "the removal command runs only for its own host");
    check(load("remove-approval.json", e, 3) && (copy_text(e->plan_kind, sizeof(e->plan_kind), "provision"), !setup_argv_valid(e, "ovh")),
          "setup remove runs only for a shown removal plan");
    check(load("provision-approval.json", e, 3) && (copy_text(e->argv[2], SETUP_ARG_TEXT, "setup"), copy_text(e->argv[3], SETUP_ARG_TEXT, "remove"),
          !setup_argv_valid(e, "ovh")), "a step's approval never becomes a removal");
    check(setup_step_needs_terminal("install_agent:claude") && setup_step_needs_terminal("sign_in:codex") &&
          !setup_step_needs_terminal("provision"), "installers and sign-in hand over the terminal");
}

static void language_cases(void) {
    static const char *const codes[] = {
        "host_key_unknown", "host_key_changed", "host_key_ambiguous", "known_hosts_unwritable", "authentication_failed", "offline",
        "timeout", "prerequisite_missing", "platform_unsupported", "asset_unavailable", "asset_download_failed", "hash_mismatch",
        "approval_required", "approval_mismatch", "platform_mismatch", "install_failed", "outcome_unknown", "setup_busy",
        "state_invalid", "alias_conflict", "recipe_unavailable", "agent_not_found", "sign_in_failed", "sign_in_unverified",
        "tty_required", "setup_not_started", "approval_declined", "cancelled", "version_mismatch", "ssh_config_invalid",
        "io_failed", "invalid_response",
    };
    struct setup_explanation x;
    size_t i;
    bool all = true;
    char name[64];
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        setup_explain(codes[i], &x);
        all = all && strcmp(x.title, "Setup stopped") && x.body[0];
    }
    check(all, "every documented setup error has a plain-language account");
    setup_explain("host_key_changed", &x);
    check(!strstr(x.body, "accept the") && strstr(x.body, "never accepts"), "a changed key is explained as refused");
    check(!setup_name_problem("ovh-2") && setup_name_problem("status") && setup_name_problem("-x") && setup_name_problem(""),
          "name validation mirrors the CLI");
    check(!setup_destination_problem("deploy@ovh.example.net") && setup_destination_problem("a b") &&
          setup_destination_problem("-oProxyCommand=x"), "destination validation refuses option-like input");
    check(!setup_config_problem("") && setup_config_problem("relative/config"), "the SSH config must be absolute");
    setup_default_name("deploy@build-1.example.net:22", name, sizeof(name));
    check(!strcmp(name, "build-1.example.net"), "the default name is the destination's host");
}

/* ---- Rendered screens ---- */

static bool render_capture(struct app *app, char *out, size_t size) {
    FILE *capture = tmpfile();
    int saved = dup(STDOUT_FILENO);
    size_t n;
    if (!capture || saved < 0) {
        if (capture) fclose(capture);
        if (saved >= 0) close(saved);
        return false;
    }
    fflush(stdout);
    dup2(fileno(capture), STDOUT_FILENO);
    render(app, 1, true);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO); close(saved);
    if (fseek(capture, 0, SEEK_SET)) { fclose(capture); return false; }
    n = fread(out, 1, size - 1, capture);
    out[n] = '\0';
    fclose(capture);
    return n > 0;
}

static void keep(const char *name, int cols, int rows, const char *text) {
    char path[4096];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s-%dx%d.txt", evidence, name, cols, rows);
    if ((f = fopen(path, "w"))) { fputs(text, f); fclose(f); }
}

struct screen_case { const char *file, *label; int exit_status; const char *must[4]; const char *never[3]; };

static const struct screen_case screens[] = {
    {"host-key-approval.json", "trust-key", 3, {"Trust the host key of ovh?", "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8", "Type yes to trust", "enter trust"}, {"y approve", NULL, NULL}},
    {"host-key-changed.json", "key-changed", 1, {"The host key changed", "ssh-keygen -R ovh.example.net -f /Users/you/.ssh/known_hosts", "enter check again", "SHA256:Q2hhbmdl"}, {"Type yes", "y approve", "trust this key"}},
    {"host-key-ambiguous.json", "key-ambiguous", 1, {"known_hosts already has a different entry", "ssh-keygen -F", "Enter checks again", NULL}, {"Type yes", "y approve", NULL}},
    {"preflight-blocked.json", "preflight-blocked", 1, {"Missing requirements on ovh", "BLOCKS", "fix: sudo apt-get install git", "never uses sudo"}, {NULL, NULL, NULL}},
    {"preflight-ok.json", "preflight-ok", 0, {"Requirements on ovh", "Nothing blocks setup", "group-writable umask", "Enter continues setup"}, {"BLOCKS", NULL, NULL}},
    {"preflight-tmux.json", "preflight-tmux", 0, {"Requirements on ovh", "Nothing blocks setup", "fix: sudo apt-get install tmux", "Enter continues setup"}, {"BLOCKS", "Heads cannot run", NULL}},
    {"provision-approval.json", "provision-pinned", 3, {"Install Hydra on ovh", "pinned: its digest is recorded", "Install location", "Hydra release download"}, {"Not a release build", "UNPINNED", NULL}},
    {"provision-approval-unpinned.json", "provision-unpinned", 3, {"Install Hydra on ovh", "Not a release build: this helper", "Binary SHA-256", "own hydra-fleet helper"}, {"UNPINNED", NULL, NULL}},
    {"install-approval.json", "install-approval", 3, {"Install claude on ovh", "umask 022; curl -fsSL https://claude.ai/install.sh", "succeeds;", "y approve"}, {NULL, NULL, NULL}},
    {"agents.json", "agents", 0, {"Agents on ovh", "\xe2\x96\x8c Continue setup", "Install claude", "Sign in to codex"}, {NULL, NULL, NULL}},
    {"agents.json", "agents-inventory", 0, {"cursor-agent", "recorded location", "not installed", "codex-cli 0.46.0"}, {NULL, NULL, NULL}},
    {"outcome-unknown.json", "outcome-unknown", 4, {"cannot tell whether", "installs twice", "Enter reconciles", "exit 4"}, {NULL, NULL, NULL}},
    {"sign-in-tty.json", "sign-in-handoff", 1, {"Sign in to claude on ovh", "hydra remote sign-in ovh --agent claude", "return here at once", "after a failure"}, {"--json", NULL, NULL}},
    {"sign-in-failed.json", "sign-in-failed", 1, {"Sign-in did not finish", "sign-in exited with a nonzero status", "Enter signs in again", NULL}, {NULL, NULL, NULL}},
    {"done.json", "done", 0, {"ovh is ready", "Sign in to claude", "Enter shows ovh in Work", "hydra fleet spawn ovh --project /path/on/host -- BRANCH --profile claude"}, {"hydra fleet tui", "(ovh)", NULL}},
    {"fresh.json", "steps", 0, {"Setting up ovh", "\xe2\x96\x8c Continue setup", "Trust the host key", "not started"}, {NULL, NULL, NULL}},
    {"remove-approval.json", "remove-plan", 3, {"Remove the setup record of ovh?", "Left on remote", "Only this machine's record of the setup is deleted", "y remove the record"}, {"Type yes", NULL, NULL}},
};

static bool contains_all(const char *text, const char *const *must, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) if (must[i] && !strstr(text, must[i])) { printf("  missing: %s\n", must[i]); return false; }
    return true;
}

static bool contains_none(const char *text, const char *const *never, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) if (never[i] && strstr(text, never[i])) { printf("  unexpected: %s\n", never[i]); return false; }
    return true;
}

/* Scroll through the whole body so text below the fold is checked too. */
static void render_all(struct app *app, char *all, size_t size) {
    char frame[65536];
    all[0] = '\0';
    app->setup->scroll = 0;
    do {
        app->setup->more = false;
        if (!render_capture(app, frame, sizeof(frame))) return;
        if (strlen(all) + strlen(frame) + 1 < size) strcat(all, frame);
        app->setup->scroll += 4;
    } while (app->setup->more && app->setup->scroll < 200);
    app->setup->scroll = 0;
}

/* Whether the first rows of a frame (the top bar) hold text. */
static bool top_bar_has(const char *frame, const char *text) {
    const char *at = strstr(frame, text), *end = frame;
    int rows = 0;
    while (rows < 2 && (end = strchr(end, '\n')) != NULL) { end++; rows++; }
    return at && (!end || at < end);
}

static void screen_case(struct app *app, const struct screen_case *c, int cols, int rows) {
    static char all[1 << 20];
    char name[160], first[65536];
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows;
    check(load(c->file, s->current, c->exit_status), c->file);
    copy_text(s->name, sizeof(s->name), "ovh");
    s->open = true; s->notice[0] = '\0'; s->paused = false; s->selected = 0;
    s->screen = setup_screen_for(s->current);
    hosts_focus_detail(app, true);
    if (s->screen == SETUP_SCREEN_HANDOFF) {
        size_t i;
        s->handoff_argc = 0;
        for (i = 0; i < s->current->argc; i++) if (strcmp(s->current->argv[i], "--json")) copy_text(s->handoff[s->handoff_argc++], SETUP_ARG_TEXT, s->current->argv[i]);
        copy_text(s->handoff_step, sizeof(s->handoff_step), s->current->next_step);
    }
    render_all(app, all, sizeof(all));
    (void)render_capture(app, first, sizeof(first));
    keep(c->label, cols, rows, first);
    snprintf(name, sizeof(name), "%s screen at %dx%d shows what it must and nothing it must not", c->label, cols, rows);
    /* Drawn inside Hosts: the view is Hosts and the top bar shows its tab;
     * the steps and their screen are in the detail, approvals in a modal. */
    check(contains_all(all, c->must, 4) && contains_none(all, c->never, 3) && app->view == 6 && top_bar_has(first, " Hosts "), name);
    /* A step's note is whole: the host-key step's known_hosts path is not
     * cut at the pane's edge. */
    snprintf(name, sizeof(name), "%s step notes at %dx%d are not cut", c->label, cols, rows);
    check(!strstr(all, "verified by ssh (") || strstr(all, "known_hosts)"), name);
}

static void hub_case(struct app *app, int cols, int rows) {
    char frame[65536], name[96];
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows; s->open = false; app->host_selected = 0;
    s->record_count = 1;
    copy_text(s->records[0].name, sizeof(s->records[0].name), "ovh");
    copy_text(s->records[0].summary, sizeof(s->records[0].summary), "Install Hydra: needs your approval");
    check(render_capture(app, frame, sizeof(frame)), "hosts tab renders");
    keep("hosts-local", cols, rows, frame);
    snprintf(name, sizeof(name), "local Hosts lists This machine, the unfinished setup and Add a host at %dx%d", cols, rows);
    check(strstr(frame, "\xe2\x96\x8c\xe2\x97\x86 This machine") && strstr(frame, "Setting up 1") && strstr(frame, "Install Hydra: needs") &&
          strstr(frame, "+ Add a host") && strstr(frame, "A add host"), name);
    s->record_count = 0;
}

/* The host-key modal explains itself in full without scrolling: the
 * fingerprint, what trusting writes, and the field that takes yes. */
static void trust_fit_case(struct app *app, int cols, int rows) {
    char frame[65536], name[128];
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows;
    check(load("host-key-approval.json", s->current, 3), "host-key fixture loads");
    s->open = true; s->notice[0] = '\0'; s->scroll = 0; s->typed[0] = '\0';
    s->screen = SETUP_SCREEN_TRUST_KEY; s->modal.screen = SETUP_SCREEN_NONE;
    check(render_capture(app, frame, sizeof(frame)), "host-key screen renders");
    snprintf(name, sizeof(name), "host-key modal at %dx%d fits its explanation above the field that takes yes", cols, rows);
    check(!s->more && !strstr(frame, "more below") && strstr(frame, "machine itself") && strstr(frame, "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8") &&
          strstr(frame, "ssh-keygen -lf") && strstr(frame, "/etc/ssh/ssh_host_ed25519_key.pub") &&
          strstr(frame, "appends exactly one line") && strstr(frame, "refuses to connect rather than") &&
          strstr(frame, "Type yes to trust this key") && strstr(frame, "Anything but yes leaves the key untrusted"), name);
}

/* A guided run paused because tmux is missing: the first frame explains why
 * and shows the tmux row with its suggested fix, above other warnings. */
static void paused_case(struct app *app, int cols, int rows) {
    static const char *const must[] = {"Heads cannot run on ovh yet", "cannot run heads there", "install tmux yourself",
                                       "fix: sudo apt-get install tmux", "Enter continues setup anyway"};
    char frame[65536], name[128];
    const char *tmux, *umask;
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows;
    check(load("preflight-tmux.json", s->current, 0), "tmux preflight fixture loads");
    s->open = true; s->notice[0] = '\0'; s->scroll = 0;
    s->screen = SETUP_SCREEN_PREFLIGHT; s->paused = true;
    hosts_focus_detail(app, true);
    check(render_capture(app, frame, sizeof(frame)), "paused requirements render");
    keep("preflight-paused", cols, rows, frame);
    tmux = strstr(frame, "warning  tmux"); umask = strstr(frame, "warning  umask");
    snprintf(name, sizeof(name), "paused requirements at %dx%d explain the pause and list tmux first", cols, rows);
    check(contains_all(frame, must, 5) && !strstr(frame, "BLOCKS") && tmux && umask && tmux < umask &&
          strstr(frame, "enter continue anyway"), name);
    s->paused = false;
}

/* The display column where the first occurrence of mark starts on the frame
 * row that holds text, or -1. */
static int column_on_row(const char *frame, const char *text, const char *mark) {
    const char *at = strstr(frame, text), *row, *found, *p;
    int column = 0;
    if (!at) return -1;
    for (row = at; row > frame && row[-1] != '\n'; row--) { }
    found = strstr(row, mark);
    if (!found || memchr(row, '\n', (size_t)(found - row))) return -1;
    for (p = row; p < found; p++) column += ((unsigned char)*p & 0xc0) != 0x80;
    return column;
}

/* Agent names of any length keep the state column apart and aligned: a
 * ten-character name does not run into "on PATH", and one too long for the
 * column is cut with an ellipsis. */
static void machine_names_case(struct app *app, int cols, int rows) {
    static const char *const names[] = {"claude", "demo-agent", "a-very-long-agent-profile-name1"};
    struct hosts_machine *m = &hosts_state(app)->machine;
    char frame[65536], name[160];
    int claude, demo, cut;
    size_t i;
    m->read = m->agents_read = true;
    for (i = 0; i < 3; i++) { copy_text(m->agents[i].name, sizeof(m->agents[i].name), names[i]); m->agents[i].available = i < 2; }
    m->agent_count = 3;
    /* Tall enough that the agents keep a row each (shorter panes list them by name). */
    app->cols = cols; app->rows = rows < 40 ? 40 : rows;
    check(render_capture(app, frame, sizeof(frame)), "This machine with long agent names renders");
    keep("hosts-machine-names", cols, rows, frame);
    claude = column_on_row(frame, " claude ", "\xe2\x97\x8f");
    demo = column_on_row(frame, "demo-agent", "\xe2\x97\x8f");
    cut = column_on_row(frame, "a-very-long", "\xe2\x97\x8b");
    snprintf(name, sizeof(name), "agent names of any length align their state column at %dx%d", cols, rows);
    check(claude > 0 && claude == demo && demo == cut && !strstr(frame, "demo-agent\xe2\x97\x8f") && !strstr(frame, "demo-agent \xe2\x97\x8f"), name);
    snprintf(name, sizeof(name), "an agent name too long for its column is cut with an ellipsis at %dx%d", cols, rows);
    check(strstr(frame, "\xe2\x80\xa6  \xe2\x97\x8b not found on PATH") && !strstr(frame, "profile-name1"), name);
    m->agent_count = 0; m->read = m->agents_read = false; app->rows = rows;
}

/* This machine with eight agent profiles at 80x24: the agents shrink to two
 * rows that still name each one, so the remote explanation and the next
 * action stay above the status line; at 140x40 they keep a row each. */
static void machine_case(struct app *app, int cols, int rows) {
    static const char *const names[] = {"claude", "codex", "cursor", "agy", "opencode", "copilot", "aider", "gemini"};
    struct hosts_machine *m = &hosts_state(app)->machine;
    char frame[65536], name[160];
    size_t i;
    app->cols = cols; app->rows = rows; app->setup->open = false; app->host_selected = 0;
    m->read = m->agents_read = true;
    copy_text(m->tmux, sizeof(m->tmux), "3.4"); copy_text(m->hydra, sizeof(m->hydra), "3.0.0");
    for (i = 0; i < 8; i++) { copy_text(m->agents[i].name, sizeof(m->agents[i].name), names[i]); m->agents[i].available = !i; }
    m->agent_count = 8;
    check(render_capture(app, frame, sizeof(frame)), "This machine renders");
    keep("hosts-machine", cols, rows, frame);
    snprintf(name, sizeof(name), "This machine at %dx%d names every agent and keeps the whole explanation in view", cols, rows);
    check(strstr(frame, "claude") && strstr(frame, "gemini") && strstr(frame, "codex") && strstr(frame, "not found on PATH") &&
          strstr(frame, "when a task") && strstr(frame, "Nothing changes on") && strstr(frame, "approve it.") &&
          strstr(frame, "Press Enter or A to add a host") && strstr(frame, "filters by location."), name);
    m->agent_count = 0; m->read = m->agents_read = false;
    machine_names_case(app, cols, rows);
}

static void screens_at(struct app *app, int cols, int rows) {
    size_t i;
    for (i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) screen_case(app, &screens[i], cols, rows);
    hub_case(app, cols, rows);
    machine_case(app, cols, rows);
    trust_fit_case(app, cols, rows);
    paused_case(app, cols, rows);
}

/* Keys as the loop delivers them: decoded from terminal bytes, then routed by
 * the control centre's input boundary to the open setup flow. */
static void setup_keys(struct app *app, const char *bytes) {
    struct kt_input decoder;
    struct kt_event event;
    kt_input_init(&decoder);
    for (; *bytes; bytes++)
        if (kt_input_feed(&decoder, (unsigned char)*bytes, &event)) input_event(app, &event);
    if (kt_input_flush(&decoder, &event)) input_event(app, &event);
}

/* Keys that must never run anything: declining, wrong confirmation text and
 * keys on a changed-key screen. */
static void key_cases(struct app *app) {
    struct native_setup *s = app->setup;
    app->cols = 100; app->rows = 30; s->open = true;
    hosts_focus_detail(app, true);
    load("provision-approval.json", s->current, 3); s->screen = SETUP_SCREEN_PLAN; s->modal.screen = SETUP_SCREEN_NONE;
    setup_keys(app, "\r");
    check(s->screen == SETUP_SCREEN_PLAN && !s->job.active && strstr(s->notice, "Press y"), "Enter does not approve a plan");
    setup_keys(app, "\t\r ");
    check(s->screen == SETUP_SCREEN_PLAN && !s->job.active, "Enter and space approve nothing whichever button has the focus");
    setup_keys(app, "n");
    check(s->screen == SETUP_SCREEN_STEPS && !s->job.active && strstr(s->notice, "nothing was changed"), "n declines and runs nothing");
    {
        char frame[65536];
        app->cols = 120; app->rows = 32;
        check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "Not approved: nothing was changed. Enter shows the plan again."),
              "the declined notice fits its row at 120x32");
        app->cols = 100; app->rows = 30;
    }
    load("host-key-approval.json", s->current, 3); s->screen = SETUP_SCREEN_TRUST_KEY; s->typed[0] = '\0'; s->modal.screen = SETUP_SCREEN_NONE;
    setup_keys(app, "y\r");
    check(s->screen == SETUP_SCREEN_TRUST_KEY && !s->job.active && strstr(s->notice, "Type yes"), "a partial yes does not trust the key");
    setup_keys(app, "\033");
    check(s->screen == SETUP_SCREEN_STEPS && !s->job.active, "Esc declines the host key");
    load("agents.json", s->current, 0); s->screen = SETUP_SCREEN_AGENTS; s->selected = 0;
    setup_keys(app, "j"); setup_keys(app, "j");
    check(s->screen == SETUP_SCREEN_AGENTS && s->selected == 2 && !s->job.active, "arrows choose an agent without running anything");
    load("host-key-changed.json", s->current, 1); s->screen = SETUP_SCREEN_KEY_CHANGED;
    setup_keys(app, "yes");
    check(s->screen == SETUP_SCREEN_KEY_CHANGED && !s->job.active, "no key accepts a changed host key");
}

/* The host-key field accepts only the word yes: other answers, its other
 * spellings and a yes with more after it run nothing; yes and Enter run the
 * CLI's own trust command. */
static void trust_cases(struct app *app) {
    static const char *const refused[] = {"\r", "ye\r", "Yes\r", "YES\r", "yes \r", "yes please\r", "y\r", "no\r"};
    struct native_setup *s = app->setup;
    struct timespec pause = {0, 20000000};
    const char *hydra = app->hydra;
    size_t i;
    int waited;
    bool none = true;
    app->cols = 80; app->rows = 24; s->open = true;
    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        load("host-key-approval.json", s->current, 3); s->screen = SETUP_SCREEN_TRUST_KEY; s->typed[0] = '\0';
        s->modal.screen = SETUP_SCREEN_NONE; s->notice[0] = '\0';
        setup_keys(app, refused[i]);
        none = none && s->screen == SETUP_SCREEN_TRUST_KEY && !s->job.active && strstr(s->notice, "Type yes (the whole word)") &&
            strstr(s->modal.fields[0].error, "Type yes (the whole word)");
    }
    check(none, "the host key is not trusted by Enter alone, a partial yes, another case, trailing text or no");
    /* The approved command is the CLI's own; here it names a program that
     * does not exist, so nothing runs past the attempt. */
    app->hydra = "/nonexistent/hydra";
    load("host-key-approval.json", s->current, 3); s->screen = SETUP_SCREEN_TRUST_KEY; s->typed[0] = '\0'; s->modal.screen = SETUP_SCREEN_NONE;
    setup_keys(app, "yes\r");
    check(s->job.active && s->job_kind == SETUP_JOB_APPROVED && !strcmp(s->job_step, "host_key"), "yes and Enter run the trust step the plan names");
    for (waited = 0; waited < 250 && s->job.active; waited++) { native_setup_tick(app); nanosleep(&pause, NULL); }
    check(!s->job.active, "the trust step's child is reaped");
    app->hydra = hydra;
    s->screen = SETUP_SCREEN_STEPS; s->notice[0] = '\0';
}

static void type_text(struct app *app, const char *text) {
    setup_keys(app, text);
}

/* A form name that already has an unfinished setup asks what to do; e edits
 * it only while nothing changed the remote; Hosts offers e and x on it. */
static void edit_cases(struct app *app) {
    struct native_setup *s = app->setup;
    struct setup_record *r = &s->records[0];
    char frame[65536];
    size_t record = 99;
    app->cols = 100; app->rows = 30;
    memset(r, 0, sizeof(*r));
    s->record_count = 1;
    copy_text(r->name, sizeof(r->name), "ovh"); copy_text(r->destination, sizeof(r->destination), "deploy@ovh.example.net");
    copy_text(r->config, sizeof(r->config), "/Users/you/.ssh/private"); copy_text(r->summary, sizeof(r->summary), "Check requirements: blocked");
    s->open = false; app->host_selected = 1;
    check(hosts_setup_selected(app, &record) && record == 0, "the Hosts row of an unfinished setup offers edit and remove");
    hosts_focus_detail(app, false);
    native_setup_open_form(app);
    type_text(app, "deploy@ovh.example.net\r");
    check(s->screen == SETUP_SCREEN_FORM && s->focus == SETUP_FIELD_NAME && !s->job.active,
          "Enter in the destination moves to the name and runs nothing");
    type_text(app, "ovh\r");
    check(s->screen == SETUP_SCREEN_FORM && s->focus == SETUP_FIELD_CONFIG && !s->job.active,
          "Enter in the name moves to the SSH config file and runs nothing");
    type_text(app, "\r");
    check(s->screen == SETUP_SCREEN_DUPLICATE && !s->job.active && !strcmp(s->duplicate, "ovh"), "a duplicate name is caught before anything runs");
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "ovh already has an unfinished setup") &&
          strstr(frame, "/Users/you/.ssh/private") && strstr(frame, "Enter continues that setup"), "the duplicate screen offers continue, edit and remove");
    keep("duplicate", 100, 30, frame);
    setup_keys(app, "\033");
    check(s->screen == SETUP_SCREEN_FORM && s->focus == SETUP_FIELD_NAME, "Esc returns to the form at the name");
    type_text(app, "\r\r");
    setup_keys(app, "e");
    check(s->screen == SETUP_SCREEN_FORM && !strcmp(s->editing, "ovh") && !strcmp(s->fields[SETUP_FIELD_CONFIG].text, "/Users/you/.ssh/private") &&
          !strcmp(s->fields[SETUP_FIELD_DESTINATION].text, "deploy@ovh.example.net") && !s->job.active, "e opens the setup prefilled");
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "Edit the setup of ovh") && strstr(frame, "removes the old setup") &&
          strstr(frame, "/Users/you/.ssh/private"),
          "the edit form says the old record is replaced");
    keep("edit-form", 100, 30, frame);
    r->remote_changed = true; app->notice[0] = '\0';
    native_setup_edit(app, 0);
    check(strstr(app->notice, "cannot be edited") && strstr(app->notice, "x removes"), "a setup that changed the remote is not edited");
    s->open = false; s->editing[0] = '\0'; s->record_count = 0;
}

/* An empty name takes the destination's host, as the field's hint says while
 * the destination is typed: setup starts under that name. */
static void default_name_cases(struct app *app) {
    struct native_setup *s = app->setup;
    struct timespec pause = {0, 20000000};
    const char *hydra = app->hydra;
    char frame[65536];
    int waited;
    app->cols = 100; app->rows = 30; s->record_count = 0; s->open = false;
    hosts_focus_detail(app, false);
    native_setup_open_form(app);
    type_text(app, "deploy@build-7.example.net\r\r");
    check(s->screen == SETUP_SCREEN_FORM && render_capture(app, frame, sizeof(frame)) && strstr(frame, "empty: build-7.example.net"),
          "the name field's hint names the default the destination gives");
    app->hydra = "/nonexistent/hydra";
    type_text(app, "\r");
    check(s->job.active && s->job_kind == SETUP_JOB_GUIDED && !strcmp(s->name, "build-7.example.net") &&
          !strcmp(s->fields[SETUP_FIELD_NAME].text, "build-7.example.net"),
          "an empty name starts setup under the destination's host name");
    for (waited = 0; waited < 250 && s->job.active; waited++) { native_setup_tick(app); nanosleep(&pause, NULL); }
    check(!s->job.active, "the default-name setup child is reaped");
    app->hydra = hydra;
    native_setup_close(app);
    s->screen = SETUP_SCREEN_STEPS; s->notice[0] = '\0';
}

/* A host that cannot be reached before anything changed on it: the screen
 * shows the ssh command to try and e reopens the form with its binding. */
static void fixable_cases(struct app *app) {
    struct native_setup *s = app->setup;
    struct setup_record *r = &s->records[0];
    char frame[65536];
    app->cols = 120; app->rows = 32;
    memset(r, 0, sizeof(*r));
    copy_text(r->name, sizeof(r->name), "ovh"); copy_text(r->destination, sizeof(r->destination), "deploy@ovh.example.net");
    copy_text(r->config, sizeof(r->config), "/Users/you/.ssh/work"); copy_text(r->summary, sizeof(r->summary), "next: Trust the host key");
    s->record_count = 1;
    check(load("offline.json", s->current, 1), "offline fixture loads");
    copy_text(s->name, sizeof(s->name), "ovh");
    s->open = true; s->screen = setup_screen_for(s->current); s->notice[0] = '\0'; s->scroll = 0;
    hosts_focus_detail(app, true);
    check(s->screen == SETUP_SCREEN_ERROR && render_capture(app, frame, sizeof(frame)) &&
          strstr(frame, "ssh -F /Users/you/.ssh/work deploy@ovh.example.net") && strstr(frame, "e changes the destination") &&
          strstr(frame, "e edit") && !strstr(frame, "hydra remote setup remove NAME"),
          "an unreachable host shows the ssh command to try and offers e to change the destination");
    keep("offline-fixable", 120, 32, frame);
    setup_keys(app, "e");
    check(s->screen == SETUP_SCREEN_FORM && !strcmp(s->editing, "ovh") &&
          !strcmp(s->fields[SETUP_FIELD_DESTINATION].text, "deploy@ovh.example.net") && !s->job.active,
          "e on the unreachable host opens its form with the destination and SSH config");
    native_setup_close(app);
    r->remote_changed = true;
    load("offline.json", s->current, 1);
    s->open = true; s->screen = SETUP_SCREEN_ERROR; s->editing[0] = '\0';
    hosts_focus_detail(app, true);
    check(render_capture(app, frame, sizeof(frame)) && !strstr(frame, "e edit") && strstr(frame, "Suggested:"),
          "a setup that changed the remote keeps the CLI's suggestion and offers no edit");
    native_setup_close(app);
    s->record_count = 0; s->open = false;
}

static size_t occurrences(const char *text, const char *needle) {
    size_t count = 0;
    for (; (text = strstr(text, needle)) != NULL; text += strlen(needle)) count++;
    return count;
}

/* While no agent on the host can run, the agents screen recommends one (the
 * agent this machine runs, else the first offered) and starts on it; once
 * one is usable it starts on Continue setup. The review rule is said once. */
static void agents_cases(struct app *app) {
    static const char *const local[] = {"codex"}, *const unrelated[] = {"aider"};
    struct native_setup *s = app->setup;
    char frame[65536];
    check(load("agents.json", s->current, 0) && setup_agent_usable(s->current) && setup_recommended_row(s->current, local, 1) == 0,
          "with a usable agent nothing is recommended and Continue setup stays first");
    check(load("agents-none.json", s->current, 0) && !setup_agent_usable(s->current) && setup_recommended_row(s->current, local, 1) == 2 &&
          setup_recommended_row(s->current, unrelated, 1) == 1 && setup_recommended_row(s->current, NULL, 0) == 1,
          "with no usable agent the recommended one is this machine's agent, else the first offered");
    app->cols = 120; app->rows = 32; app->view = 6;
    copy_text(s->name, sizeof(s->name), "ovh");
    s->open = true; s->screen = SETUP_SCREEN_AGENTS; s->modal.screen = SETUP_SCREEN_NONE; s->notice[0] = '\0';
    s->recommended = s->selected = 2;
    hosts_focus_detail(app, true);
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "\xe2\x96\x8c Install codex (recommended)") &&
          strstr(frame, "Continue setup without an agent") && strstr(frame, "No agent can run on ovh yet") &&
          occurrences(frame, "installer's plan") == 1 && !strstr(frame, "you review the installer first"),
          "the agents screen starts on the recommended agent and says once that installers are reviewed");
    check(!strcmp(native_setup_label(app, KEY_SETUP_CONTINUE), "run"), "Enter on the recommended agent runs its row, never an approval");
    keep("agents-none", 120, 32, frame);
    s->recommended = s->selected = 0;
    native_setup_close(app);
    s->open = false;
}

/* An observed host that answers with no work: its confirmation is an age,
 * not epoch seconds, and the detail names the command that starts a head. */
static void observed_cases(struct app *app) {
    struct host_observation *h = &app->model.hosts[0];
    char frame[65536];
    app->cols = 120; app->rows = 32; app->setup->open = false; app->view = 6;
    memset(h, 0, sizeof(*h));
    copy_text(h->name, sizeof(h->name), "ovh"); copy_text(h->state, sizeof(h->state), "responded");
    copy_text(h->connection, sizeof(h->connection), "reachable"); copy_text(h->freshness, sizeof(h->freshness), "fresh");
    snprintf(h->last_confirmed, sizeof(h->last_confirmed), "%lld", (long long)time(NULL) - 30);
    app->model.host_count = 1; app->host_selected = 1;
    hosts_focus_detail(app, false);
    check(render_capture(app, frame, sizeof(frame)) && (strstr(frame, "Last confirmed: 30s ago") || strstr(frame, "Last confirmed: 31s ago")) &&
          strstr(frame, "No work on this host yet") && strstr(frame, "hydra fleet spawn ovh --project /path/on/host -- BRANCH"),
          "an answering host without work shows its confirmation age and how to start a head there");
    keep("hosts-observed-empty", 120, 32, frame);
    h->heads = 1;
    check(render_capture(app, frame, sizeof(frame)) && !strstr(frame, "No work on this host yet"), "a host with heads does not repeat the start command");
    app->model.host_count = 0; app->host_selected = 0;
}

/* A ready host: Enter follows it into Work (Esc there comes back to Hosts)
 * and asks for a fresh observation so its row appears at once. */
static void done_cases(struct app *app) {
    struct native_setup *s = app->setup;
    char frame[65536];
    app->cols = 120; app->rows = 32; app->view = 6; app->location[0] = '\0';
    load("done.json", s->current, 0);
    copy_text(s->name, sizeof(s->name), "ovh");
    s->open = true; s->screen = SETUP_SCREEN_DONE; s->modal.screen = SETUP_SCREEN_NONE;
    hosts_focus_detail(app, true);
    app->remote_next = time(NULL) + 600;
    check(!strcmp(native_setup_label(app, KEY_SETUP_CONTINUE), "show in Work"), "Enter on a ready host is labelled show in Work");
    setup_keys(app, "\r");
    check(!s->open && app->view == 7 && !strcmp(app->location, "ovh") && app->remote_next == 0,
          "Enter on a ready host shows its work in Work and observes it afresh");
    check(hosts_jump_return(app) && app->view == 6 && !app->location[0], "Esc from the ready host's work returns to Hosts");
    /* Its record, listed before the host's first observation, says it is set up. */
    memset(&s->records[0], 0, sizeof(s->records[0]));
    copy_text(s->records[0].name, sizeof(s->records[0].name), "ovh");
    copy_text(s->records[0].summary, sizeof(s->records[0].summary), "set up");
    s->records[0].complete = s->records[0].remote_changed = true;
    s->record_count = 1; app->host_selected = 1; app->notice[0] = '\0';
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "This host is set up") && !strstr(frame, "not in the fleet yet") &&
          !strstr(frame, "e edits it") && !strstr(frame, "setup unfinished"), "a finished setup not observed yet is described as set up");
    s->record_count = 0; app->host_selected = 0;
}

/* A running step names what it does and for how long; c stops the child
 * and returns to the screen the recorded state gives. */
static void running_cases(struct app *app) {
    char *const sleeper[] = {"sleep", "30", NULL};
    char frame[65536], label[96];
    struct native_setup *s = app->setup;
    struct timespec pause = {0, 20000000};
    int waited;
    setup_running_label("host_key", label, sizeof(label));
    check(!strcmp(label, "Checking the host key"), "the host-key step runs as Checking the host key");
    setup_running_label("", label, sizeof(label));
    check(!strcmp(label, "Checking setup status"), "the status read runs as Checking setup status");
    setup_running_label("install_agent:claude", label, sizeof(label));
    check(!strcmp(label, "Preparing to install claude"), "an installer choice names its agent");
    app->cols = 80; app->rows = 24;
    check(load("fresh.json", s->current, 0), "fresh fixture loads");
    copy_text(s->name, sizeof(s->name), "ovh");
    check(setup_capture_start(&s->job, sleeper, 60000), "a setup child starts");
    s->job_kind = SETUP_JOB_INSPECT; s->job_started = time(NULL) - 12;
    copy_text(s->job_step, sizeof(s->job_step), "host_key");
    s->open = true; s->notice[0] = '\0'; s->scroll = 0; s->screen = SETUP_SCREEN_RUNNING;
    hosts_focus_detail(app, true);
    check(render_capture(app, frame, sizeof(frame)), "running screen renders");
    keep("running", 80, 24, frame);
    check(strstr(frame, "Checking the host key\xe2\x80\xa6 12s") && strstr(frame, "c cancels (a rerun continues safely); b leaves it running.") &&
          strstr(frame, "c cancel step") && strstr(frame, "b background"),
          "a running step shows its phrase, elapsed seconds and how to cancel or leave it running");
    setup_keys(app, "\033");
    check(!s->job.cancelled && s->job.active && !s->open && strstr(app->notice, "keeps running"),
          "Esc leaves a running step running and gives the list the focus");
    native_setup_resume(app, "ovh");
    check(s->open && s->screen == SETUP_SCREEN_RUNNING && s->job.active, "Enter on its row shows the running step again");
    setup_keys(app, "c");
    check(s->job.cancelled && s->job.active, "c cancels the running child");
    for (waited = 0; waited < 250 && s->job.active; waited++) { native_setup_tick(app); nanosleep(&pause, NULL); }
    check(!s->job.active && s->screen == SETUP_SCREEN_STEPS && strstr(s->notice, "Cancelled: Checking the host key"),
          "a cancelled read-only check returns to the recorded steps");
}

/* The footer shares this freshness label with every view: a steady "Live"
 * while refreshes succeed, a counting age only once it means something. */
static void freshness_cases(struct app *app) {
    char text[128];
    time_t now = time(NULL);
    bool fleet = app->fleet;
    app->fleet = false;
    check(kt_style_equal(snapshot_freshness(app, now - 1, false, false, text, sizeof(text)), KT_MUTED) && !strcmp(text, "Live"),
          "a current snapshot is Live, without a restarting age");
    check(kt_style_equal(snapshot_freshness(app, now - 4, false, false, text, sizeof(text)), KT_MUTED) && !strcmp(text, "Live"),
          "two refresh intervals are still Live");
    check(kt_style_equal(snapshot_freshness(app, now - 9, false, false, text, sizeof(text)), KT_BASE) && !strcmp(text, "Updated 9s ago"),
          "an overdue refresh shows the age counting up");
    check(kt_style_equal(snapshot_freshness(app, now - 12, true, false, text, sizeof(text)), KT_WARNING) &&
          !strcmp(text, "STALE: last good snapshot, updated 12s ago"), "a failed refresh shows STALE with the age");
    check(kt_style_equal(snapshot_freshness(app, 0, false, false, text, sizeof(text)), KT_MUTED) && !strcmp(text, "Waiting for the first snapshot"),
          "before the first snapshot nothing claims to be live");
    check(kt_style_equal(snapshot_freshness(app, now - 60, false, true, text, sizeof(text)), KT_MUTED) && !strcmp(text, "Live"),
          "headless fixtures render deterministically");
    app->fleet = true;
    check(kt_style_equal(snapshot_freshness(app, now - 6, false, false, text, sizeof(text)), KT_BASE) && !strcmp(text, "Updated 6s ago"),
          "the compatibility entry keeps the local snapshot's own cadence");
    app->fleet = fleet;
    /* The remote observation has its own age: one request may take the
     * whole fleet budget, so it is Live for longer and stale only after a
     * failed attempt (U11). */
    {
        struct kt_style tone = KT_BASE;
        check(!remote_footer_text(app, text, sizeof(text), &tone), "no remote observation yet says nothing");
        app->remote_known = true; app->model.host_count = 1; app->remote_at = now - 12;
        check(remote_footer_text(app, text, sizeof(text), &tone) && kt_style_equal(tone, KT_MUTED) && !strcmp(text, "Remote live"),
              "a remote observation within one fleet budget is live");
        app->remote_at = now - 30;
        check(remote_footer_text(app, text, sizeof(text), &tone) && kt_style_equal(tone, KT_BASE) && !strcmp(text, "Remote updated 30s ago"),
              "an overdue remote observation shows its age");
        app->remote_stale = true; copy_text(app->remote_error, sizeof(app->remote_error), "remote observation timed out");
        check(remote_footer_text(app, text, sizeof(text), &tone) && kt_style_equal(tone, KT_WARNING) &&
              !strcmp(text, "Remote: STALE, last good observation 30s ago; remote observation timed out"),
              "a failed remote observation is STALE with the last good age and the reason");
        app->remote_stale = false; app->remote_known = false; app->model.host_count = 0; app->remote_at = 0; app->remote_error[0] = '\0';
    }
}

/* The SSH config field takes ~/ paths: only a leading ~ is expanded. */
static void config_path_cases(void) {
    char out[1024];
    check(setup_config_expand("~/.ssh/work", "/Users/you", out, sizeof(out)) && !strcmp(out, "/Users/you/.ssh/work") &&
          !setup_config_problem(out), "~/ expands to HOME and passes validation");
    check(setup_config_expand("~", "/Users/you", out, sizeof(out)) && !strcmp(out, "/Users/you"), "a bare ~ is HOME");
    check(setup_config_expand("/etc/ssh/cfg", NULL, out, sizeof(out)) && !strcmp(out, "/etc/ssh/cfg"), "absolute paths are unchanged");
    check(setup_config_expand("$HOME/.ssh/cfg", "/Users/you", out, sizeof(out)) && !strcmp(out, "$HOME/.ssh/cfg") &&
          setup_config_problem(out), "$VARS are not expanded and stay refused");
    check(setup_config_expand("~other/.ssh/cfg", "/Users/you", out, sizeof(out)) && setup_config_problem(out),
          "~user is not expanded and stays refused");
    check(!setup_config_expand("~/.ssh/cfg", NULL, out, sizeof(out)) && !setup_config_expand("~/x", "relative", out, sizeof(out)),
          "~ without an absolute HOME is refused");
    check(setup_config_expand("", "/Users/you", out, sizeof(out)) && !out[0] && !setup_config_problem(out), "empty stays empty");
    check(strstr(setup_config_problem("relative/cfg"), "~/"), "the problem text offers ~/");
}

int main(int argc, char **argv) {
    struct app *app = calloc(1, sizeof(*app));
    struct setup_envelope *e = calloc(1, sizeof(*e));
    if (argc != 3 || !app || !e) { free(app); free(e); fputs("usage: test-tui-setup FIXTURES EVIDENCE\n", stderr); return 2; }
    fixtures = argv[1]; evidence = argv[2];
    (void)mkdir(evidence, 0700);
    json_cases();
    envelope_cases(e);
    list_cases(e);
    argv_cases(e);
    language_cases();
    config_path_cases();
    app->hydra = "hydra"; app->view = 6; app->no_color = true; app->ascii = false;
    if (!native_setup_state(app)) { free(app); free(e); return 1; }
    screens_at(app, 80, 24);
    screens_at(app, 140, 40);
    key_cases(app);
    trust_cases(app);
    running_cases(app);
    edit_cases(app);
    default_name_cases(app);
    done_cases(app);
    agents_cases(app);
    fixable_cases(app);
    observed_cases(app);
    freshness_cases(app);
    native_setup_destroy(app);
    frame_free(app); transcript_free(app->transcript);
    free(app); free(e);
    printf("%s: %d failure%s\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
