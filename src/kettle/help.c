#include "component_internal.h"
#include <string.h>

/* Key hints from binding tables: a one-line footer that fits its width, and
 * a full screen of every binding grouped under headings. */

#define ENTRIES 64
#define KEY_TEXT 64
#define GROUPS 16
#define COLUMN_GAP 6
#define KEY_GAP 3
#define KEY_LIMIT 12

/* One hint: a run of adjacent bindings with the same label. */
struct entry {
    const struct kt_binding *first;
    size_t span;
    int priority, key_width, label_width;
    char keys[KEY_TEXT];
};
struct entries { struct entry items[ENTRIES]; size_t count; };

static bool listed(const struct kt_binding *b, uint32_t scope) {
    return (b->scope & scope) && b->label && b->label[0] && b->chord_count > 0;
}

/* Adds a binding to the entry before it when that is its neighbour in the
 * same table with the same label. */
static bool joins(const struct entries *e, const struct kt_binding *b) {
    const struct entry *last;
    if (!e->count) return false;
    last = &e->items[e->count - 1];
    return last->first + last->span == b && !strcmp(last->first->label, b->label);
}

static void add(struct entries *e, const struct kt_binding *b) {
    struct entry *entry;
    if (joins(e, b)) {
        entry = &e->items[e->count - 1];
        entry->span++;
        if (b->priority > entry->priority) entry->priority = b->priority;
        return;
    }
    if (e->count == ENTRIES) return;
    entry = &e->items[e->count++];
    memset(entry, 0, sizeof(*entry));
    entry->first = b;
    entry->span = 1;
    entry->priority = b->priority;
}

static void collect(struct entries *e, const struct kt_help_source *sources, size_t count) {
    size_t s, i;
    e->count = 0;
    for (s = 0; s < count; s++)
        for (i = 0; i < sources[s].count; i++)
            if (listed(&sources[s].bindings[i], sources[s].scope)) add(e, &sources[s].bindings[i]);
}

/* ---- key text ----------------------------------------------------------- */

struct text { char *out; size_t capacity, used; };

static void append(struct text *t, const char *piece) {
    size_t n = strlen(piece);
    if (t->used + n >= t->capacity) return;
    memcpy(t->out + t->used, piece, n + 1);
    t->used += n;
}

static void append_chord(struct text *t, const struct kt_chord *chord) {
    char hint[48];
    (void)kt_chord_hint(chord, hint, sizeof(hint));
    append(t, hint);
}

/* Chord `c` of every binding in the entry: "a/b", or "first–last" for
 * three or more. False when a binding lacks that chord. */
static bool chord_set(struct text *t, const struct entry *e, unsigned c) {
    size_t i;
    for (i = 0; i < e->span; i++)
        if (e->first[i].chord_count <= c) return false;
    if (e->span > 2) {
        append_chord(t, &e->first[0].chords[c]);
        append(t, "\xe2\x80\x93");
        append_chord(t, &e->first[e->span - 1].chords[c]);
        return true;
    }
    for (i = 0; i < e->span; i++) {
        if (i) append(t, "/");
        append_chord(t, &e->first[i].chords[c]);
    }
    return true;
}

/* The entry's keys: the first chord of each binding, or every chord while
 * that stays within KEY_LIMIT columns, so one long alternative does not
 * widen the key column of every entry. */
static void entry_keys(struct entry *e, bool every) {
    struct text t;
    unsigned c;
    size_t first;
    t.out = e->keys; t.capacity = sizeof(e->keys); t.used = 0;
    e->keys[0] = '\0';
    (void)chord_set(&t, e, 0);
    first = t.used;
    for (c = 1; every && c < KT_BINDING_CHORDS; c++) {
        size_t mark = t.used;
        append(&t, "  ");
        if (!chord_set(&t, e, c)) { t.used = mark; e->keys[mark] = '\0'; }
    }
    if (kt_text_width(e->keys) > KEY_LIMIT) e->keys[first] = '\0';
    e->key_width = kt_text_width(e->keys);
    e->label_width = kt_text_width(e->first->label);
}

static void all_keys(struct entries *e, bool every) {
    size_t i;
    for (i = 0; i < e->count; i++) entry_keys(&e->items[i], every);
}

/* ---- the short form ----------------------------------------------------- */

struct line {
    bool shown[ENTRIES];
    int separator, more, width;
    bool dropped;
};

static int entry_width(const struct entry *e) { return e->key_width + 1 + e->label_width; }

static int line_width(const struct entries *e, const struct line *l) {
    int width = 0, items = 0;
    size_t i;
    for (i = 0; i < e->count; i++) {
        if (!l->shown[i]) continue;
        width += entry_width(&e->items[i]);
        items++;
    }
    if (l->dropped && l->more) { width += l->more; items++; }
    return width + (items > 1 ? (items - 1) * l->separator : 0);
}

/* The shown entry to drop next: the toggle itself, else the lowest
 * priority, the latest among equals. */
static size_t victim(const struct entries *e, const struct line *l, const struct kt_binding *toggle) {
    size_t i, worst = KT_NONE;
    for (i = 0; i < e->count; i++) {
        if (!l->shown[i]) continue;
        if (toggle && e->items[i].first == toggle) return i;
        if (worst == KT_NONE || e->items[i].priority <= e->items[worst].priority) worst = i;
    }
    return worst;
}

static void fit_line(const struct entries *e, struct line *l, const struct kt_binding *toggle) {
    size_t i;
    for (i = 0; i < e->count; i++) l->shown[i] = e->items[i].priority >= 0;
    l->dropped = false;
    while (line_width(e, l) > l->width) {
        size_t drop = victim(e, l, toggle);
        if (drop == KT_NONE) break;
        l->shown[drop] = false;
        l->dropped = true;
    }
}

/* One run of the line; spans hold no text of their own. */
struct runs { struct kt_span spans[ENTRIES * 4 + 4]; size_t count; };

static void run(struct runs *r, const char *text, struct kt_style style) {
    if (r->count == sizeof(r->spans) / sizeof(r->spans[0])) return;
    r->spans[r->count].text = text;
    r->spans[r->count++].style = style;
}

static void run_entry(struct runs *r, const char *keys, const char *label, const char *separator,
                      const struct kt_help_style *style) {
    if (r->count) run(r, separator, style->separator);
    run(r, keys, style->key);
    run(r, " ", style->label);
    run(r, label, style->label);
}

int kt_help_line(struct kt_canvas *canvas, struct kt_rect area, const struct kt_help_source *sources, size_t count,
                 const struct kt_binding *toggle, const char *more, const struct kt_help_style *style) {
    struct entries e;
    struct line l;
    struct runs r;
    char toggle_keys[48] = "";
    const char *separator = canvas->unicode ? " \xe2\x80\xa2 " : " | ";
    size_t i;
    collect(&e, sources, count);
    all_keys(&e, false);
    if (toggle && toggle->chord_count) (void)kt_chord_hint(&toggle->chords[0], toggle_keys, sizeof(toggle_keys));
    if (!more) more = "more";
    l.width = area.width;
    l.separator = kt_text_width(separator);
    l.more = toggle_keys[0] ? kt_text_width(toggle_keys) + 1 + kt_text_width(more) : 0;
    fit_line(&e, &l, toggle_keys[0] ? toggle : NULL);
    r.count = 0;
    for (i = 0; i < e.count; i++)
        if (l.shown[i]) run_entry(&r, e.items[i].keys, e.items[i].first->label, separator, style);
    if (l.dropped && l.more && line_width(&e, &l) <= l.width) run_entry(&r, toggle_keys, more, separator, style);
    return r.count ? kt_spans(canvas, area, r.spans, r.count) : 0;
}

/* ---- the full form ------------------------------------------------------ */

struct groups { const char *names[GROUPS]; size_t count; };

static bool same_group(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }

static void group_names(const struct entries *e, struct groups *g) {
    size_t i, j;
    g->count = 0;
    for (i = 0; i < e->count; i++) {
        const char *name = e->items[i].first->group;
        for (j = 0; j < g->count && !same_group(g->names[j], name); j++) {}
        if (j == g->count && g->count < GROUPS) g->names[g->count++] = name;
    }
}

static size_t group_size(const struct entries *e, const char *name) {
    size_t i, n = 0;
    for (i = 0; i < e->count; i++) n += same_group(e->items[i].first->group, name);
    return n;
}

/* Column geometry shared by every group, so key columns line up. */
struct columns { int key, label, width, count; };

static struct columns measure_columns(const struct entries *e, int width) {
    struct columns c = {0, 0, 0, 1};
    size_t i;
    for (i = 0; i < e->count; i++) {
        if (e->items[i].key_width > c.key) c.key = e->items[i].key_width;
        if (e->items[i].label_width > c.label) c.label = e->items[i].label_width;
    }
    c.width = c.key + KEY_GAP + c.label;
    if (c.width + COLUMN_GAP > 0 && width > c.width) c.count = (width + COLUMN_GAP) / (c.width + COLUMN_GAP);
    if (c.count < 1) c.count = 1;
    return c;
}

static int group_rows(size_t entries, int columns) { return (int)((entries + (size_t)columns - 1) / (size_t)columns); }

struct full { struct entries e; struct groups g; struct columns c; };

static void full_layout(struct full *f, const struct kt_help *help, int width) {
    collect(&f->e, help->sources, help->count);
    all_keys(&f->e, true);
    group_names(&f->e, &f->g);
    f->c = measure_columns(&f->e, width);
}

static int full_height(const struct full *f) {
    int rows = 0;
    size_t g;
    for (g = 0; g < f->g.count; g++) {
        if (g) rows++;
        if (f->g.names[g]) rows++;
        rows += group_rows(group_size(&f->e, f->g.names[g]), f->c.count);
    }
    return rows;
}

int kt_help_full_height(const struct kt_help *help, int width) {
    struct full f;
    full_layout(&f, help, width);
    return full_height(&f);
}

int kt_help_full_width(const struct kt_help *help, int columns) {
    struct full f;
    full_layout(&f, help, 0);
    if (columns < 1) columns = 1;
    return columns * f.c.width + (columns - 1) * COLUMN_GAP;
}

static void draw_entry(struct kt_canvas *canvas, struct kt_rect cell, const struct entry *e, const struct columns *c,
                       const struct kt_help_style *style) {
    struct kt_rect label = cell;
    (void)kt_text_aligned(canvas, (struct kt_rect){cell.x, cell.y, c->key, 1}, e->keys, KT_ALIGN_LEFT, style->key);
    label.x += c->key + KEY_GAP;
    label.width -= c->key + KEY_GAP;
    (void)kt_text_aligned(canvas, label, e->first->label, KT_ALIGN_LEFT, style->label);
}

/* One group's entries, top to bottom and then across; returns rows used. */
static int draw_group(struct kt_canvas *canvas, struct kt_rect area, const struct full *f, const char *name,
                      const struct kt_help_style *style) {
    int rows = group_rows(group_size(&f->e, name), f->c.count), n = 0;
    size_t i;
    for (i = 0; i < f->e.count; i++) {
        struct kt_rect cell;
        if (!same_group(f->e.items[i].first->group, name)) continue;
        cell.x = area.x + (n / rows) * (f->c.width + COLUMN_GAP);
        cell.y = area.y + n % rows;
        cell.width = f->c.width;
        cell.height = 1;
        if (cell.y < area.y + area.height) draw_entry(canvas, cell, &f->e.items[i], &f->c, style);
        n++;
    }
    return rows;
}

int kt_help_full_view(const struct kt_help *help, struct kt_canvas *canvas, struct kt_rect area) {
    struct full f;
    int y = area.y;
    size_t g;
    full_layout(&f, help, area.width);
    for (g = 0; g < f.g.count; g++) {
        if (g) y++;
        if (f.g.names[g])
            (void)kt_text_aligned(canvas, (struct kt_rect){area.x, y++, area.width, 1}, f.g.names[g], KT_ALIGN_LEFT,
                                  help->style.heading);
        y += draw_group(canvas, (struct kt_rect){area.x, y, area.width, area.y + area.height - y}, &f, f.g.names[g],
                        &help->style);
    }
    return y - area.y;
}

/* ---- the component ------------------------------------------------------ */

enum { SCOPE_CLOSED = 1, SCOPE_OPEN = 2 };

static const struct kt_binding help_keys[KT_HELP_ACTIONS] = {
    KT_UI_ROW(KT_HELP_TOGGLE, "help.toggle", "help", SCOPE_CLOSED | SCOPE_OPEN, NULL, 0, 1, KT_UI_CHORD('?', 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_HELP_CLOSE, "help.close", "close", SCOPE_OPEN, NULL, 0, 1, KT_UI_CHORD(KT_KEY_ESCAPE, 0),
              KT_UI_NO_CHORD),
};

void kt_help_init(struct kt_help *help) {
    memset(help, 0, sizeof(*help));
    help->more = "more";
    help->style = kt_ui_hint_style();
    memcpy(help->keys, help_keys, sizeof(help_keys));
}

bool kt_help_set(struct kt_help *help, const struct kt_help_source *sources, size_t count) {
    help->count = count < KT_HELP_SOURCES ? count : KT_HELP_SOURCES;
    if (help->count) memcpy(help->sources, sources, help->count * sizeof(sources[0]));
    return count <= KT_HELP_SOURCES;
}

uint32_t kt_help_scope(const struct kt_help *help) { return help->full ? SCOPE_OPEN : SCOPE_CLOSED; }

struct kt_keymap kt_help_keymap(struct kt_help *help) {
    struct kt_keymap map;
    map.bindings = help->keys;
    map.count = KT_HELP_ACTIONS;
    return map;
}

enum kt_help_result kt_help_update(struct kt_help *help, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action = kt_ui_action(help->keys, KT_HELP_ACTIONS, &help->chord, kt_help_scope(help), msg, &consumed);
    (void)cmds;
    if (action == KT_HELP_TOGGLE || (action == KT_HELP_CLOSE && help->full)) {
        help->full = action == KT_HELP_TOGGLE ? !help->full : false;
        return help->full ? KT_HELP_OPENED : KT_HELP_CLOSED;
    }
    return consumed ? KT_HELP_HANDLED : KT_HELP_IGNORED;
}

int kt_help_view(const struct kt_help *help, struct kt_canvas *canvas, struct kt_rect area) {
    return kt_help_line(canvas, area, help->sources, help->count, &help->keys[KT_HELP_TOGGLE], help->more,
                        &help->style);
}
