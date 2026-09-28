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
    {"host-key-approval.json", "trust-key", 3, {"TRUST THE HOST KEY OF ovh", "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8", "Type yes to trust", "type yes, then Enter"}, {"y approve", NULL, NULL}},
    {"host-key-changed.json", "key-changed", 1, {"THE HOST KEY CHANGED", "ssh-keygen -R ovh.example.net -f /Users/you/.ssh/known_hosts", "Enter check again", "SHA256:Q2hhbmdl"}, {"Type yes", "y approve", "trust this key"}},
    {"host-key-ambiguous.json", "key-ambiguous", 1, {"known_hosts already has a different entry", "ssh-keygen -F", "Enter checks again", NULL}, {"Type yes", "y approve", NULL}},
    {"preflight-blocked.json", "preflight-blocked", 1, {"MISSING REQUIREMENTS ON ovh", "BLOCKS", "fix: sudo apt-get install git", "never uses sudo"}, {NULL, NULL, NULL}},
    {"preflight-ok.json", "preflight-ok", 0, {"REQUIREMENTS ON ovh", "Nothing blocks setup", "group-writable umask", "Enter continues setup"}, {"BLOCKS", NULL, NULL}},
    {"preflight-tmux.json", "preflight-tmux", 0, {"REQUIREMENTS ON ovh", "Nothing blocks setup", "fix: sudo apt-get install tmux", "Enter continues setup"}, {"BLOCKS", "HEADS CANNOT RUN", NULL}},
    {"provision-approval.json", "provision-pinned", 3, {"REVIEW: INSTALL HYDRA ON ovh", "pinned: its digest is recorded", "Install location", "Hydra release download"}, {"Not a release build", "UNPINNED", NULL}},
    {"provision-approval-unpinned.json", "provision-unpinned", 3, {"REVIEW: INSTALL HYDRA ON ovh", "Not a release build: this helper", "Binary SHA-256", "own hydra-fleet helper"}, {"UNPINNED", NULL, NULL}},
    {"install-approval.json", "install-approval", 3, {"REVIEW: INSTALL claude ON ovh", "umask 022; curl -fsSL https://claude.ai/install.sh", "succeeds;", "y approve"}, {NULL, NULL, NULL}},
    {"agents.json", "agents", 0, {"AGENTS ON ovh", "> Continue setup", "Install claude", "Sign in to codex"}, {NULL, NULL, NULL}},
    {"agents.json", "agents-inventory", 0, {"cursor-agent", "recorded location", "not installed", "codex-cli 0.46.0"}, {NULL, NULL, NULL}},
    {"outcome-unknown.json", "outcome-unknown", 4, {"cannot tell whether", "never installs twice", "Enter reconciles", "exit 4"}, {NULL, NULL, NULL}},
    {"sign-in-tty.json", "sign-in-handoff", 1, {"SIGN IN TO CLAUDE ON OVH", "hydra remote sign-in ovh --agent claude", "return here at once", "after a failure"}, {"--json", NULL, NULL}},
    {"sign-in-failed.json", "sign-in-failed", 1, {"Sign-in did not finish", "sign-in exited with a nonzero status", "Enter signs in again", NULL}, {NULL, NULL, NULL}},
    {"done.json", "done", 0, {"ovh IS READY", "Sign in to claude", "hydra fleet tui", NULL}, {NULL, NULL, NULL}},
    {"fresh.json", "steps", 0, {"SET UP ovh", "> Continue setup", "Trust the host key", "not started"}, {NULL, NULL, NULL}},
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

static void screen_case(struct app *app, const struct screen_case *c, int cols, int rows) {
    static char all[1 << 20];
    char name[160], first[65536];
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows;
    check(load(c->file, s->current, c->exit_status), c->file);
    copy_text(s->name, sizeof(s->name), "ovh");
    s->open = true; s->notice[0] = '\0'; s->paused = false;
    s->screen = setup_screen_for(s->current);
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
    check(contains_all(all, c->must, 4) && contains_none(all, c->never, 3) && strstr(first, "[Hosts]"), name);
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
    snprintf(name, sizeof(name), "local Hosts tab offers Add a host and Continue setup at %dx%d", cols, rows);
    check(strstr(frame, "> + Add a host") && strstr(frame, "Install Hydra: needs your approval") && strstr(frame, "A add a host"), name);
}

/* The host-key screen explains itself in full without scrolling, and the
 * typed answer sits on the row directly above its notice. */
static void trust_fit_case(struct app *app, int cols, int rows) {
    char frame[65536], name[128];
    const char *prompt, *next;
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows;
    check(load("host-key-approval.json", s->current, 3), "host-key fixture loads");
    s->open = true; s->notice[0] = '\0'; s->scroll = 0; s->typed[0] = '\0';
    s->screen = SETUP_SCREEN_TRUST_KEY;
    check(render_capture(app, frame, sizeof(frame)), "host-key screen renders");
    prompt = strstr(frame, "Type yes to trust this key: _");
    next = prompt ? strchr(prompt, '\n') : NULL;
    snprintf(name, sizeof(name), "host-key screen at %dx%d fits its explanation above an adjacent prompt", cols, rows);
    check(!s->more && !strstr(frame, "more below") && strstr(frame, "machine itself") &&
          strstr(frame, "appends exactly one line") && strstr(frame, "refuses to connect rather than replace it.") &&
          next && strstr(next + 1, "Anything but yes") && strchr(next + 1, '\n') > strstr(next + 1, "Anything but yes"), name);
}

/* A guided run paused because tmux is missing: the first frame explains why
 * and shows the tmux row with its suggested fix, above other warnings. */
static void paused_case(struct app *app, int cols, int rows) {
    static const char *const must[] = {"HEADS CANNOT RUN ON ovh YET", "cannot run heads there", "install tmux yourself",
                                       "fix: sudo apt-get install tmux", "Enter continues setup anyway"};
    char frame[65536], name[128];
    const char *tmux, *umask;
    struct native_setup *s = app->setup;
    app->cols = cols; app->rows = rows;
    check(load("preflight-tmux.json", s->current, 0), "tmux preflight fixture loads");
    s->open = true; s->notice[0] = '\0'; s->scroll = 0;
    s->screen = SETUP_SCREEN_PREFLIGHT; s->paused = true;
    check(render_capture(app, frame, sizeof(frame)), "paused requirements render");
    keep("preflight-paused", cols, rows, frame);
    tmux = strstr(frame, "warning  tmux"); umask = strstr(frame, "warning  umask");
    snprintf(name, sizeof(name), "paused requirements at %dx%d explain the pause and list tmux first", cols, rows);
    check(contains_all(frame, must, 5) && !strstr(frame, "BLOCKS") && tmux && umask && tmux < umask &&
          strstr(frame, "Enter continue anyway"), name);
    s->paused = false;
}

static void screens_at(struct app *app, int cols, int rows) {
    size_t i;
    for (i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) screen_case(app, &screens[i], cols, rows);
    hub_case(app, cols, rows);
    trust_fit_case(app, cols, rows);
    paused_case(app, cols, rows);
}

/* Keys that must never run anything: declining, wrong confirmation text and
 * keys on a changed-key screen. */
static void key_cases(struct app *app) {
    struct native_setup *s = app->setup;
    const char *keys;
    app->cols = 100; app->rows = 30; s->open = true;
    load("provision-approval.json", s->current, 3); s->screen = SETUP_SCREEN_PLAN;
    for (keys = "\r"; *keys; keys++) (void)native_setup_byte(app, (unsigned char)*keys);
    check(s->screen == SETUP_SCREEN_PLAN && !s->job.active && strstr(s->notice, "Press y"), "Enter does not approve a plan");
    (void)native_setup_byte(app, 'n');
    check(s->screen == SETUP_SCREEN_STEPS && !s->job.active && strstr(s->notice, "nothing was changed"), "n declines and runs nothing");
    load("host-key-approval.json", s->current, 3); s->screen = SETUP_SCREEN_TRUST_KEY; s->typed[0] = '\0'; s->typed_cursor = 0;
    for (keys = "y\r"; *keys; keys++) (void)native_setup_byte(app, (unsigned char)*keys);
    check(s->screen == SETUP_SCREEN_TRUST_KEY && !s->job.active && strstr(s->notice, "Type yes"), "a partial yes does not trust the key");
    (void)native_setup_byte(app, 27); native_setup_flush_input(app);
    check(s->screen == SETUP_SCREEN_STEPS && !s->job.active, "Esc declines the host key");
    load("agents.json", s->current, 0); s->screen = SETUP_SCREEN_AGENTS; s->selected = 0;
    (void)native_setup_byte(app, 'j'); (void)native_setup_byte(app, 'j');
    check(s->screen == SETUP_SCREEN_AGENTS && s->selected == 2 && !s->job.active, "arrows choose an agent without running anything");
    load("host-key-changed.json", s->current, 1); s->screen = SETUP_SCREEN_KEY_CHANGED;
    for (keys = "yes"; *keys; keys++) (void)native_setup_byte(app, (unsigned char)*keys);
    check(s->screen == SETUP_SCREEN_KEY_CHANGED && !s->job.active, "no key accepts a changed host key");
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
    check(render_capture(app, frame, sizeof(frame)), "running screen renders");
    keep("running", 80, 24, frame);
    check(strstr(frame, "Checking the host key\xe2\x80\xa6 12s") && strstr(frame, "Esc or c cancel"),
          "a running step shows its phrase, elapsed seconds and how to cancel");
    (void)native_setup_byte(app, 'c');
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
    check(snapshot_freshness(app, now - 1, false, false, text, sizeof(text)) == TV_MUTED && !strcmp(text, "Live"),
          "a current snapshot is Live, without a restarting age");
    check(snapshot_freshness(app, now - 4, false, false, text, sizeof(text)) == TV_MUTED && !strcmp(text, "Live"),
          "two refresh intervals are still Live");
    check(snapshot_freshness(app, now - 9, false, false, text, sizeof(text)) == TV_BASE && !strcmp(text, "Updated 9s ago"),
          "an overdue refresh shows the age counting up");
    check(snapshot_freshness(app, now - 12, true, false, text, sizeof(text)) == TV_WARNING &&
          !strcmp(text, "STALE: last good snapshot, updated 12s ago"), "a failed refresh shows STALE with the age");
    check(snapshot_freshness(app, 0, false, false, text, sizeof(text)) == TV_MUTED && !strcmp(text, "Waiting for the first snapshot"),
          "before the first snapshot nothing claims to be live");
    check(snapshot_freshness(app, now - 60, false, true, text, sizeof(text)) == TV_MUTED && !strcmp(text, "Live"),
          "headless fixtures render deterministically");
    app->fleet = true;
    check(snapshot_freshness(app, now - 6, false, false, text, sizeof(text)) == TV_MUTED && !strcmp(text, "Live"),
          "fleet mode allows one remote request per refresh");
    check(snapshot_freshness(app, now - 8, false, false, text, sizeof(text)) == TV_BASE && !strcmp(text, "Updated 8s ago"),
          "an overdue fleet refresh shows the age");
    app->fleet = fleet;
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
    running_cases(app);
    freshness_cases(app);
    native_setup_destroy(app);
    frame_free(app); transcript_free(app->transcript);
    free(app); free(e);
    printf("%s: %d failure%s\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
