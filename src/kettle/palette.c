#include "component_internal.h"
#include <string.h>

/* A filterable dialog of the application's bindings. A binding is named by
 * its position among the listed ones, counted across the sources. */

#define ALL 1U

static const struct kt_binding palette_keys[KT_PALETTE_ACTIONS] = {
    KT_UI_ROW(KT_PALETTE_UP, "palette.up", "select", ALL, NULL, 1, 2, KT_UI_CHORD(KT_KEY_UP, 0),
              KT_UI_CHORD('p', KT_MOD_CTRL)),
    KT_UI_ROW(KT_PALETTE_DOWN, "palette.down", "select", ALL, NULL, 1, 2, KT_UI_CHORD(KT_KEY_DOWN, 0),
              KT_UI_CHORD('n', KT_MOD_CTRL)),
    KT_UI_ROW(KT_PALETTE_CHOOSE, "palette.choose", "run", ALL, NULL, 2, 1, KT_UI_CHORD(KT_KEY_ENTER, 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_PALETTE_CANCEL, "palette.cancel", "cancel", ALL, NULL, 2, 1, KT_UI_CHORD(KT_KEY_ESCAPE, 0),
              KT_UI_NO_CHORD),
};

static bool listed(const struct kt_binding *b, uint32_t scope) { return (b->scope & scope) && b->label && b->label[0]; }

/* The listed binding at a position, or NULL past the end. */
static const struct kt_binding *binding_at(const struct kt_palette *p, size_t position) {
    size_t s, i;
    for (s = 0; s < p->source_count; s++)
        for (i = 0; i < p->sources[s].count; i++) {
            const struct kt_binding *b = &p->sources[s].bindings[i];
            if (!listed(b, p->sources[s].scope)) continue;
            if (!position--) return b;
        }
    return NULL;
}

static int score(const char *query, const struct kt_binding *b) {
    int label = kt_fuzzy(query, b->label, NULL, 0), name = b->name ? kt_fuzzy(query, b->name, NULL, 0) : -1;
    /* The label is what people read; a match by name alone ranks lower. */
    if (name >= 0) name /= 2;
    return label > name ? label : name;
}

static void add(struct kt_palette *p, size_t position, const struct kt_binding *b, const char *query) {
    int value = score(query, b);
    if (value < 0 || p->match_count == KT_PALETTE_MAX) return;
    p->matches[p->match_count].index = position;
    p->matches[p->match_count++].score = value;
}

static void refilter(struct kt_palette *p) {
    const char *query = kt_textinput_value(&p->query);
    size_t s, i, position = 0;
    p->match_count = 0;
    for (s = 0; s < p->source_count; s++)
        for (i = 0; i < p->sources[s].count; i++) {
            const struct kt_binding *b = &p->sources[s].bindings[i];
            if (listed(b, p->sources[s].scope)) add(p, position++, b, query);
        }
    if (query[0]) kt_fuzzy_rank(p->matches, p->match_count);
    p->selected = p->top = 0;
}

void kt_palette_init(struct kt_palette *p, const char *title) {
    struct kt_palette_style *s = &p->style;
    memset(p, 0, sizeof(*p));
    p->title = title;
    p->rows = 10;
    kt_textinput_init(&p->query, NULL, 0);
    p->query.prompt = "> ";
    p->query.placeholder = "type to filter";
    p->query.focused = true;
    p->query.style.field = KT_STYLE(KT_KEEP, KT_KEEP, 0);
    s->border = KT_INK(KT_COLOR_ACCENT, 0);
    s->title = KT_INK(KT_COLOR_TEXT, KT_BOLD);
    s->label = KT_INK(KT_COLOR_TEXT, 0);
    s->name = KT_INK(KT_COLOR_FAINT, 0);
    s->keys = KT_INK(KT_COLOR_MUTED, 0);
    s->match = KT_INK(KT_COLOR_ACCENT, KT_UNDERLINE);
    s->selection = KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, 0);
    s->bar = KT_INK(KT_COLOR_ACCENT, 0);
    s->empty = KT_INK(KT_COLOR_MUTED, 0);
    s->hints = kt_ui_hint_style();
    memcpy(p->keys, palette_keys, sizeof(palette_keys));
}

bool kt_palette_set(struct kt_palette *p, const struct kt_help_source *sources, size_t count) {
    p->source_count = count < KT_HELP_SOURCES ? count : KT_HELP_SOURCES;
    if (p->source_count) memcpy(p->sources, sources, p->source_count * sizeof(sources[0]));
    kt_textinput_set(&p->query, "");
    p->chosen = NULL;
    refilter(p);
    return count <= KT_HELP_SOURCES;
}

const struct kt_binding *kt_palette_chosen(const struct kt_palette *p) { return p->chosen; }
uint32_t kt_palette_scope(const struct kt_palette *p) { (void)p; return ALL; }

struct kt_keymap kt_palette_keymap(struct kt_palette *p) {
    struct kt_keymap map;
    map.bindings = p->keys;
    map.count = KT_PALETTE_ACTIONS;
    return map;
}

int kt_palette_height(const struct kt_palette *p, int width) {
    (void)width;
    return (p->rows > 0 ? p->rows : 1) + 2 + 4;
}

/* The query row and the result rows inside the panel. */
static struct kt_rect results_of(struct kt_rect area) { return kt_rect_inset(area, 4, 2, 2, 2); }

void kt_palette_place(struct kt_palette *p, struct kt_rect area) {
    struct kt_rect inside = kt_rect_inset(area, 2, 3, 2, 3);
    p->area = area;
    kt_textinput_place(&p->query, (struct kt_rect){inside.x, inside.y, inside.width, 1});
}

/* ---- input -------------------------------------------------------------- */

static int shown(const struct kt_palette *p) {
    int rows = results_of(p->area).height;
    return rows < 1 ? 1 : rows;
}

static enum kt_palette_result move(struct kt_palette *p, int step) {
    if (!p->match_count) return KT_PALETTE_HANDLED;
    if (step < 0 && p->selected > 0) p->selected--;
    if (step > 0 && p->selected + 1 < p->match_count) p->selected++;
    if (p->selected < p->top) p->top = p->selected;
    if (p->selected >= p->top + (size_t)shown(p)) p->top = p->selected + 1 - (size_t)shown(p);
    return KT_PALETTE_HANDLED;
}

static enum kt_palette_result choose(struct kt_palette *p) {
    if (p->selected >= p->match_count) return KT_PALETTE_HANDLED;
    p->chosen = binding_at(p, p->matches[p->selected].index);
    return p->chosen ? KT_PALETTE_CHOSEN : KT_PALETTE_HANDLED;
}

static enum kt_palette_result mouse(struct kt_palette *p, const struct kt_msg *msg) {
    struct kt_rect results = results_of(p->area);
    int x, y, wheel = kt_ui_wheel(msg);
    if (wheel) return move(p, wheel);
    if (!kt_ui_click(msg, results, &x, &y)) return KT_PALETTE_HANDLED;
    if (p->top + (size_t)y >= p->match_count) return KT_PALETTE_HANDLED;
    p->selected = p->top + (size_t)y;
    return choose(p);
}

enum kt_palette_result kt_palette_update(struct kt_palette *p, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action;
    if (msg->type == KT_MSG_MOUSE) return mouse(p, msg);
    action = kt_ui_action(p->keys, KT_PALETTE_ACTIONS, &p->chord, ALL, msg, &consumed);
    if (action == KT_PALETTE_UP || action == KT_PALETTE_DOWN) return move(p, action == KT_PALETTE_UP ? -1 : 1);
    if (action == KT_PALETTE_CHOOSE) return choose(p);
    if (action == KT_PALETTE_CANCEL) return KT_PALETTE_CANCELLED;
    if (consumed) return KT_PALETTE_HANDLED;
    switch (kt_textinput_update(&p->query, msg, cmds)) {
    case KT_TEXTINPUT_CHANGED: refilter(p); return KT_PALETTE_HANDLED;
    case KT_TEXTINPUT_IGNORED: return KT_PALETTE_IGNORED;
    default: return KT_PALETTE_HANDLED;
    }
}

/* ---- view --------------------------------------------------------------- */

static void draw_row(const struct kt_palette *p, struct kt_canvas *canvas, struct kt_rect row,
                     const struct kt_binding *b, bool selected) {
    struct kt_style label = p->style.label;
    struct kt_rect text = kt_rect_inset(row, 0, 1, 0, 2);
    char keys[48] = "";
    int used;
    if (selected) {
        label.attrs |= KT_BOLD;
        kt_ui_selection(canvas, row, true, p->style.selection, p->style.bar, p->style.bar);
    }
    if (b->chord_count) (void)kt_chord_hint(&b->chords[0], keys, sizeof(keys));
    used = kt_text_aligned(canvas, text, keys, KT_ALIGN_RIGHT, p->style.keys);
    text.width -= used ? used + 2 : 0;
    used = kt_ui_marked(canvas, text, b->label, kt_textinput_value(&p->query), label, p->style.match);
    if (b->name) (void)kt_text_aligned(canvas, kt_rect_inset(text, 0, 0, 0, used + 2), b->name, KT_ALIGN_LEFT,
                                       p->style.name);
}

void kt_palette_view(const struct kt_palette *p, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_help_source hints;
    struct kt_rect inside, results = results_of(area);
    size_t i;
    hints.bindings = p->keys;
    hints.count = KT_PALETTE_ACTIONS;
    hints.scope = ALL;
    inside = kt_ui_dialog(canvas, area, p->title, p->style.border, p->style.title, &hints, 1, &p->style.hints);
    kt_textinput_view(&p->query, canvas, (struct kt_rect){inside.x, inside.y, inside.width, 1});
    if (!p->match_count)
        (void)kt_text_aligned(canvas, kt_rect_inset((struct kt_rect){results.x, results.y, results.width, 1}, 0, 0, 0, 2),
                              "No matching command", KT_ALIGN_LEFT, p->style.empty);
    for (i = p->top; i < p->match_count && (int)(i - p->top) < results.height; i++) {
        const struct kt_binding *b = binding_at(p, p->matches[i].index);
        if (b) draw_row(p, canvas, (struct kt_rect){results.x, results.y + (int)(i - p->top), results.width, 1}, b,
                        i == p->selected);
    }
}
