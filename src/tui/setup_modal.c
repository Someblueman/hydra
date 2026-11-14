#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* The setup flow's modals over Hosts: the add-host form, the host key (a
 * form whose one field accepts only yes) and a plan to approve (a keyed
 * confirmation: y approves, n or Esc declines, Enter answers nothing). Every
 * value comes from the CLI envelope; the fixed sentences say what approving
 * does. The components live in native_setup between frames; their titles
 * and bodies are written again for each frame from the current envelope. */

bool native_setup_modal_open(const struct app *app) {
    const struct native_setup *s = app->setup;
    return s && s->open && (s->screen == SETUP_SCREEN_FORM || s->screen == SETUP_SCREEN_TRUST_KEY || s->screen == SETUP_SCREEN_PLAN);
}

/* ---- the plan's rows as text -------------------------------------------- */

/* Plan members in display order with their labels (provision: binary.*,
 * platform, prefix, changes; installer: command, effect, source, docs). */
static const char *const plan_labels[][2] = {
    {"fingerprint", "Fingerprint"}, {"key_type", "Key type"}, {"host", "Host"}, {"hostname", "Host"}, {"port", "Port"},
    {"host_key_alias", "Host key alias"}, {"known_hosts", "Will be added to"}, {"platform", "Platform"},
    {"platform.os", "Operating system"}, {"platform.arch", "CPU"}, {"version", "Hydra version"},
    {"hydra_version", "Hydra version"}, {"binary.trust", "Trust"}, {"binary.source", "Binary source"},
    {"binary.name", "Binary"}, {"binary.sha256", "Binary SHA-256"}, {"binary_sha256", "Binary SHA-256"},
    {"package_sha256", "Package SHA-256"}, {"prefix", "Install location"}, {"changes", "Changes"}, {"agent", "Agent"},
    {"executable", "Executable"}, {"command", "Command"}, {"effect", "Runs"}, {"url", "Installer URL"},
    {"source", "Recipe"}, {"docs_url", "Provider docs"}, {"verified_on", "Recipe checked"}, {"recipe_source", "Recipe"},
    {"requires", "Requires"}, {"expected_dirs", "Installs into"}, {"peer_fingerprint", "Host key"},
    {"hashed", "Hashed entry"}, {"change", "Change"}, {"notes", "Notes"}, {"effects", "Effects"},
    {"record", "Setup record"}, {"remote", "On the remote"}, {"remote_left", "Left on remote"},
    {"local_left", "Left here"},
};

static bool plan_skipped(const char *key) { return !strcmp(key, "name") || !strcmp(key, "destination"); }

/* Plain-language values for trust and origin members; others are shown as is. */
static const char *const plan_values[][3] = {
    {"binary.trust", "pinned", "pinned: its digest is recorded in this Hydra release"},
    {"binary.trust", "unpinned", "Not a release build: this helper is not a Hydra release binary; approve only if you trust where it came from"},
    {"binary.source", "release-asset", "Hydra release download, checked against the pinned digest"},
    {"binary.source", "local-helper", "this machine's own hydra-fleet helper (a development build)"},
    {"binary.source", "binary", "the file you passed with --binary"},
    {"source", "built-in", "built into this Hydra release"},
    {"source", "local", "LOCAL recipe from your $HYDRA_HOME/fleet/agent-recipes.json"},
};

static void value_text(const struct setup_row *row, char *out, size_t size) {
    size_t i;
    for (i = 0; i < sizeof(plan_values) / sizeof(plan_values[0]); i++) {
        if (strcmp(row->key, plan_values[i][0]) || strcmp(row->value, plan_values[i][1])) continue;
        copy_text(out, size, plan_values[i][2]);
        return;
    }
    if (!row->value[0] && strstr(row->key, "_left")) { copy_text(out, size, "nothing"); return; }
    copy_text(out, size, !strcmp(row->value, "true") ? "yes" : !strcmp(row->value, "false") ? "no" : row->value);
}

/* A body written for a known inside width: labelled values wrapped under
 * themselves, so the dialog's own wrapping leaves them as they are. */
struct body { char *text; size_t size; int width, label; };

/* Bytes of text that fit in width columns, preferring to break at a space. */
static size_t fit(const char *text, int width) {
    size_t i = 0, cut = 0, length = strlen(text);
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

static void body_text(struct body *b, const char *text) { text_append(b->text, b->size, "%s\n", text); }

static void body_pair(struct body *b, const char *label, const char *value) {
    int width = b->width - b->label;
    const char *rest = value[0] ? value : "-";
    bool first = true;
    if (width < 8) width = 8;
    do {
        size_t take = fit(rest, width);
        text_append(b->text, b->size, "%-*s%.*s\n", b->label, first ? label : "", (int)take, rest);
        first = false;
        rest += take;
        while (*rest == ' ') rest++;
    } while (*rest);
}

static void plan_row(struct body *b, const struct setup_row *row, const char *label) {
    char value[600], generic[64], *c;
    value_text(row, value, sizeof(value));
    if (!label) {
        copy_text(generic, sizeof(generic), row->key);
        for (c = generic; *c; c++) if (*c == '_' || *c == '.') *c = ' ';
        generic[0] = (char)toupper((unsigned char)generic[0]);
        label = generic;
    }
    body_pair(b, label, value);
}

static void plan_rows(struct body *b, const struct setup_envelope *e) {
    bool shown[SETUP_ROW_MAX] = {false};
    size_t i, j;
    for (j = 0; j < sizeof(plan_labels) / sizeof(plan_labels[0]); j++)
        for (i = 0; i < e->plan_count; i++)
            if (!shown[i] && !strcmp(e->plan[i].key, plan_labels[j][0])) { plan_row(b, &e->plan[i], plan_labels[j][1]); shown[i] = true; }
    for (i = 0; i < e->plan_count; i++) if (!shown[i] && !plan_skipped(e->plan[i].key)) plan_row(b, &e->plan[i], NULL);
}

static const char *plan_value(const struct setup_envelope *e, const char *key) {
    size_t i;
    for (i = 0; i < e->plan_count; i++) if (!strcmp(e->plan[i].key, key)) return e->plan[i].value;
    return "";
}

static const char *or_text(const char *value, const char *fallback) { return value[0] ? value : fallback; }

/* ---- titles and bodies ----------------------------------------------------- */

static void plan_title(const struct native_setup *s, char *out, size_t size) {
    const struct setup_envelope *e = s->current;
    if (!strcmp(e->plan_kind, "remove")) snprintf(out, size, "Remove the setup record of %.120s?", s->name);
    else if (!strcmp(e->plan_kind, "provision")) snprintf(out, size, "Install Hydra on %.120s", s->name);
    else if (!strcmp(e->plan_kind, "install_agent")) snprintf(out, size, "Install %.60s on %.120s", or_text(plan_value(e, "agent"), "an agent"), s->name);
    else snprintf(out, size, "Review: %.60s on %.120s", or_text(e->plan_kind, "change"), s->name);
}

static void plan_guarantees(struct body *b, const struct setup_envelope *e) {
    if (!strcmp(e->plan_kind, "provision")) {
        body_text(b, "Hydra copies a verified runtime to the machine and installs it only in the location above: "
                  "no PATH changes, no sudo, and other Hydra installs there stay untouched.");
    } else if (!strcmp(e->plan_kind, "remove")) {
        body_text(b, "Only this machine's record of the setup is deleted. Hydra does not connect to the remote, and "
                  "nothing listed above is removed or undone.");
    } else if (!strcmp(e->plan_kind, "install_agent")) {
        body_text(b, "The command runs on the remote as your user, in this terminal so you can answer it; no sudo. "
                  "Hydra returns here as soon as it succeeds; after a failure it waits for Enter so you can read the output.");
    }
}

static void plan_body(struct body *b, const struct setup_envelope *e) {
    char text[200];
    if (!strcmp(e->code, "approval_mismatch")) body_text(b, "The plan changed since it was approved, so nothing ran. This is the new plan.\n");
    plan_rows(b, e);
    body_text(b, "");
    plan_guarantees(b, e);
    snprintf(text, sizeof(text), "\nPlan SHA-256 %.16s... y approves exactly this plan; if it changes, Hydra asks again.", e->plan_sha256);
    body_text(b, text);
}

/* Sized so the whole explanation fits at 80x24. */
static void trust_body(struct body *b, const struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    char text[600];
    snprintf(text, sizeof(text), "Hydra has not connected to %.200s before. Compare this fingerprint with one read on the "
             "machine itself.\n", e->destination[0] ? e->destination : s->name);
    body_text(b, text);
    plan_rows(b, e);
    body_text(b, "\nIf they match, type yes: Hydra appends exactly one line to the file above. If this key ever changes, "
              "Hydra refuses to connect rather than replace it.");
}

static void form_body(struct body *b, const struct native_setup *s) {
    if (s->problem[0]) { body_text(b, s->problem); body_text(b, ""); }
    if (s->editing[0]) {
        body_text(b, "Nothing has changed on this remote yet. With the same name, Hydra removes the old setup record "
                  "(you approve that first) and starts again with these values.");
        return;
    }
    body_text(b, "Hydra connects with your own SSH setup. It checks the machine and asks before every change; nothing "
              "changes on the remote until you approve it. Next: Hydra reads the machine's SSH host key and shows it "
              "for you to confirm.");
}

/* The modal's width for this terminal, and the inside its body wraps to. */
static int modal_width(const struct app *app, const struct native_setup *s) {
    int most = s->screen == SETUP_SCREEN_FORM ? 66 : 76, width = app->cols - 4;
    return width > most ? most : width < 30 ? 30 : width;
}

static void modal_texts(const struct app *app, struct native_setup *s) {
    struct setup_modal *m = &s->modal;
    struct body b = {m->body, sizeof(m->body), modal_width(app, s) - 6, 0};
    b.label = b.width < 66 ? 14 : 18;
    m->body[0] = '\0';
    if (s->screen == SETUP_SCREEN_FORM) {
        if (s->editing[0]) snprintf(m->title, sizeof(m->title), "Edit the setup of %.120s", s->editing);
        else copy_text(m->title, sizeof(m->title), "Add a host");
        form_body(&b, s);
    } else if (s->screen == SETUP_SCREEN_TRUST_KEY) {
        snprintf(m->title, sizeof(m->title), "Trust the host key of %.120s?", s->name);
        trust_body(&b, s);
    } else {
        plan_title(s, m->title, sizeof(m->title));
        plan_body(&b, s->current);
    }
    /* The last line break would be a blank row. */
    if (m->body[0] && m->body[strlen(m->body) - 1] == '\n') m->body[strlen(m->body) - 1] = '\0';
}

/* ---- the components ---------------------------------------------------------- */

static void field(struct setup_modal *m, size_t index, char *buffer, size_t size, const char *label, const char *hint) {
    kt_textinput_init(&m->inputs[index], buffer, size);
    m->fields[index].kind = KT_FIELD_TEXT;
    m->fields[index].label = label; m->fields[index].hint = hint;
    m->fields[index].input = &m->inputs[index];
}

static void form_build(struct native_setup *s) {
    struct setup_modal *m = &s->modal;
    memset(m->fields, 0, sizeof(m->fields));
    field(m, SETUP_FIELD_DESTINATION, s->fields[SETUP_FIELD_DESTINATION].text, sizeof(s->fields[0].text), "SSH destination",
          "user@host, or a Host from ~/.ssh/config");
    field(m, SETUP_FIELD_NAME, s->fields[SETUP_FIELD_NAME].text, sizeof(s->fields[0].text), "Name in Hydra",
          "empty: the host name is used");
    field(m, SETUP_FIELD_CONFIG, s->fields[SETUP_FIELD_CONFIG].text, sizeof(s->fields[0].text), "SSH config file",
          "absolute path or ~/...; empty: your usual SSH config");
    kt_form_init(&m->form, m->title, m->fields, SETUP_FORM_FIELDS);
    m->form.body = m->body;
    m->form.keys[KT_FORM_SUBMIT].label = "check";
    m->form.keys[KT_FORM_NEXT].label = "next";
    kt_form_focus(&m->form, (size_t)s->focus);
}

static bool yes_typed(void *context, struct kt_form *form) {
    (void)context;
    if (!strcmp(kt_textinput_value(form->fields[0].input), "yes")) return true;
    snprintf(form->fields[0].error, sizeof(form->fields[0].error), "Type yes (the whole word) to trust this key, or press Esc to decline");
    return false;
}

static void trust_build(struct native_setup *s) {
    struct setup_modal *m = &s->modal;
    memset(m->fields, 0, sizeof(m->fields));
    field(m, 0, s->typed, sizeof(s->typed), "Type yes to trust this key", "Anything but yes leaves the key untrusted. Esc declines.");
    kt_form_init(&m->form, m->title, m->fields, 1);
    m->form.body = m->body;
    m->form.validate = yes_typed;
    m->form.keys[KT_FORM_SUBMIT].label = "trust";
    m->form.keys[KT_FORM_NEXT].priority = -1;   /* one field: nothing to move to */
    m->form.keys[KT_FORM_CANCEL].label = "decline";
}

static void plan_build(struct native_setup *s) {
    struct setup_modal *m = &s->modal;
    bool removal = !strcmp(s->current->plan_kind, "remove");
    kt_confirm_init(&m->question, m->title, m->body, removal ? "Remove the record" : "Approve", removal ? "Keep it" : "Decline", removal);
    m->question.keyed = true;
    m->question.focus = m->question.cancel;
    m->question.keys[KT_CONFIRM_CANCEL].label = removal ? "keep" : "decline";
}

void setup_modal_sync(const struct app *app, struct native_setup *s) {
    struct setup_modal *m = &s->modal;
    bool removal = !strcmp(s->current->plan_kind, "remove");
    modal_texts(app, s);
    if (m->screen == s->screen && (s->screen != SETUP_SCREEN_PLAN || m->removal == removal)) return;
    m->screen = s->screen; m->removal = removal;
    if (s->screen == SETUP_SCREEN_FORM) form_build(s);
    else if (s->screen == SETUP_SCREEN_TRUST_KEY) trust_build(s);
    else if (s->screen == SETUP_SCREEN_PLAN) plan_build(s);
}

/* Drawn over the whole frame, which is dimmed behind it. A plan's scroll is
 * the flow's own, so its body reads like any other screen of the flow. */
void native_setup_modal(struct app *app) {
    struct native_setup *s = app->setup;
    struct setup_modal *m = &s->modal;
    struct kt_modals stack;
    struct kt_modal modal;
    struct kt_rect whole = {0, 0, app->frame.width, app->frame.height};
    bool plan = s->screen == SETUP_SCREEN_PLAN;
    if (!app->frame.cells || !native_setup_modal_open(app)) return;
    setup_modal_sync(app, s);
    if (plan) m->question.scroll = s->scroll;
    modal = plan ? kt_confirm_modal(&m->question, 1, modal_width(app, s)) : kt_form_modal(&m->form, 1, modal_width(app, s));
    kt_modals_init(&stack);
    if (!kt_modals_push(&stack, &modal)) return;
    kt_modals_place(&stack, whole);
    if (plan) { s->scroll = m->question.scroll; s->more = kt_confirm_more(&m->question); }
    else s->more = false;
    kt_modals_view(&stack, &app->frame, whole);
}
