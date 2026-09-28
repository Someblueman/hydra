#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* Remote setup controller: which CLI child runs, what its envelope leads to,
 * and the keys of each setup screen. Mutations run only as the CLI's own
 * data.next.argv after the user approved exactly the plan it returned. */
#define SETUP_STEP_BUDGET_MS (30L * 60L * 1000L)
#define SETUP_READ_BUDGET_MS (2L * 60L * 1000L)
#define SETUP_AUTOMATIC_LIMIT 8U

struct setup_argv { char *argv[SETUP_ARG_MAX + 8]; size_t count; };

static void argv_add(struct setup_argv *a, const char *word) {
    if (a->count + 1 < sizeof(a->argv) / sizeof(a->argv[0])) a->argv[a->count++] = (char *)word;
    a->argv[a->count] = NULL;
}

struct native_setup *native_setup_state(struct app *app) {
    if (app->setup) return app->setup;
    app->setup = calloc(1, sizeof(*app->setup));
    if (!app->setup) return NULL;
    app->setup->current = calloc(1, sizeof(struct setup_envelope));
    app->setup->incoming = calloc(1, sizeof(struct setup_envelope));
    app->setup->sweep_result = calloc(1, sizeof(struct setup_envelope));
    app->setup->job.fd = app->setup->sweep.fd = -1;
    if (!app->setup->current || !app->setup->incoming || !app->setup->sweep_result) { native_setup_destroy(app); return NULL; }
    tv_input_init(&app->setup->input);
    return app->setup;
}

void native_setup_destroy(struct app *app) {
    struct native_setup *s = app->setup;
    if (!s) return;
    /* Never interrupt a remote change: the child finishes and records it. */
    setup_capture_release(&s->job);
    setup_capture_release(&s->sweep);
    free(s->current); free(s->incoming); free(s->sweep_result);
    free(s); app->setup = NULL;
}

bool native_setup_active(const struct app *app) { return app->setup && app->setup->open; }
bool native_setup_running(const struct app *app) { return app->setup && app->setup->job.active; }

static void setup_notice(struct native_setup *s, const char *text) { copy_text(s->notice, sizeof(s->notice), text); }

/* ---- Children ---- */

static bool job_start(struct app *app, struct native_setup *s, enum setup_job kind, const struct setup_argv *a, const char *step) {
    long budget = kind == SETUP_JOB_GUIDED || kind == SETUP_JOB_APPROVED ? SETUP_STEP_BUDGET_MS : SETUP_READ_BUDGET_MS;
    if (s->job.active) { setup_notice(s, "A setup step is still running; wait for its result"); return false; }
    if (!setup_capture_start(&s->job, a->argv, budget)) { setup_notice(s, "Hydra could not start the setup command"); return false; }
    s->job_kind = kind; s->job_started = time(NULL);
    copy_text(s->job_step, sizeof(s->job_step), step ? step : "");
    s->screen = SETUP_SCREEN_RUNNING; s->notice[0] = '\0'; s->scroll = 0;
    (void)app;
    return true;
}

/* hydra remote setup NAME [DEST [--ssh-config FILE]] --json */
static void guided_start(struct app *app, struct native_setup *s, bool first) {
    struct setup_argv a = {{NULL}, 0};
    const struct setup_step *open = setup_open_step(s->current);
    argv_add(&a, app->hydra); argv_add(&a, "remote"); argv_add(&a, "setup"); argv_add(&a, s->name);
    if (first) {
        argv_add(&a, s->destination);
        if (s->config[0]) { argv_add(&a, "--ssh-config"); argv_add(&a, s->config); }
    }
    argv_add(&a, "--json");
    (void)job_start(app, s, SETUP_JOB_GUIDED, &a, first ? "host_key" : open ? open->id : "");
}

static void status_start(struct app *app, struct native_setup *s, enum setup_job kind) {
    struct setup_argv a = {{NULL}, 0};
    argv_add(&a, app->hydra); argv_add(&a, "remote"); argv_add(&a, "setup"); argv_add(&a, "status");
    argv_add(&a, s->name); argv_add(&a, "--json");
    (void)job_start(app, s, kind, &a, kind == SETUP_JOB_RETURNED ? s->handoff_step : "");
}

/* Read-only views of one step: hydra remote preflight|agents NAME --json. */
static void inspect_start(struct app *app, struct native_setup *s, const char *word, const char *step) {
    struct setup_argv a = {{NULL}, 0};
    argv_add(&a, app->hydra); argv_add(&a, "remote"); argv_add(&a, word); argv_add(&a, s->name); argv_add(&a, "--json");
    (void)job_start(app, s, SETUP_JOB_INSPECT, &a, step);
}

/* The CLI's own next command, with argv[0] replaced by this Hydra. */
static bool next_argv(struct app *app, const struct native_setup *s, struct setup_argv *a, bool json) {
    size_t i;
    if (!setup_argv_valid(s->current, s->name)) return false;
    argv_add(a, app->hydra);
    for (i = 1; i < s->current->argc; i++) if (json || strcmp(s->current->argv[i], "--json")) argv_add(a, s->current->argv[i]);
    return true;
}

static void refuse(struct native_setup *s) {
    s->current->ok = false;
    copy_text(s->current->code, sizeof(s->current->code), "refused_command");
    s->current->message[0] = s->current->recovery[0] = '\0';
    s->screen = SETUP_SCREEN_ERROR;
}

/* ---- Terminal hand-off ---- */

static bool handoff_prepare(struct native_setup *s) {
    size_t i;
    if (!setup_argv_valid(s->current, s->name)) return false;
    s->handoff_argc = 0;
    for (i = 0; i < s->current->argc; i++) {
        if (!strcmp(s->current->argv[i], "--json")) continue;
        copy_text(s->handoff[s->handoff_argc++], SETUP_ARG_TEXT, s->current->argv[i]);
    }
    copy_text(s->handoff_step, sizeof(s->handoff_step), s->current->next_step);
    return true;
}

static void handoff_start(struct app *app, struct native_setup *s) {
    struct setup_argv a = {{NULL}, 0};
    size_t i;
    if (!s->handoff_argc) { refuse(s); return; }
    argv_add(&a, app->hydra);
    for (i = 1; i < s->handoff_argc; i++) argv_add(&a, s->handoff[i]);
    s->handoff_exit = setup_terminal_handoff(app, a.argv, s->handoff_step);
    s->handed_off = true;
    status_start(app, s, SETUP_JOB_RETURNED);
}

/* ---- Results ---- */

static void no_result(struct native_setup *s, int exit_status) {
    struct setup_envelope *e = s->incoming;
    memcpy(e, s->current, sizeof(*e));
    e->parsed = true; e->ok = false; e->complete = false; e->exit_status = exit_status;
    e->argc = e->plan_count = e->requirement_count = e->agent_count = 0;
    e->plan_sha256[0] = e->fingerprint[0] = e->next_step[0] = e->recovery[0] = '\0';
    copy_text(e->code, sizeof(e->code), "no_result");
    if (exit_status == 124) copy_text(e->message, sizeof(e->message), "The setup command did not finish before its deadline and was stopped.");
    else snprintf(e->message, sizeof(e->message), "The setup command ended with exit status %d and no readable result.", exit_status);
}

static void show(struct native_setup *s) {
    s->screen = setup_screen_for(s->current);
    s->scroll = 0; s->selected = 0; s->typed[0] = '\0'; s->typed_cursor = 0;
    if (s->screen == SETUP_SCREEN_HANDOFF && !handoff_prepare(s)) refuse(s);
}

/* Continues after an approved or handed-off step finished. A bounded number
 * of automatic runs keeps a misbehaving result from looping. */
static bool automatic_continue(struct app *app, struct native_setup *s) {
    if (++s->automatic <= SETUP_AUTOMATIC_LIMIT) {
        guided_start(app, s, false);
        if (s->job.active) return true;
    } else setup_notice(s, "Setup paused; Continue setup resumes it");
    show(s);
    return false;
}

static const struct setup_step *find_step(const struct setup_envelope *e, const char *id) {
    size_t i;
    for (i = 0; i < e->step_count; i++) if (!strcmp(e->steps[i].id, id)) return &e->steps[i];
    return NULL;
}

/* After the terminal came back: continue only when the CLI recorded the step
 * as finished; otherwise explain what is known and offer the step again. */
static void returned_result(struct app *app, struct native_setup *s) {
    struct setup_envelope *e = s->current;
    const struct setup_step *step = find_step(e, s->handoff_step);
    const char *code = s->handoff_exit >= 128 ? "cancelled" : !strncmp(s->handoff_step, "sign_in:", 8) ? "sign_in_failed" : "install_failed";
    if (!e->ok) { show(s); return; }
    if (step && setup_status_finished(step->status) && automatic_continue(app, s)) return;
    if (step && setup_status_finished(step->status)) return;
    e->ok = false; e->exit_status = s->handoff_exit;
    copy_text(e->code, sizeof(e->code), code);
    snprintf(e->message, sizeof(e->message), "The command ended with exit status %d; the step is now %s.",
             s->handoff_exit, step ? setup_status_label(step->status) : "unrecorded");
    copy_text(e->recovery, sizeof(e->recovery), step ? step->detail : "");
    s->screen = SETUP_SCREEN_ERROR; s->scroll = 0;
}

static void job_result(struct app *app, struct native_setup *s, enum setup_job kind) {
    if (kind == SETUP_JOB_RETURNED) { returned_result(app, s); return; }
    if (kind == SETUP_JOB_APPROVED && s->current->ok && automatic_continue(app, s)) return;
    show(s);
    if (kind == SETUP_JOB_APPROVED && s->screen == SETUP_SCREEN_STEPS) setup_notice(s, "Approved step finished");
}

static void job_finish(struct app *app, struct native_setup *s) {
    struct setup_envelope *swap;
    enum setup_job kind = s->job_kind;
    size_t length;
    int exit_status;
    char *text = setup_capture_finish(&s->job, &length, &exit_status);
    if (!text || setup_envelope_parse(s->incoming, text, length, exit_status)) no_result(s, exit_status);
    free(text);
    s->job_kind = SETUP_JOB_NONE;
    swap = s->current; s->current = s->incoming; s->incoming = swap;
    job_result(app, s, kind);
    if (!s->job.active) native_setup_hosts_refresh(app);
    if (!s->open && !s->job.active)
        snprintf(app->notice, sizeof(app->notice), s->screen == SETUP_SCREEN_DONE ? "Remote host %.100s is set up" :
                 "Remote setup for %.100s needs you: open Hosts (H) to continue", s->name);
}

void native_setup_tick(struct app *app) {
    struct native_setup *s = app->setup;
    if (!s) return;
    if (s->job.active && setup_capture_step(&s->job)) job_finish(app, s);
    native_setup_sweep_tick(app);
}

/* ---- Entry points ---- */

void native_setup_open_form(struct app *app) {
    struct native_setup *s = native_setup_state(app);
    if (!s) { copy_text(app->notice, sizeof(app->notice), "Remote setup is unavailable: out of memory"); return; }
    s->open = true;
    if (s->job.active) { s->screen = SETUP_SCREEN_RUNNING; return; }
    memset(s->fields, 0, sizeof(s->fields));
    s->focus = SETUP_FIELD_DESTINATION; s->problem[0] = s->notice[0] = '\0';
    s->screen = SETUP_SCREEN_FORM; s->automatic = 0;
    tv_input_init(&s->input);
}

void native_setup_resume(struct app *app, const char *name) {
    struct native_setup *s = native_setup_state(app);
    if (!s) return;
    s->open = true; s->automatic = 0; s->notice[0] = '\0';
    tv_input_init(&s->input);
    if (s->job.active) { s->screen = SETUP_SCREEN_RUNNING; return; }
    copy_text(s->name, sizeof(s->name), name);
    memset(s->current, 0, sizeof(*s->current));
    status_start(app, s, SETUP_JOB_STATUS);
}

static void close_flow(struct app *app, struct native_setup *s) {
    s->open = false;
    if (s->job.active) snprintf(app->notice, sizeof(app->notice), "Setup for %.100s keeps running; Hosts shows its progress", s->name);
    native_setup_hosts_refresh(app);
}

/* ---- Actions ---- */

static void submit_form(struct app *app, struct native_setup *s) {
    struct setup_field *f = s->fields;
    const char *problem;
    if (!f[SETUP_FIELD_NAME].text[0]) {
        setup_default_name(f[SETUP_FIELD_DESTINATION].text, f[SETUP_FIELD_NAME].text, sizeof(f[0].text));
        f[SETUP_FIELD_NAME].cursor = strlen(f[SETUP_FIELD_NAME].text);
    }
    if ((problem = setup_destination_problem(f[SETUP_FIELD_DESTINATION].text))) s->focus = SETUP_FIELD_DESTINATION;
    else if ((problem = setup_name_problem(f[SETUP_FIELD_NAME].text))) s->focus = SETUP_FIELD_NAME;
    else if ((problem = setup_config_problem(f[SETUP_FIELD_CONFIG].text))) s->focus = SETUP_FIELD_CONFIG;
    if (problem) { copy_text(s->problem, sizeof(s->problem), problem); return; }
    s->problem[0] = '\0';
    copy_text(s->destination, sizeof(s->destination), f[SETUP_FIELD_DESTINATION].text);
    copy_text(s->name, sizeof(s->name), f[SETUP_FIELD_NAME].text);
    copy_text(s->config, sizeof(s->config), f[SETUP_FIELD_CONFIG].text);
    memset(s->current, 0, sizeof(*s->current));
    guided_start(app, s, true);
}

static void approve(struct app *app, struct native_setup *s) {
    struct setup_argv a = {{NULL}, 0};
    if (setup_step_needs_terminal(s->current->next_step)) {
        if (handoff_prepare(s)) handoff_start(app, s);
        else refuse(s);
        return;
    }
    if (!next_argv(app, s, &a, true)) { refuse(s); return; }
    (void)job_start(app, s, SETUP_JOB_APPROVED, &a, s->current->next_step);
}

static void decline(struct native_setup *s) {
    s->screen = SETUP_SCREEN_STEPS; s->selected = 0; s->typed[0] = '\0';
    snprintf(s->notice, sizeof(s->notice), "Not approved: nothing was changed on %.200s. Continue setup shows the plan again.",
             s->current->destination[0] ? s->current->destination : s->name);
}

static void step_enter(struct app *app, struct native_setup *s) {
    const struct setup_step *step;
    if (!s->selected || s->selected > s->current->step_count) { guided_start(app, s, false); return; }
    step = &s->current->steps[s->selected - 1];
    if (!strcmp(step->id, "preflight")) inspect_start(app, s, "preflight", "preflight");
    else if (!strcmp(step->id, "agents") && setup_status_finished(step->status)) inspect_start(app, s, "agents", "agents");
    else setup_notice(s, "Continue setup (the first row) runs the next step");
}

static bool retry_sign_in(struct native_setup *s) {
    if (!strncmp(s->current->next_step, "sign_in:", 8) && handoff_prepare(s)) return true;
    return !strncmp(s->handoff_step, "sign_in:", 8) && s->handoff_argc;
}

static void error_enter(struct app *app, struct native_setup *s) {
    struct setup_explanation x;
    setup_explain(s->current->code, &x);
    if (!x.action) { setup_notice(s, "There is nothing to retry from here; Esc goes back"); return; }
    if (!strcmp(s->current->code, "no_result")) { status_start(app, s, SETUP_JOB_STATUS); return; }
    if (!strncmp(s->current->code, "sign_in", 7) && retry_sign_in(s)) { handoff_start(app, s); return; }
    guided_start(app, s, false);
}

static void trust_enter(struct app *app, struct native_setup *s) {
    if (!strcmp(s->typed, "yes")) approve(app, s);
    else setup_notice(s, "Type yes (the whole word) to trust this key, or press Esc to decline");
}

static void enter(struct app *app, struct native_setup *s) {
    switch (s->screen) {
        case SETUP_SCREEN_FORM: submit_form(app, s); break;
        case SETUP_SCREEN_STEPS: step_enter(app, s); break;
        case SETUP_SCREEN_TRUST_KEY: trust_enter(app, s); break;
        case SETUP_SCREEN_PLAN: setup_notice(s, "Press y to approve this exact plan, or n / Esc to decline"); break;
        case SETUP_SCREEN_HANDOFF: handoff_start(app, s); break;
        case SETUP_SCREEN_ERROR: error_enter(app, s); break;
        case SETUP_SCREEN_DONE: close_flow(app, s); break;
        case SETUP_SCREEN_RUNNING: break;
        default: guided_start(app, s, false); break;
    }
}

static void back(struct app *app, struct native_setup *s) {
    switch (s->screen) {
        case SETUP_SCREEN_TRUST_KEY: case SETUP_SCREEN_PLAN: decline(s); return;
        case SETUP_SCREEN_FORM: case SETUP_SCREEN_STEPS: case SETUP_SCREEN_DONE: case SETUP_SCREEN_RUNNING:
            close_flow(app, s); return;
        default: break;
    }
    if (!s->current->step_count) { close_flow(app, s); return; }
    s->screen = SETUP_SCREEN_STEPS; s->selected = 0; s->scroll = 0;
}

/* ---- Keys ---- */

static bool text_screen(const struct native_setup *s) {
    return s->screen == SETUP_SCREEN_FORM || s->screen == SETUP_SCREEN_TRUST_KEY;
}

static void text_target(struct native_setup *s, char **text, size_t *size, size_t **cursor) {
    if (s->screen == SETUP_SCREEN_FORM) {
        *text = s->fields[s->focus].text; *size = sizeof(s->fields[0].text); *cursor = &s->fields[s->focus].cursor;
    } else { *text = s->typed; *size = sizeof(s->typed); *cursor = &s->typed_cursor; }
}

static void text_insert(struct native_setup *s, const struct tv_event *e) {
    char *text; size_t size, *cursor, length, i;
    text_target(s, &text, &size, &cursor);
    length = strlen(text);
    for (i = 0; i < e->length && length + 1 < size; i++) {
        unsigned char c = (unsigned char)e->bytes[i];
        if (c < 32 || c == 127) continue;
        memmove(text + *cursor + 1, text + *cursor, length - *cursor + 1);
        text[(*cursor)++] = (char)c; length++;
    }
}

/* Editing keys of the focused text; false for keys the screen handles. */
static bool text_key(struct native_setup *s, const struct tv_event *e) {
    char *text; size_t size, *cursor, length;
    text_target(s, &text, &size, &cursor);
    length = strlen(text);
    if (*cursor > length) *cursor = length;
    if (e->key == 127 || e->key == 8) {
        if (!*cursor) return true;
        memmove(text + *cursor - 1, text + *cursor, length - *cursor + 1); (*cursor)--;
    } else if (e->key == TV_KEY_LEFT) { if (*cursor) (*cursor)--; }
    else if (e->key == TV_KEY_RIGHT) { if (*cursor < length) (*cursor)++; }
    else if (e->key == TV_KEY_HOME) *cursor = 0;
    else if (e->key == TV_KEY_END) *cursor = length;
    else if (e->key >= 32 && e->key < TV_KEY_UP && e->key != 127) text_insert(s, e);
    else return false;
    s->problem[0] = '\0';
    return true;
}

static void move(struct native_setup *s, int direction) {
    if (s->screen == SETUP_SCREEN_FORM) { s->focus = (s->focus + SETUP_FORM_FIELDS + direction) % SETUP_FORM_FIELDS; return; }
    if (s->screen == SETUP_SCREEN_STEPS) {
        if (direction < 0 && s->selected) s->selected--;
        else if (direction > 0 && s->selected < s->current->step_count) s->selected++;
        return;
    }
    if (direction < 0 && s->scroll) s->scroll--;
    else if (direction > 0 && s->more) s->scroll++;
}

static void page(struct app *app, struct native_setup *s, int direction) {
    int rows = app->rows > 12 ? app->rows / 2 : 6;
    while (rows-- > 0) move(s, direction);
}

/* Single-letter commands of screens without a text field. */
static void letter(struct app *app, struct native_setup *s, uint32_t key) {
    if (key == 'j') move(s, 1);
    else if (key == 'k') move(s, -1);
    else if (key == 'y' && s->screen == SETUP_SCREEN_PLAN) approve(app, s);
    else if (key == 'n' && s->screen == SETUP_SCREEN_PLAN) decline(s);
    else if (key == 'q' && !s->job.active) app->running = false;
}

static void screen_key(struct app *app, struct native_setup *s, const struct tv_event *e) {
    uint32_t key = e->key;
    if (key == TV_KEY_SEQUENCE && !strcmp(e->bytes, "\033[Z")) move(s, -1);
    else if (key == '\t') move(s, 1);
    else if (key == TV_KEY_UP || key == TV_KEY_DOWN) move(s, key == TV_KEY_UP ? -1 : 1);
    else if (key == TV_KEY_PAGE_UP || key == TV_KEY_PAGE_DOWN) page(app, s, key == TV_KEY_PAGE_UP ? -1 : 1);
    else if (key == '\r' || key == '\n') enter(app, s);
    else if (key == 27) back(app, s);
    else if (!text_screen(s) && key < 128) letter(app, s, key);
}

static void setup_event(struct app *app, struct native_setup *s, const struct tv_event *e) {
    if (e->type == TV_MOUSE || e->type == TV_PASTE_BEGIN || e->type == TV_PASTE_END) return;
    s->automatic = 0;
    if (e->type == TV_PASTE) { if (text_screen(s)) text_insert(s, e); return; }
    if (text_screen(s) && e->key != 27 && text_key(s, e)) return;
    screen_key(app, s, e);
}

bool native_setup_byte(struct app *app, unsigned char byte) {
    struct tv_event e;
    struct native_setup *s = app->setup;
    if (!s || !s->open || app->view != 6) return false;
    if (tv_input_feed(&s->input, byte, &e)) setup_event(app, s, &e);
    return true;
}

void native_setup_flush_input(struct app *app) {
    struct tv_event e;
    struct native_setup *s = app->setup;
    if (s && s->open && tv_input_flush(&s->input, &e)) setup_event(app, s, &e);
}
