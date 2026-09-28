#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* Remote setup screens. Every value shown comes from the CLI envelope; the
 * fixed sentences explain the step and its guarantees in plain language. */

/* A scrolled, word-wrapped body between fixed title and footer rows. */
struct pen { struct app *app; size_t row, skip; int bottom; bool more; };

static void pen_emit(struct pen *p, int indent, enum tv_style tone, const char *text, size_t length) {
    char line[1024];
    struct app *app = p->app;
    if (p->row++ < p->skip) return;
    if (app->line >= p->bottom) { p->more = true; return; }
    snprintf(line, sizeof(line), "%.*s", (int)(length < sizeof(line) ? length : sizeof(line) - 1), text);
    column(app, indent, app->content_width - indent, tone, line);
    app->line++;
}

/* Bytes of text that fit in width columns, preferring to break at a space. */
static size_t wrap_length(const char *text, size_t length, int width) {
    size_t i = 0, cut = 0;
    int columns = 0;
    while (i < length && columns < width) {
        if (text[i] == ' ') cut = i;
        i++;
        while (i < length && ((unsigned char)text[i] & 0xc0) == 0x80) i++;
        columns++;
    }
    if (i >= length) return length;
    return cut > 0 ? cut : i;
}

/* A label column and a value wrapped with a hanging indent under itself;
 * an empty label wraps plain text at x. */
struct hanging { int x, width; const char *label; enum tv_style label_tone; };

static void pen_hanging(struct pen *p, const struct hanging *h, const char *value, enum tv_style tone) {
    int width = p->app->content_width - h->x - h->width;
    const char *rest = value;
    if (width < 8) width = 8;
    do {
        size_t take = wrap_length(rest, strlen(rest), width);
        if (p->row >= p->skip && p->app->line < p->bottom && rest == value && h->label[0])
            column(p->app, h->x, h->width, h->label_tone, h->label);
        pen_emit(p, h->x + h->width, tone, rest, take);
        /* wrap_length never exceeds strlen(rest), so rest stays within the
         * string and at worst reaches its terminator. */
        rest += take;
        while (*rest == ' ') rest++; // NOLINT(clang-analyzer-security.ArrayBound)
    } while (*rest);
}

static void pen_text(struct pen *p, int indent, enum tv_style tone, const char *text) {
    struct hanging h = {indent, 0, "", TV_BASE};
    pen_hanging(p, &h, text, tone);
}

static void pen_pair(struct pen *p, const char *label, const char *value, enum tv_style tone) {
    struct hanging h = {2, p->app->content_width < 70 ? 14 : 20, label, TV_MUTED};
    pen_hanging(p, &h, value, tone);
}

static void title(struct app *app, enum tv_style tone, const char *text) {
    style(app, tone == TV_WARNING ? TONE_WARNING : tone == TV_SUCCESS ? TONE_SUCCESS : TONE_STRONG);
    linef(app, "%s", text);
    style(app, TONE_BASE);
}

static void pen_begin(struct pen *p, struct app *app, int footer) {
    p->app = app; p->row = 0; p->skip = app->setup->scroll; p->more = false;
    p->bottom = app->limit - footer;
}

/* Footer rows: the scroll marker, then the screen's notice or prompt. */
static void footer(struct app *app, struct pen *p, const char *prompt, enum tv_style tone) {
    struct native_setup *s = app->setup;
    s->more = p->more;
    app->line = app->limit - 2;
    if (p->more || p->skip) {
        column(app, 0, app->content_width, TV_MUTED, p->more ? (app->ascii ? "v more below: Down or PgDn scrolls" : "\xe2\x86\x93 more below: Down or PgDn scrolls")
                                                               : "Up or PgUp scrolls back");
    }
    app->line++;
    column(app, 0, app->content_width, s->notice[0] ? TV_WARNING : tone, s->notice[0] ? s->notice : prompt);
    app->line++;
}

/* ---- Form ---- */

static void field_row(struct app *app, struct native_setup *s, int index, const char *label, const char *help) {
    const struct setup_field *f = &s->fields[index];
    int label_width = app->content_width < 70 ? 14 : 20, width = app->content_width - label_width - 2;
    size_t start = 0, length = strlen(f->text);
    bool focused = s->focus == index;
    char shown[1100];
    if (width > 70) width = 70;
    while (f->cursor > start && (int)(f->cursor - start) >= width - 1) start++;
    snprintf(shown, sizeof(shown), "%.*s", width - 1, f->text + start);
    column(app, 0, label_width + 2, focused ? TV_STRONG : TV_MUTED, label);
    column(app, label_width + 2, width, focused ? TV_SELECTED : TV_BORDER, "");
    column(app, label_width + 2, width, focused ? TV_SELECTED : TV_BASE, shown);
    if (focused && f->cursor <= length) column(app, label_width + 2 + (int)(f->cursor - start), 1, TV_FOCUS, f->cursor < length ? "" : "_");
    app->line++;
    if (app->line < app->limit) { column(app, label_width + 2, width, TV_MUTED, help); app->line++; }
}

static void render_form(struct app *app, struct native_setup *s) {
    title(app, TV_STRONG, "ADD A REMOTE HOST");
    paragraph(app, "Hydra connects with your own SSH setup. It checks the machine and asks before every change; nothing changes on the remote until you approve it.", TV_BASE);
    linef(app, "");
    field_row(app, s, SETUP_FIELD_DESTINATION, "SSH destination", "user@host, or a Host from ~/.ssh/config");
    field_row(app, s, SETUP_FIELD_NAME, "Name in Hydra", "empty: the host name is used");
    field_row(app, s, SETUP_FIELD_CONFIG, "SSH config file", "optional; empty uses your usual SSH config");
    linef(app, "");
    if (s->problem[0]) paragraph(app, s->problem, TV_WARNING);
    else paragraph(app, "Next: Hydra reads the machine's SSH host key and shows it for you to confirm.", TV_MUTED);
}

/* ---- Steps ---- */

static enum tv_style step_tone(const char *status) {
    if (!strcmp(status, "done") || !strcmp(status, "running")) return TV_SUCCESS;
    if (!strcmp(status, "pending") || !strcmp(status, "skipped")) return TV_MUTED;
    return TV_WARNING;
}

static void step_row(struct app *app, struct pen *p, const struct setup_step *step, bool selected, bool running) {
    const char *status = running ? "running" : step->status;
    char line[400], label[96];
    int width = app->content_width;
    setup_step_label(step->id, label, sizeof(label));
    snprintf(line, sizeof(line), "%s %-24s %s", setup_status_mark(status, app->ascii), label, setup_status_label(status));
    if (p->row >= p->skip && app->line < p->bottom && selected) column(app, 0, width, TV_SELECTED, "");
    if (width >= 90) {
        char detail[300];
        snprintf(detail, sizeof(detail), "%-52.52s %s", line, step->detail);
        pen_emit(p, 2, selected ? TV_SELECTED : step_tone(status), detail, strlen(detail));
        return;
    }
    pen_emit(p, 2, selected ? TV_SELECTED : step_tone(status), line, strlen(line));
    if (step->detail[0]) pen_text(p, 6, TV_MUTED, step->detail);
}

/* The action row: what is running, or the selectable Continue setup. */
static void steps_action(struct app *app, const struct native_setup *s, bool running) {
    char work[200], label[96];
    if (!running) {
        column(app, 0, app->content_width, s->selected ? TV_STRONG : TV_SELECTED, s->selected ? "  Continue setup" : "> Continue setup");
        app->line++;
        return;
    }
    setup_step_label(s->job_step[0] ? s->job_step : "setup", label, sizeof(label));
    snprintf(work, sizeof(work), "Working: %s... %lds", s->job_step[0] ? label : "checking setup status", (long)(time(NULL) - s->job_started));
    column(app, 0, app->content_width, TV_SUCCESS, work);
    app->line++;
}

static void render_steps(struct app *app, struct native_setup *s, bool running) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char heading[512];
    size_t i;
    snprintf(heading, sizeof(heading), "SET UP %.120s%s%.200s", s->name, dot(app), e->destination[0] ? e->destination : s->destination);
    title(app, TV_STRONG, heading);
    steps_action(app, s, running);
    linef(app, "");
    pen_begin(&p, app, 2);
    for (i = 0; i < e->step_count; i++)
        step_row(app, &p, &e->steps[i], !running && s->selected == i + 1, running && !strcmp(e->steps[i].id, s->job_step));
    if (!e->step_count) pen_text(&p, 2, TV_MUTED, running ? "Contacting the machine..." : "No steps recorded yet.");
    footer(app, &p, running ? "Esc keeps this step running in the background."
                            : "Continue setup runs the next step; changes ask for approval.", TV_MUTED);
}

/* ---- Plans ---- */

static const char *const plan_labels[][2] = {
    {"fingerprint", "Fingerprint"}, {"key_type", "Key type"}, {"host", "Host"}, {"hostname", "Host"}, {"port", "Port"},
    {"host_key_alias", "Host key alias"}, {"known_hosts", "Will be added to"}, {"platform", "Platform"},
    {"platform.os", "Operating system"}, {"platform.arch", "CPU"}, {"version", "Hydra version"},
    {"hydra_version", "Hydra version"}, {"binary.source", "Binary source"}, {"source", "Source"},
    {"binary.sha256", "Binary SHA-256"}, {"binary_sha256", "Binary SHA-256"}, {"binary.path", "Binary file"},
    {"package_sha256", "Package SHA-256"}, {"prefix", "Install location"}, {"agent", "Agent"},
    {"command", "Command"}, {"url", "Installer URL"}, {"docs_url", "Provider docs"}, {"recipe_source", "Recipe"},
    {"requires", "Requires"}, {"expected_dirs", "Installs into"}, {"peer_fingerprint", "Host key"},
    {"hashed", "Hashed entry"}, {"change", "Change"}, {"notes", "Notes"}, {"effects", "Effects"},
};

static bool plan_skipped(const char *key) { return !strcmp(key, "name") || !strcmp(key, "destination"); }

static void source_text(const char *value, char *out, size_t size, enum tv_style *tone) {
    if (!strcmp(value, "pinned")) { snprintf(out, size, "pinned: its digest is recorded in this Hydra release"); *tone = TV_SUCCESS; }
    else if (!strcmp(value, "unpinned")) { snprintf(out, size, "UNPINNED: not a Hydra release binary; approve only if you trust where it came from"); *tone = TV_WARNING; }
    else copy_text(out, size, value);
}

static void plan_row(struct pen *p, const struct setup_row *row, const char *label) {
    char value[600], generic[64], *c;
    enum tv_style tone = TV_BASE;
    size_t length = strlen(row->key);
    copy_text(value, sizeof(value), !strcmp(row->value, "true") ? "yes" : !strcmp(row->value, "false") ? "no" : row->value);
    if (length >= 6 && !strcmp(row->key + length - 6, "source")) source_text(row->value, value, sizeof(value), &tone);
    if (!strcmp(row->key, "fingerprint") || !strcmp(row->key, "command")) tone = TV_STRONG;
    if (!label) {
        copy_text(generic, sizeof(generic), row->key);
        for (c = generic; *c; c++) if (*c == '_' || *c == '.') *c = ' ';
        generic[0] = (char)toupper((unsigned char)generic[0]);
        label = generic;
    }
    pen_pair(p, label, value, tone);
}

static void plan_rows(struct pen *p, const struct setup_envelope *e) {
    bool shown[SETUP_ROW_MAX] = {false};
    size_t i, j;
    for (j = 0; j < sizeof(plan_labels) / sizeof(plan_labels[0]); j++)
        for (i = 0; i < e->plan_count; i++)
            if (!shown[i] && !strcmp(e->plan[i].key, plan_labels[j][0])) { plan_row(p, &e->plan[i], plan_labels[j][1]); shown[i] = true; }
    for (i = 0; i < e->plan_count; i++) if (!shown[i] && !plan_skipped(e->plan[i].key)) plan_row(p, &e->plan[i], NULL);
}

static const char *plan_value(const struct setup_envelope *e, const char *key) {
    size_t i;
    for (i = 0; i < e->plan_count; i++) if (!strcmp(e->plan[i].key, key)) return e->plan[i].value;
    return "";
}

static void render_trust(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[600], prompt[96];
    snprintf(text, sizeof(text), "TRUST THE HOST KEY OF %.120s?", s->name);
    title(app, TV_STRONG, text);
    pen_begin(&p, app, 3);
    snprintf(text, sizeof(text), "Hydra has not connected to %.200s before. Compare this fingerprint with one you got from the machine itself, "
             "for example from its console or your provider's dashboard.", e->destination[0] ? e->destination : s->name);
    pen_text(&p, 0, TV_BASE, text);
    pen_text(&p, 0, TV_BASE, "");
    plan_rows(&p, e);
    pen_text(&p, 0, TV_BASE, "");
    pen_text(&p, 0, TV_BASE, "If it matches, type yes and press Enter: Hydra appends exactly this key to the file above. "
             "If this key ever changes, Hydra refuses to connect instead of replacing it.");
    app->line = app->limit - 3;
    snprintf(prompt, sizeof(prompt), "Type yes to trust this key: %s%s", s->typed, "_");
    column(app, 0, app->content_width, TV_SELECTED, prompt);
    app->line++;
    footer(app, &p, "Anything but yes leaves the key untrusted. Esc declines.", TV_MUTED);
}

static void plan_heading(const struct native_setup *s, char *out, size_t size) {
    const struct setup_envelope *e = s->current;
    if (!strcmp(e->plan_kind, "provision")) snprintf(out, size, "REVIEW: INSTALL HYDRA ON %.120s", s->name);
    else if (!strcmp(e->plan_kind, "install_agent")) snprintf(out, size, "REVIEW: INSTALL %.60s ON %.120s", plan_value(e, "agent")[0] ? plan_value(e, "agent") : "AN AGENT", s->name);
    else snprintf(out, size, "REVIEW: %.60s ON %.120s", e->plan_kind[0] ? e->plan_kind : "CHANGE", s->name);
}

static void plan_guarantees(struct pen *p, const struct setup_envelope *e) {
    if (!strcmp(e->plan_kind, "provision")) {
        pen_text(p, 0, TV_BASE, "Hydra copies a verified runtime to the machine and installs it only in the location above: "
                 "no PATH changes, no sudo, and other Hydra installs there stay untouched.");
    } else if (!strcmp(e->plan_kind, "install_agent")) {
        pen_text(p, 0, TV_BASE, "The command runs on the remote as your user, in this terminal so you can answer it; no sudo. "
                 "Hydra returns here when it finishes.");
    }
}

static void render_plan(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[600];
    plan_heading(s, text, sizeof(text));
    title(app, TV_STRONG, text);
    pen_begin(&p, app, 2);
    if (!strcmp(e->code, "approval_mismatch")) pen_text(&p, 0, TV_WARNING, "The plan changed since it was approved, so nothing ran. This is the new plan.");
    plan_rows(&p, e);
    pen_text(&p, 0, TV_BASE, "");
    plan_guarantees(&p, e);
    snprintf(text, sizeof(text), "Plan SHA-256 %.16s... y approves exactly this plan; if it changes, Hydra asks again.", e->plan_sha256);
    pen_text(&p, 0, TV_MUTED, text);
    footer(app, &p, "y approve   n or Esc decline (nothing changes)", TV_STRONG);
}

/* ---- Problems ---- */

/* The CLI's own ssh-keygen -R command from its recovery text, or one built
 * from the reported host and file. Shown for the user to run; never run. */
static void removal_command(const struct native_setup *s, char *out, size_t size) {
    const struct setup_envelope *e = s->current;
    const char *host = e->key_host[0] ? e->key_host : strrchr(e->destination, '@') ? strrchr(e->destination, '@') + 1 : e->destination;
    const char *start = strstr(e->recovery, "ssh-keygen -R"), *end;
    if (start) {
        end = strstr(start, ", then");
        snprintf(out, size, "%.*s", (int)(end ? (size_t)(end - start) : strlen(start)), start);
        return;
    }
    snprintf(out, size, "ssh-keygen -R %.200s%s%.300s", host, e->key_file[0] ? " -f " : "", e->key_file);
}

static void render_key_changed(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct setup_explanation x;
    struct pen p;
    char command[800];
    setup_explain("host_key_changed", &x);
    title(app, TV_WARNING, "THE HOST KEY CHANGED: HYDRA WILL NOT CONNECT");
    pen_begin(&p, app, 2);
    pen_text(&p, 0, TV_BASE, x.body);
    pen_text(&p, 0, TV_BASE, "");
    if (e->presented[0]) pen_pair(&p, "Presented key", e->presented, TV_STRONG);
    if (e->key_file[0]) pen_pair(&p, "Old key in", e->key_file, TV_BASE);
    pen_text(&p, 0, TV_BASE, "");
    pen_text(&p, 0, TV_STRONG, "What you can do");
    pen_text(&p, 2, TV_BASE, "1. Confirm the new key with whoever runs the machine, over a channel other than this connection.");
    pen_text(&p, 2, TV_BASE, "2. Only if the change is expected, remove the old entry yourself:");
    removal_command(s, command, sizeof(command));
    pen_text(&p, 6, TV_STRONG, command);
    pen_text(&p, 2, TV_BASE, "3. Press Enter to check again; Hydra then shows the new key for your review.");
    if (e->message[0]) { pen_text(&p, 0, TV_BASE, ""); snprintf(command, sizeof(command), "Hydra says: %s", e->message); pen_text(&p, 0, TV_MUTED, command); }
    footer(app, &p, "Hydra never accepts a changed key; there is no option to do so here.", TV_MUTED);
}

static bool blocking(const struct setup_envelope *e) {
    size_t i;
    for (i = 0; i < e->requirement_count; i++) if (e->requirements[i].blocking && strcmp(e->requirements[i].status, "ok")) return true;
    return false;
}

static void requirement_row(struct app *app, struct pen *p, const struct setup_requirement *r) {
    const char *status = r->blocking && strcmp(r->status, "ok") ? "BLOCKS" : r->status;
    enum tv_style tone = !strcmp(r->status, "ok") ? TV_SUCCESS : TV_WARNING;
    char label[96], fix[300];
    bool narrow = app->content_width < 70;
    struct hanging h = {2, narrow ? 8 : 25, label, tone};
    snprintf(label, sizeof(label), narrow ? "%.7s" : "%-8s %.15s", status, r->name);
    if (narrow) { pen_text(p, 2, tone, r->name); h.label = status; }
    pen_hanging(p, &h, r->detail[0] ? r->detail : "-", narrow ? TV_MUTED : TV_BASE);
    if (r->command[0]) { snprintf(fix, sizeof(fix), "fix: %s", r->command); h.label = ""; pen_hanging(p, &h, fix, TV_STRONG); }
}

static void render_preflight(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[300];
    size_t i;
    bool blocked = blocking(e) || !strcmp(e->code, "prerequisite_missing");
    snprintf(text, sizeof(text), "%s ON %.120s", blocked ? "MISSING REQUIREMENTS" : "REQUIREMENTS", s->name);
    title(app, blocked ? TV_WARNING : TV_STRONG, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, TV_BASE, blocked ? "Setup cannot continue until the items marked BLOCKS are installed. Hydra never uses sudo: "
                                       "run the suggested commands on the machine yourself, then check again."
                                     : "Nothing blocks setup. Warnings are worth fixing but do not stop it.");
    pen_text(&p, 0, TV_BASE, "");
    pen_text(&p, 2, TV_MUTED, app->content_width < 70 ? "STATUS  REQUIREMENT" : "STATUS   REQUIREMENT     DETAIL");
    for (i = 0; i < e->requirement_count; i++) requirement_row(app, &p, &e->requirements[i]);
    footer(app, &p, blocked ? "Enter checks again after you install them   Esc back" : "Enter continues setup   Esc back", TV_STRONG);
}

static const char *agent_status(const char *status) {
    static const char *const names[][2] = {
        {"on_path", "on PATH"}, {"found_off_path", "found outside PATH"}, {"recorded", "recorded location"},
        {"missing", "not installed"}, {"ambiguous", "several copies found"},
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(status, names[i][0])) return names[i][1];
    return status;
}

static void render_agents(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[600];
    size_t i;
    snprintf(text, sizeof(text), "AGENTS ON %.120s", s->name);
    title(app, TV_STRONG, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, TV_BASE, "Hydra looked for agent programs on the remote PATH and in common per-user install directories.");
    pen_text(&p, 0, TV_BASE, "");
    for (i = 0; i < e->agent_count; i++) {
        const struct setup_agent *a = &e->agents[i];
        snprintf(text, sizeof(text), "%-12s %-20s %s%s%s", a->name, agent_status(a->status), a->path, a->version[0] ? "  " : "", a->version);
        pen_text(&p, 2, !strcmp(a->status, "missing") || !strcmp(a->status, "ambiguous") ? TV_WARNING : TV_BASE, text);
    }
    if (!e->agent_count) pen_text(&p, 2, TV_MUTED, "No agent programs were reported.");
    pen_text(&p, 0, TV_BASE, "");
    pen_text(&p, 0, TV_MUTED, "Setup then installs and signs in to the selected agents; you approve each installer command.");
    footer(app, &p, "Enter continues setup   Esc back", TV_STRONG);
}

static void render_handoff(struct app *app, struct native_setup *s) {
    struct pen p;
    char text[1200], label[96];
    size_t i;
    bool sign_in = !strncmp(s->handoff_step, "sign_in:", 8);
    setup_step_label(s->handoff_step, label, sizeof(label));
    snprintf(text, sizeof(text), "%.90s ON %.120s", label, s->name);
    for (i = 0; text[i]; i++) text[i] = (char)toupper((unsigned char)text[i]);
    title(app, TV_STRONG, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, TV_BASE, sign_in ? "The provider's sign-in asks questions or shows a link to open in your browser. Hydra hands this "
                                       "terminal to it and comes back when it finishes. Credentials stay on the remote; Hydra copies nothing."
                                     : "The installer runs in this terminal so you can answer it. Hydra comes back when it finishes.");
    pen_text(&p, 0, TV_BASE, "");
    text[0] = '\0';
    for (i = 0; i < s->handoff_argc; i++) text_append(text, sizeof(text), "%s%s", i ? " " : "", s->handoff[i]);
    pen_pair(&p, "Command", text, TV_STRONG);
    footer(app, &p, "Enter hands over the terminal   Esc later", TV_STRONG);
}

static void render_problem(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct setup_explanation x;
    struct pen p;
    char text[800];
    setup_explain(e->code, &x);
    title(app, TV_WARNING, x.title);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, TV_BASE, x.body);
    pen_text(&p, 0, TV_BASE, "");
    if (e->message[0]) { snprintf(text, sizeof(text), "Hydra says: %s", e->message); pen_text(&p, 0, TV_BASE, text); }
    if (e->recovery[0]) { snprintf(text, sizeof(text), "Suggested: %s", e->recovery); pen_text(&p, 0, TV_BASE, text); }
    snprintf(text, sizeof(text), "code %s%sexit %d", e->code, dot(app), e->exit_status);
    pen_text(&p, 0, TV_MUTED, text);
    snprintf(text, sizeof(text), "%s   Esc back", x.action ? x.action : "Nothing to retry here");
    footer(app, &p, text, x.action ? TV_STRONG : TV_MUTED);
}

static void render_done(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[600];
    size_t i;
    snprintf(text, sizeof(text), "%.120s IS READY", s->name);
    title(app, TV_SUCCESS, text);
    pen_begin(&p, app, 2);
    snprintf(text, sizeof(text), "Hydra added %.120s (%.200s) as a remote host.", s->name, e->destination[0] ? e->destination : s->destination);
    pen_text(&p, 0, TV_BASE, text);
    pen_text(&p, 0, TV_BASE, "");
    for (i = 0; i < e->step_count; i++) step_row(app, &p, &e->steps[i], false, false);
    pen_text(&p, 0, TV_BASE, "");
    pen_text(&p, 0, TV_BASE, "Follow and start work on it with hydra fleet tui.");
    footer(app, &p, "Enter or Esc closes", TV_STRONG);
}

void native_setup_render(struct app *app) {
    struct native_setup *s = app->setup;
    switch (s->screen) {
        case SETUP_SCREEN_FORM: render_form(app, s); break;
        case SETUP_SCREEN_RUNNING: render_steps(app, s, true); break;
        case SETUP_SCREEN_TRUST_KEY: render_trust(app, s); break;
        case SETUP_SCREEN_PLAN: render_plan(app, s); break;
        case SETUP_SCREEN_KEY_CHANGED: render_key_changed(app, s); break;
        case SETUP_SCREEN_PREFLIGHT: render_preflight(app, s); break;
        case SETUP_SCREEN_AGENTS: render_agents(app, s); break;
        case SETUP_SCREEN_HANDOFF: render_handoff(app, s); break;
        case SETUP_SCREEN_ERROR: case SETUP_SCREEN_UNKNOWN: render_problem(app, s); break;
        case SETUP_SCREEN_DONE: render_done(app, s); break;
        default: render_steps(app, s, false); break;
    }
}

const char *native_setup_title(const struct app *app) {
    return app->setup->screen == SETUP_SCREEN_FORM ? "Add a host" : "Remote setup";
}

const char *native_setup_hints(const struct app *app, bool narrow) {
    static const char *const hints[][2] = {
        [SETUP_SCREEN_FORM] = {"Tab or Up/Down field  Enter start setup  Esc cancel", "Tab field  Enter start  Esc cancel"},
        [SETUP_SCREEN_RUNNING] = {"Esc keep running in the background", "Esc background"},
        [SETUP_SCREEN_STEPS] = {"Up/Down select  Enter continue or inspect the step  Esc back  q quit", "Enter continue  Esc back"},
        [SETUP_SCREEN_TRUST_KEY] = {"type yes, then Enter to trust  Esc decline", "yes + Enter trust  Esc decline"},
        [SETUP_SCREEN_PLAN] = {"y approve  n or Esc decline  Up/Down scroll", "y approve  n/Esc decline"},
        [SETUP_SCREEN_KEY_CHANGED] = {"Enter check again  Esc back  Up/Down scroll", "Enter check again  Esc back"},
        [SETUP_SCREEN_PREFLIGHT] = {"Enter check again or continue  Esc back  Up/Down scroll", "Enter continue  Esc back"},
        [SETUP_SCREEN_AGENTS] = {"Enter continue setup  Esc back  Up/Down scroll", "Enter continue  Esc back"},
        [SETUP_SCREEN_UNKNOWN] = {"Enter reconcile  Esc back  Up/Down scroll", "Enter reconcile  Esc back"},
        [SETUP_SCREEN_HANDOFF] = {"Enter hand over the terminal  Esc later", "Enter hand over  Esc later"},
        [SETUP_SCREEN_DONE] = {"Enter or Esc close  q quit", "Enter close"},
    };
    enum setup_screen screen = app->setup->screen;
    struct setup_explanation x;
    if (screen == SETUP_SCREEN_ERROR) {
        setup_explain(app->setup->current->code, &x);
        snprintf(app->setup->hint, sizeof(app->setup->hint), "%s%sEsc back  Up/Down scroll", x.action ? x.action : "", x.action ? "  " : "");
        return app->setup->hint;
    }
    if (screen <= SETUP_SCREEN_NONE || screen > SETUP_SCREEN_DONE) screen = SETUP_SCREEN_STEPS;
    return hints[screen][narrow ? 1 : 0];
}
