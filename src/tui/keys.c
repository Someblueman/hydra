#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* The binding table: every configurable command, its name in the keys file
 * and its short label. Chords come from default_keys, written in the syntax
 * of that file. Rows follow enum key_action; hints list them in this order. */
#define KEYS_FILE_LIMIT (64U * 1024U)

#define KEY_ROW(id, key_scope, key_name, text) [id] = {.name = key_name, .label = text, .action = id, .scope = key_scope}
static const struct kt_binding key_rows[KEY_ACTIONS] = {
    KEY_ROW(KEY_TERMINAL_LEAVE, KEY_SCOPE_TERMINAL, "terminal.leave", "leave input"),
    KEY_ROW(KEY_TERMINAL_CLOSE, KEY_SCOPE_TERMINAL, "terminal.close", "close view"),
    KEY_ROW(KEY_TERMINAL_NEXT, KEY_SCOPE_TERMINAL, "terminal.next", "next agent"),
    KEY_ROW(KEY_TERMINAL_RECONNECT, KEY_SCOPE_TERMINAL, "terminal.reconnect", "reconnect"),
    KEY_ROW(KEY_TERMINAL_HISTORY, KEY_SCOPE_TERMINAL, "terminal.history", "scroll"),
    KEY_ROW(KEY_TERMINAL_LIVE, KEY_SCOPE_TERMINAL, "terminal.live", "live output"),
    KEY_ROW(KEY_APP_QUIT, KEY_SCOPE_TERMINAL, "app.quit", "quit"),
    KEY_ROW(KEY_VIEW_STATISTICS, KEY_SCOPE_TERMINAL, "view.statistics", "statistics"),
    KEY_ROW(KEY_LAYOUT_CONVERSATION, KEY_SCOPE_TERMINAL, "workspace.layout.conversation", "conversation"),
    KEY_ROW(KEY_LAYOUT_PLAN, KEY_SCOPE_TERMINAL, "workspace.layout.plan", "plan"),
    KEY_ROW(KEY_LAYOUT_MONITOR, KEY_SCOPE_TERMINAL, "workspace.layout.monitor", "monitor"),
    KEY_ROW(KEY_WORKSPACE_ZOOM, KEY_SCOPE_TERMINAL, "workspace.zoom", "zoom"),
    KEY_ROW(KEY_WORKSPACE_SPLIT, KEY_SCOPE_TERMINAL, "workspace.split", "two agents"),
    KEY_ROW(KEY_TERMINAL_LITERAL, KEY_SCOPE_TERMINAL, "terminal.literal", "send that key to the agent"),
};

/* Ctrl-] alone leaves agent input; every other command follows Ctrl-\.
 * Neither is tmux's prefix, so the tmux an agent runs in keeps its own, and
 * the terminal.literal chords send either key on to the agent. */
static const char default_keys[] =
    "terminal.leave = ctrl+], ctrl+\\ tab\n"
    "terminal.close = ctrl+\\ x\n"
    "terminal.next = ctrl+\\ n\n"
    "terminal.reconnect = ctrl+\\ r\n"
    "terminal.history = ctrl+\\ [\n"
    "terminal.live = ctrl+\\ ]\n"
    "app.quit = ctrl+\\ q\n"
    "view.statistics = ctrl+\\ D\n"
    "workspace.layout.conversation = ctrl+\\ A\n"
    "workspace.layout.plan = ctrl+\\ B\n"
    "workspace.layout.monitor = ctrl+\\ C\n"
    "workspace.zoom = ctrl+\\ z\n"
    "workspace.split = ctrl+\\ S\n"
    "terminal.literal = ctrl+\\ ctrl+\\, ctrl+\\ ctrl+]\n";

/* Fills rows with every action and its default chords. */
static void default_bindings(struct kt_binding rows[KEY_ACTIONS]) {
    struct kt_keymap map = {rows, KEY_ACTIONS};
    char error[TEXT];
    size_t i;
    memcpy(rows, key_rows, sizeof(key_rows));
    (void)kt_keymap_configure(&map, default_keys, sizeof(default_keys) - 1U, error, sizeof(error));
    for (i = 0; i < KEY_ACTIONS; i++) rows[i].line = 0;
}

static void keys_defaults(struct native_keys *k) {
    default_bindings(k->bindings);
    k->map.bindings = k->bindings; k->map.count = KEY_ACTIONS;
}

/* Without a chord for terminal.leave nothing typed would ever return input to
 * Hydra, so a keys file cannot unbind it: false after restoring its default. */
static bool keep_leave(struct native_keys *k) {
    struct kt_binding rows[KEY_ACTIONS];
    if (k->bindings[KEY_TERMINAL_LEAVE].chord_count) return true;
    default_bindings(rows);
    k->bindings[KEY_TERMINAL_LEAVE] = rows[KEY_TERMINAL_LEAVE];
    return false;
}

struct native_keys *native_keys(struct app *app) {
    struct native_keys *k = &app->keys;
    if (!k->ready) {
        memset(k, 0, sizeof(*k));
        keys_defaults(k);
        k->ready = true;
    }
    k->map.bindings = k->bindings;
    return k;
}

/* HYDRA_TUI_KEYS names the file outright; otherwise it lives in Hydra's home. */
static bool keys_path(char *out, size_t size, bool *named) {
    const char *file = getenv("HYDRA_TUI_KEYS"), *home = getenv("HYDRA_HOME"), *user = getenv("HOME");
    int n;
    *named = file && file[0];
    out[0] = '\0';
    if (*named) n = snprintf(out, size, "%s", file);
    else if (home && home[0]) n = snprintf(out, size, "%s/tui/keys.conf", home);
    else if (user && user[0]) n = snprintf(out, size, "%s/.hydra/tui/keys.conf", user);
    else return false;
    if (n > 0 && (size_t)n < size) return true;
    out[0] = '\0';
    return false;
}

/* Records why the keys file was not applied as written, naming it. The file
 * name and reason come first so a narrow status line still shows them. */
static void keys_problem(struct native_keys *k, const char *reason, const char *outcome) {
    const char *name = strrchr(k->path, '/');
    name = name ? name + 1 : k->path;
    if (snprintf(k->problem, sizeof(k->problem), "%s %s; %s (%s)", name, reason, outcome, k->path) >= (int)sizeof(k->problem))
        (void)snprintf(k->problem, sizeof(k->problem), "%.40s %.170s; %.30s", name, reason, outcome);
}

/* Reads the whole file; NULL with a reason, or with none when it is absent. */
static char *keys_read(const char *path, size_t *length, char *reason, size_t size) {
    FILE *in = fopen(path, "rb");
    char *text;
    reason[0] = '\0';
    if (!in) {
        if (errno != ENOENT) snprintf(reason, size, "cannot be read: %s", strerror(errno));
        return NULL;
    }
    text = malloc(KEYS_FILE_LIMIT + 1U);
    *length = text ? fread(text, 1, KEYS_FILE_LIMIT + 1U, in) : 0;
    if (!text || ferror(in)) snprintf(reason, size, "cannot be read");
    else if (*length > KEYS_FILE_LIMIT) snprintf(reason, size, "is larger than 64 KiB");
    (void)fclose(in);
    if (!reason[0]) return text;
    free(text);
    return NULL;
}

void native_keys_load(struct app *app, bool default_file) {
    struct native_keys *k = native_keys(app);
    char reason[TEXT], conflict[TEXT + 32], *text;
    size_t length = 0;
    bool named;
    k->problem[0] = '\0';
    if (!keys_path(k->path, sizeof(k->path), &named) || (!named && !default_file)) return;
    text = keys_read(k->path, &length, reason, sizeof(reason));
    if (!text) {
        if (reason[0]) keys_problem(k, reason, "using the default keys");
        else if (named) keys_problem(k, "does not exist", "using the default keys");
        return;
    }
    if (!kt_keymap_configure(&k->map, text, length, reason, sizeof(reason)))
        keys_problem(k, reason, "other lines applied");
    free(text);
    if (!keep_leave(k) && !k->problem[0]) keys_problem(k, "leaves terminal.leave unbound", "its default is kept");
    if (kt_keymap_check(&k->map, reason, sizeof(reason))) return;
    keys_defaults(k);
    snprintf(conflict, sizeof(conflict), "has conflicting keys: %s", reason);
    keys_problem(k, conflict, "using the default keys");
}

void native_keys_print(struct app *app, FILE *out) {
    struct native_keys *k = native_keys(app);
    size_t i;
    fputs("# Hydra TUI keys: Hydra's own commands while you type to an attached agent.\n"
          "# One \"action = chord[, chord]\" per line; \"none\" unbinds; \"#\" starts a comment.\n"
          "# A chord is one to four keys pressed in order, such as ctrl+] or ctrl+\\ x.\n"
          "# Save as $HYDRA_HOME/tui/keys.conf (default ~/.hydra) or name a file in HYDRA_TUI_KEYS.\n", out);
    for (i = 0; i < KEY_ACTIONS; i++) {
        char line[256];
        (void)kt_binding_config(&k->bindings[i], line, sizeof(line));
        fprintf(out, "%-46s # %s\n", line, k->bindings[i].label);
    }
}

bool key_text(struct app *app, enum key_action action, char *out, size_t size) {
    const struct kt_binding *b = &native_keys(app)->bindings[action];
    if (size) out[0] = '\0';
    if (!b->chord_count) return false;
    (void)kt_chord_format(&b->chords[0], out, size);
    return true;
}

/* Appends at most the room that is left; out stays terminated. */
static void hint_append(char *out, size_t size, const char *text, size_t length) {
    size_t used = strlen(out);
    if (used + 1U >= size) return;
    if (length > size - used - 1U) length = size - used - 1U;
    memcpy(out + used, text, length);
    out[used + length] = '\0';
}

/* Appends the first chord of the action named in braces at text; false when
 * the name is unknown or the action is unbound. Returns the reference's end. */
static bool hint_reference(struct app *app, const char *text, const char *end, const char **next, char *out, size_t size) {
    const char *close = memchr(text, '}', (size_t)(end - text));
    const struct kt_binding *b;
    char name[64], chord[96];
    if (!close || (size_t)(close - text) > sizeof(name)) return false;
    memcpy(name, text + 1, (size_t)(close - text) - 1U);
    name[close - text - 1] = '\0';
    b = kt_keymap_find(&native_keys(app)->map, name);
    if (!b || !b->chord_count) return false;
    (void)kt_chord_format(&b->chords[0], chord, sizeof(chord));
    hint_append(out, size, chord, strlen(chord));
    *next = close + 1;
    return true;
}

/* Expands one segment into out; false when it names an unbound action. */
static bool hint_segment(struct app *app, const char *text, const char *end, char *out, size_t size) {
    out[0] = '\0';
    while (text < end) {
        const char *brace = memchr(text, '{', (size_t)(end - text));
        hint_append(out, size, text, (size_t)((brace ? brace : end) - text));
        if (!brace) break;
        if (!hint_reference(app, brace, end, &text, out, size)) return false;
    }
    return true;
}

/* One segment's text: its expansion, or the alternative after '~' when it
 * names an unbound action. False when the segment is left out. */
static bool hint_text(struct app *app, const char *segment, const char *end, char *out, size_t size) {
    const char *alternative = memchr(segment, '~', (size_t)(end - segment));
    if (hint_segment(app, segment, alternative ? alternative : end, out, size)) return true;
    if (!alternative) return false;
    out[0] = '\0';
    hint_append(out, size, alternative + 1, (size_t)(end - alternative) - 1U);
    return true;
}

void key_hint(struct app *app, char *out, size_t size, const char *separator, const char *template_text) {
    const char *segment = template_text;
    if (!size) return;
    out[0] = '\0';
    while (*segment) {
        const char *end = strchr(segment, '|');
        char text[512];
        if (!end) end = segment + strlen(segment);
        if (hint_text(app, segment, end, text, sizeof(text))) {
            if (out[0]) hint_append(out, size, separator, strlen(separator));
            hint_append(out, size, text, strlen(text));
        }
        segment = *end ? end + 1 : end;
    }
}

void key_hint_fit(struct app *app, char *out, size_t size, const char *separator,
                  const char *const templates[], size_t count, int width) {
    size_t i;
    if (size) out[0] = '\0';
    for (i = 0; i < count; i++) {
        key_hint(app, out, size, separator, templates[i]);
        if ((int)strlen(out) <= width) return;
    }
}

/* The keys of chord after its first skip, as a chord of their own. */
static void chord_rest(const struct kt_chord *chord, unsigned skip, struct kt_chord *rest) {
    unsigned i;
    memset(rest, 0, sizeof(*rest));
    for (i = skip; i < chord->count; i++) rest->keys[rest->count++] = chord->keys[i];
}

bool key_pending_text(struct app *app, char *out, size_t size) {
    struct native_keys *k = native_keys(app);
    const struct kt_chord *pending = &k->state.pending;
    char text[96];
    const char *join = " ";
    size_t i;
    unsigned c;
    if (!pending->count) return false;
    (void)kt_chord_format(pending, text, sizeof(text));
    snprintf(out, size, "%s %s ", text, app->ascii ? "..." : "\xe2\x80\xa6");
    for (i = 0; i < KEY_ACTIONS; i++) {
        const struct kt_binding *b = &k->bindings[i];
        for (c = 0; c < b->chord_count; c++) {
            struct kt_chord rest;
            if (!(b->scope & KEY_SCOPE_TERMINAL) || !kt_chord_extends(&b->chords[c], pending)) continue;
            chord_rest(&b->chords[c], pending->count, &rest);
            (void)kt_chord_format(&rest, text, sizeof(text));
            text_append(out, size, "%s%s %s", join, text, b->label);
            join = dot(app);
        }
    }
    return true;
}
