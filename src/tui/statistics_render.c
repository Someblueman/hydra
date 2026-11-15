#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "statistics.h"
#include "../kettle/components.h"
/* Statistics are a view over recorded evidence, never a telemetry collector.
 * This file draws the frame every Stats page shares and the workflow runs
 * page; statistics_sessions.c draws head sessions. */

#define TILE_GAP 2
#define CHART_WIDTH 52
#define LIST_SHARE 60

/* ---- text ---------------------------------------------------------------- */

const char *stats_range(const struct statistics_view *v) {
    return v->filter.days == 1 ? "24 hours" : v->filter.days == 7 ? "7 days" : "all recorded";
}

void stats_time(uint64_t epoch, bool known, char *out, size_t size) {
    time_t value = (time_t)epoch;
    struct tm utc;
    if (!known || !epoch || !gmtime_r(&value, &utc) || !strftime(out, size, "%m-%d %H:%M UTC", &utc))
        copy_text(out, size, "unknown date");
}

void stats_seconds(uint64_t seconds, bool known, char *out, size_t size) {
    if (known) format_duration((long long)seconds, out, size);
    else copy_text(out, size, "--");
}

void stats_percent(uint64_t part, uint64_t whole, char *out, size_t size) {
    if (whole) (void)snprintf(out, size, "%.0f%%", (double)part * 100.0 / (double)whole);
    else copy_text(out, size, "--");
}

static const char *all_text(const struct app *app) {
    if (app->fleet) return "all hosts";
    return statistics_sessions_shown(app) ? "all heads" : "all workflows";
}

void stats_scope(const struct app *app, bool location, char *out, size_t size) {
    const struct statistics_view *v = app->statistics;
    const char *sep = dot(app);
    (void)snprintf(out, size, "%s%s%.60s", app->fleet ? "latest fleet response" : stats_range(v), sep,
                   v->filter.query[0] ? v->filter.query : all_text(app));
    if (!statistics_sessions_shown(app) && v->filter.workflow[0])
        text_append(out, size, "%sworkflow %.60s", sep, v->filter.workflow);
    if (v->filter.attention) text_append(out, size, "%sneeds you only", sep);
    if (location && !app->fleet && v->filter.location[0])
        text_append(out, size, "%slocation: %.60s", sep, v->filter.location);
}

/* ---- layout -------------------------------------------------------------- */

bool stats_side(const struct app *app) { return app->cols >= 120; }

bool stats_evidence_focused(const struct app *app) {
    const struct statistics_view *v = app->statistics;
    return v && v->detail && (!stats_side(app) || v->evidence_focus);
}

/* At 30 rows and more a blank row follows the strip and each block; the
 * tiles need 100 columns, the charts 34 rows as well. */
void stats_layout(const struct app *app, const struct kt_canvas *c, int notes, bool data, struct stats_frame *f) {
    (void)app;
    memset(f, 0, sizeof(*f));
    f->width = c->width - 2;
    f->strip = 2;
    f->top = c->height >= 30 ? 4 : 3;
    f->tiles = f->width >= 97 && c->height >= 26;
    f->summary = !f->tiles && f->width >= 68 && c->height >= 16;
    f->charts = f->tiles && data && c->height >= 34 + notes;
    f->side = stats_side(app);
}

int stats_list_row(const struct stats_frame *f, int notes) {
    int row = f->top;
    if (f->tiles) row += 4 + notes + 1;
    else if (f->summary) row += 2;
    if (f->charts) row += 9;
    return row;
}

void stats_areas(const struct app *app, const struct kt_canvas *c, const struct stats_frame *f, int row,
                 struct kt_rect *list, struct kt_rect *evidence) {
    int bottom = c->height - 3, width = c->width - 1;
    *list = (struct kt_rect){0, row, width, bottom - row + 1};
    *evidence = *list;
    if (!app->statistics->detail) return;
    if (!f->side) { list->height = 0; return; }
    list->width = width * LIST_SHARE / 100;
    *evidence = (struct kt_rect){list->width + 1, row, width - list->width - 1, list->height};
}

/* ---- pieces -------------------------------------------------------------- */

void stats_rule(struct kt_canvas *c, struct kt_rect r, const char *title, const char *note, bool focused) {
    int length = note ? kt_text_width(note) : 0, x;
    if (r.width < 4 || r.height < 1) return;
    x = r.x + 1;
    kt_rule_styled(c, (struct kt_rect){x, r.y, r.width - 1, 1}, title, KT_ALIGN_LEFT,
                   focused ? KT_FOCUS : KT_STRONG, focused ? KT_FOCUS : KT_BORDER);
    if (!length || length + kt_text_width(title) + 8 > r.width) return;
    /* The note ends the rule: a blank before it and one after. */
    kt_fill(c, (struct kt_rect){r.x + r.width - length - 3, r.y, length + 3, 1}, KT_BASE);
    kt_text(c, (struct kt_rect){r.x + r.width - length - 2, r.y, length, 1}, note, KT_MUTED);
}

static void strip_hit(struct app *app, int x, int y, int width, size_t item) {
    size_t n = app->hit_count;
    if (n == MAX_HEADS) return;
    app->hit_rows[n] = y + 1; app->hit_bottom[n] = y + 1;
    app->hit_left[n] = x + 1; app->hit_right[n] = x + width;
    app->hit_items[n] = item; app->hit_count++;
}

/* One word of the strip, with a blank on each side; returns where the next
 * goes. */
static int strip_word(struct app *app, struct kt_canvas *c, int x, int y, const char *word, bool shown, size_t hit) {
    int width = kt_text_width(word);
    kt_fill(c, (struct kt_rect){x - 1, y, width + 2, 1}, KT_BASE);
    kt_text(c, (struct kt_rect){x, y, width, 1}, word, shown ? KT_STRONG : KT_MUTED);
    if (hit) strip_hit(app, x, y, width, hit);
    return x + width + 1;
}

/* What the filters cover, ending the strip's rule: a blank before it and one
 * after, the location filter last as a chip (accent on a raised fill,
 * reverse video without colour). Dropped when it would not fit after the
 * pages' words, which take the first `words` columns. */
static void strip_scope(struct app *app, struct kt_canvas *c, const struct stats_frame *f, int words) {
    const struct statistics_view *v = app->statistics;
    char scope[256], chip[96] = "";
    int end = f->width - 1, length, chip_width = 0;
    stats_scope(app, false, scope, sizeof(scope));
    if (!app->fleet && v->filter.location[0]) {
        (void)snprintf(chip, sizeof(chip), " location: %.60s ", v->filter.location);
        chip_width = kt_text_width(chip);
    }
    length = kt_text_width(scope) + (chip_width ? chip_width + 1 : 0);
    if (length + words > f->width) { scope[0] = '\0'; length = chip_width; }
    if (!length || length + words > f->width) return;
    kt_fill(c, (struct kt_rect){end - length - 1, f->strip, length + 3, 1}, KT_BASE);
    if (scope[0]) kt_text(c, (struct kt_rect){end - length, f->strip, kt_text_width(scope), 1}, scope, KT_MUTED);
    if (chip_width)
        kt_text(c, (struct kt_rect){end - chip_width, f->strip, chip_width, 1}, chip, app->no_color ?
                KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, KT_BOLD) : KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_RAISED, KT_BOLD));
}

/* The page strip: the pages as words in a rule (the shown one bold), and
 * what the filters cover at its right. Fleet mode has one page, its hosts;
 * a sample older than head sessions has no sessions page, and one without
 * remote steps no cohorts page. */
void stats_strip(struct app *app, struct kt_canvas *c, const struct stats_frame *f) {
    static const char *const words[] = {"Sessions", "Runs", "Cohorts"};
    static const size_t hits[] = {STATS_HIT_SESSIONS, STATS_HIT_RUNS, STATS_HIT_COHORTS};
    enum stats_page pages[3], shown = statistics_page(app);
    size_t count = statistics_pages(app, pages), i;
    int x = 1, width = 6;
    for (i = 0; i < count; i++) width += kt_text_width(words[pages[i]]) + 2;
    stats_rule(c, (struct kt_rect){0, f->strip, f->width + 1, 1}, NULL, NULL, false);
    strip_scope(app, c, f, width);
    if (app->fleet) { (void)strip_word(app, c, x, f->strip, "Hosts", true, 0); return; }
    for (i = 0; i < count; i++)
        x = strip_word(app, c, x, f->strip, words[pages[i]], pages[i] == shown, hits[pages[i]]) + 1;
}

/* A tile is a raised fill with a label, a value and a note; no box. */
void stats_tiles(struct kt_canvas *c, int y, int width, const struct stats_tile tiles[4]) {
    struct kt_style raised = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_RAISED, 0);
    int tile = (width - 3 * TILE_GAP) / 4, i;
    for (i = 0; i < 4; i++) {
        int x = 1 + i * (tile + TILE_GAP);
        struct kt_style value = tiles[i].tone;
        value.bg = KT_COLOR_RAISED; value.attrs |= KT_BOLD;
        kt_fill(c, (struct kt_rect){x, y, tile, 3}, raised);
        (void)kt_text_aligned(c, (struct kt_rect){x + 1, y, tile - 2, 1}, tiles[i].label, KT_ALIGN_LEFT,
                              KT_STYLE(KT_COLOR_MUTED, KT_COLOR_RAISED, 0));
        (void)kt_text_aligned(c, (struct kt_rect){x + 1, y + 1, tile - 2, 1}, tiles[i].value, KT_ALIGN_LEFT, value);
        (void)kt_text_aligned(c, (struct kt_rect){x + 1, y + 2, tile - 2, 1}, tiles[i].note, KT_ALIGN_LEFT,
                              KT_STYLE(KT_COLOR_FAINT, KT_COLOR_RAISED, 0));
    }
}

void stats_note(struct kt_canvas *c, int y, int width, const char *text) {
    (void)kt_text_aligned(c, (struct kt_rect){1, y, width, 1}, text, KT_ALIGN_LEFT, KT_MUTED);
}

/* The progress component's fractional blocks in accent, with no track. */
void stats_fraction(struct kt_canvas *c, struct kt_rect r, double fraction) {
    struct kt_progress bar;
    kt_progress_init(&bar);
    bar.percent = false;
    bar.fill = KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SURFACE, 0);
    bar.track = KT_STYLE(KT_COLOR_SURFACE, KT_COLOR_SURFACE, 0);
    kt_progress_set(&bar, fraction);
    kt_progress_view(&bar, c, r);
}

void stats_bar(struct kt_canvas *c, int x, int y, int width, const char *label, size_t count, double fraction) {
    char text[48];
    (void)snprintf(text, sizeof(text), "%-4s%3zu", label, count);
    kt_text(c, (struct kt_rect){x, y, 8, 1}, text, KT_BASE);
    kt_text(c, (struct kt_rect){x, y, kt_text_width(label), 1}, label, KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0));
    if (width <= 10 || !count) return;
    stats_fraction(c, (struct kt_rect){x + 9, y, width - 10, 1}, fraction);
}

/* ---- the list ------------------------------------------------------------ */

struct cells {
    const struct stats_list *list;
    int ids[KT_TABLE_COLUMNS];
    char text[256];
};

static void cell_text(void *context, size_t row, size_t column, struct kt_table_cell *cell) {
    struct cells *k = context;
    kt_color color = KT_DEFAULT;
    k->text[0] = '\0';
    k->list->cell(k->list->context, row, k->ids[column], k->text, sizeof(k->text), &color);
    cell->text = k->text; cell->color = color; cell->attrs = 0;
}

/* The columns that fit; the last one shown takes the rest of the width. */
static size_t list_columns(const struct stats_list *l, int width, struct kt_table_column *out, int *ids) {
    size_t i, n = 0;
    for (i = 0; i < l->column_count && n < KT_TABLE_COLUMNS; i++) {
        if (width < l->columns[i].from) continue;
        out[n].title = l->columns[i].title;
        out[n].width = (struct kt_constraint)KT_FIXED(l->columns[i].width);
        out[n].align = KT_ALIGN_LEFT;
        ids[n++] = (int)i;
    }
    if (n) out[n - 1].width = (struct kt_constraint)KT_FLEX(1);
    return n;
}

static void row_hit(struct app *app, struct kt_rect line, size_t item) {
    size_t n = app->hit_count;
    if (n == MAX_HEADS) return;
    app->hit_rows[n] = line.y + 1; app->hit_bottom[n] = line.y + 1;
    app->hit_left[n] = line.x + 1; app->hit_right[n] = line.x + line.width;
    app->hit_items[n] = item; app->hit_count++;
}

/* The filter's text marked where it occurs in each row's first column. */
static void mark_match(struct kt_canvas *c, struct kt_rect part, const char *text, const char *query) {
    size_t length = strlen(query), i;
    char prefix[256];
    int x;
    if (!length) return;
    for (i = 0; text[i]; i++) if (!strncasecmp(text + i, query, length)) break;
    if (!text[i] || i >= sizeof(prefix)) return;
    memcpy(prefix, text, i); prefix[i] = '\0';
    x = part.x + kt_text_width(prefix);
    if (x >= part.x + part.width) return;
    kt_restyle(c, (struct kt_rect){x, part.y, kt_text_width(query), 1}, KT_INK(KT_COLOR_ACCENT, KT_UNDERLINE));
}

static void list_rows(struct app *app, struct kt_canvas *c, const struct kt_table *t, struct cells *k,
                      struct kt_rect body) {
    struct kt_rect parts[KT_TABLE_COLUMNS];
    const char *query = app->statistics->filter.query;
    kt_color color;
    size_t row;
    kt_table_columns(t, (struct kt_rect){body.x, body.y - 1, body.width, body.height + 1}, parts);
    for (row = t->top; row < t->count && (int)(row - t->top) < body.height; row++) {
        struct kt_rect line = {body.x, body.y + (int)(row - t->top), body.width, 1};
        row_hit(app, line, row);
        if (row == t->selected && !c->unicode)
            kt_put(c, line.x, line.y, '>', t->focused ? t->style.bar : t->style.bar_unfocused);
        k->list->cell(k->list->context, row, k->ids[0], k->text, sizeof(k->text), &color);
        parts[0].y = line.y;
        mark_match(c, parts[0], k->text, query);
    }
}

/* The list's rule names the filter while it is typed or kept, and where the
 * selection is among the rows. */
static void list_note(const struct app *app, const struct stats_list *l, char *out, size_t size) {
    const struct statistics_view *v = app->statistics;
    const char *sep = dot(app);
    out[0] = '\0';
    if (v->typing || v->filter.query[0])
        (void)snprintf(out, size, "/%.60s%s%s", v->filter.query, v->typing ? (app->ascii ? "_" : "\xe2\x96\x8f") : "", sep);
    if (l->count) text_append(out, size, "%zu of %zu", l->selected + 1, l->count);
    if (l->order) text_append(out, size, "%s%s", l->count ? sep : "", l->order);
}

void stats_list_view(struct app *app, struct kt_canvas *c, struct kt_rect area, const struct stats_list *l) {
    struct kt_table_column columns[KT_TABLE_COLUMNS];
    struct kt_table t;
    struct cells k;
    char note[160];
    size_t n;
    if (area.height < 1 || area.width < 8) return;
    list_note(app, l, note, sizeof(note));
    stats_rule(c, (struct kt_rect){area.x, area.y, area.width, 1}, l->title, note, false);
    if (area.height < 3) return;
    k.list = l;
    n = list_columns(l, area.width, columns, k.ids);
    kt_table_init(&t, columns, n, cell_text, &k, l->count);
    t.gap = 1; t.focused = l->focused; t.empty = l->empty ? l->empty : "";
    t.selected = l->count ? l->selected : KT_NONE;
    t.style.header = KT_INK(KT_COLOR_FAINT, 0);
    kt_table_place(&t, (struct kt_rect){area.x, area.y + 1, area.width, area.height - 1});
    kt_table_view(&t, c, t.area);
    list_rows(app, c, &t, &k, (struct kt_rect){area.x, area.y + 2, area.width, area.height - 2});
}

/* Nothing recorded at all is told apart from rows the filters hide. */
void stats_empty(struct app *app, struct kt_canvas *c, struct kt_rect area, const char *title, size_t recorded,
                 const char *nothing, const char *how) {
    char text[160];
    int x = area.x + 2, width = area.width - 4;
    stats_rule(c, (struct kt_rect){area.x, area.y, area.width, 1}, title, NULL, false);
    if (area.height < 2) return;
    if (recorded) {
        (void)snprintf(text, sizeof(text), "Nothing matches the filters%s0 resets them", dot(app));
        canvas_text(c, x, area.y + 1, width, KT_WARNING, "%s", text);
        if (area.height > 3)
            canvas_text(c, x, area.y + 2, width, KT_MUTED, "%zu recorded; the filters hide them all.", recorded);
        return;
    }
    canvas_text(c, x, area.y + 1, width, KT_STRONG, "%s", nothing);
    if (area.height > 5 && how) (void)kt_paragraph(c, (struct kt_rect){x, area.y + 3, width, area.height - 3}, how, KT_MUTED);
}

/* ---- status and frame ---------------------------------------------------- */

static void status_text(struct app *app, const char *page, char *text, size_t size) {
    const struct statistics_view *v = app->statistics;
    char scope[256];
    stats_scope(app, true, scope, sizeof(scope));
    (void)snprintf(text, size, "%s%s%s", page, dot(app), scope);
    if (v->model && v->model->warnings) (void)snprintf(text, size, "Partial sample: %s", v->model->warning);
    if (app->notice[0]) copy_text(text, size, app->notice);
    if (v->error[0]) copy_text(text, size, v->error);
}

/* How fresh the sample is, in the words the status chip starts from; a
 * stale sample names the time it was taken. */
static bool fresh_text(struct app *app, bool headless, char *out, size_t size) {
    const struct statistics_view *v = app->statistics;
    char when[40];
    if (v->stale && v->model) {
        stats_time(v->model->observed, true, when, sizeof(when));
        (void)snprintf(out, size, "STALE: last statistics from %s", when);
    } else if (v->stale) copy_text(out, size, "STALE: no statistics yet");
    else if (!v->model) copy_text(out, size, "Waiting for the first snapshot");
    else (void)snapshot_freshness(app, v->received, false, headless, out, size);
    return v->stale || (v->model && v->model->warnings);
}

void stats_status(struct app *app, struct kt_canvas *c, const char *page, bool headless) {
    char fresh[96], text[400], status[512];
    bool warn = fresh_text(app, headless, fresh, sizeof(fresh)), word;
    status_text(app, page, text, sizeof(text));
    /* A chip that is the whole freshness word ends with a colon, which the
     * chip takes; one with more to say (an age, a date) is followed by a dot. */
    word = !strcmp(fresh, "Live") || !strncmp(fresh, "Waiting", 7);
    (void)snprintf(status, sizeof(status), "%s%s%s", fresh, word ? ": " : dot(app), text);
    chrome_footer(app, c, status, warn ? KT_WARNING : KT_MUTED);
}

bool stats_begin(struct app *app, bool headless, struct kt_canvas *c) {
    if (!statistics_init(app) || !frame_begin(app, headless)) return false;
    *c = app->frame;
    if (c->width < 19 || c->height < 6) return false;
    statistics_visible(app);
    chrome_header(app, c, true);
    return true;
}

void stats_finish(struct app *app, struct kt_canvas *c, const char *page, unsigned frame, bool headless) {
    stats_status(app, c, page, headless);
    frame_end(app, frame, headless);
}

/* ---- the runs page ------------------------------------------------------- */

enum { RUN_NAME, RUN_STATE, RUN_CREATED, RUN_TOKENS, RUN_ID };
static const struct stats_column run_columns[] = {
    {"RUN", 28, 0}, {"STATE", 20, 0}, {"CREATED", 16, 60}, {"TOKENS", 24, 96}, {"ID", 30, 118}};

static const char *state_glyph(const struct app *app, enum hs_state state) {
    static const char *const utf8[] = {"\xe2\x9c\x93", "\xe2\x9c\x97", "\xe2\x97\x8f", "\xe2\x97\x8b", "\xe2\x96\xb2",
                                       "\xe2\x97\x8b", "?"};
    static const char *const ascii[] = {"+", "x", "*", "-", "!", "-", "?"};
    return app->ascii ? ascii[state] : utf8[state];
}

static kt_color state_color(enum hs_state state) {
    if (state == HS_SUCCEEDED || state == HS_RUNNING) return KT_COLOR_SUCCESS;
    if (state == HS_FAILED) return KT_COLOR_DANGER;
    if (state == HS_BLOCKED) return KT_COLOR_WARNING;
    return KT_COLOR_MUTED;
}

static void run_cell(void *context, size_t row, int column, char *out, size_t size, kt_color *color) {
    struct app *app = context;
    const struct statistics_view *v = app->statistics;
    const struct hs_run *run = &v->model->runs[v->visible[row]];
    enum hs_state state = hs_state(run->state);
    if (column != RUN_NAME) *color = KT_COLOR_MUTED;
    switch (column) {
    case RUN_NAME: copy_text(out, size, run->name); break;
    case RUN_STATE: (void)snprintf(out, size, "%s %s", state_glyph(app, state), run->state); *color = state_color(state); break;
    case RUN_CREATED: stats_time(run->created, true, out, size); break;
    case RUN_TOKENS: statistics_run_tokens(v->model, v->visible[row], out, size); break;
    default: copy_text(out, size, run->id); break;
    }
}

static void run_tiles(struct kt_canvas *c, int y, int width, const struct hs_summary *s) {
    char runs[24], running[24], attention[24], retries[24];
    struct stats_tile tiles[4] = {
        {"Runs", runs, "in this scope", KT_BASE}, {"Running", running, "steps right now", KT_SUCCESS},
        {"Need attention", attention, "failed, blocked or unknown", KT_WARNING},
        {"Retries", retries, "repeated attempts", KT_BASE}};
    (void)snprintf(runs, sizeof(runs), "%zu", s->runs);
    (void)snprintf(running, sizeof(running), "%zu", s->step_states[HS_RUNNING]);
    (void)snprintf(attention, sizeof(attention), "%zu", s->run_states[HS_FAILED] + s->run_states[HS_BLOCKED] + s->run_states[HS_UNKNOWN]);
    if (s->attempts_known || !s->steps) (void)snprintf(retries, sizeof(retries), "%llu", (unsigned long long)s->retries);
    else copy_text(retries, sizeof(retries), "--");
    stats_tiles(c, y, width, tiles);
}

/* How the timing and tokens are known; the short forms when narrow. */
static void run_timing(char *out, size_t size, const struct hs_summary *s, bool wide) {
    char mean[40] = "--", longest[40] = "--";
    if (s->duration_known) {
        (void)snprintf(mean, sizeof(mean), "%.1fs", (double)s->duration_sum / (double)s->duration_known);
        (void)snprintf(longest, sizeof(longest), "%llus", (unsigned long long)s->duration_max);
    }
    (void)snprintf(out, size, wide ? "Latest attempts average %s over %zu timed steps, longest %s. " :
                   "Average %s over %zu steps, longest %s. ", mean, s->duration_known, longest);
    text_append(out, size, "Timing %zu/%zu, Attempts %zu/%zu, %zu undated, %zu outside range.", s->duration_known,
                s->steps, s->attempts_known, s->steps, s->undated, s->excluded_undated);
}

/* What the steps' resources are known from: remote task receipts, or
 * nothing for local steps. */
static void run_resources(char *out, size_t size, const struct hs_summary *s) {
    char cpu[40], rss[40];
    if (!s->remote_steps) { copy_text(out, size, "CPU and memory: not measured for local steps (no task receipt)."); return; }
    statistics_remote_text(HS_CPU_MICROSECONDS, s->remote[HS_CPU_MICROSECONDS], s->remote_known[HS_CPU_MICROSECONDS],
                           s->remote_steps, cpu, sizeof(cpu));
    statistics_remote_text(HS_PEAK_RSS_BYTES, s->remote[HS_PEAK_RSS_BYTES], s->remote_known[HS_PEAK_RSS_BYTES],
                           s->remote_steps, rss, sizeof(rss));
    (void)snprintf(out, size, "Remote CPU %s, peak RSS %s from %zu task receipts; local steps unmeasured. "
                   "C shows the remote cohorts.", cpu, rss, s->remote_steps);
}

static void run_tokens(char *out, size_t size, const struct hs_summary *s, bool wide) {
    char in[24], cached[24], output[24];
    if (!s->agent_steps) {
        copy_text(out, size, "Agent tokens: no agent steps in scope. ");
        if (!s->remote_steps) text_append(out, size, "CPU and memory: not measured.");
        return;
    }
    statistics_count(s->usage[HS_TOKENS_IN], s->usage_known[HS_TOKENS_IN] > 0, in, sizeof(in));
    statistics_count(s->usage[HS_TOKENS_CACHED], s->usage_known[HS_TOKENS_CACHED] > 0, cached, sizeof(cached));
    statistics_count(s->usage[HS_TOKENS_OUT], s->usage_known[HS_TOKENS_OUT] > 0, output, sizeof(output));
    (void)snprintf(out, size, wide ? "Agent tokens in %s, cached %s, out %s (%zu of %zu agent steps reported); " :
                   "Tokens in %s, cached %s, out %s (%zu/%zu steps); ", in, cached, output, s->usage_known[HS_TOKENS_IN], s->agent_steps);
    if (s->usage_known[HS_COST_MICROUSD]) text_append(out, size, "cost $%.4f. ", (double)s->usage[HS_COST_MICROUSD] / 1e6);
    else text_append(out, size, "cost not reported. ");
    if (wide && !s->remote_steps) text_append(out, size, "CPU and memory: not measured.");
}

static void run_summary(const struct app *app, struct kt_canvas *c, int y, int width, const struct hs_summary *s) {
    char text[256], tokens[24];
    const char *sep = dot(app);
    (void)snprintf(text, sizeof(text), "%zu run%s%s%zu steps%s%zu need attention", s->runs, s->runs == 1 ? "" : "s", sep,
                   s->steps, sep, s->run_states[HS_FAILED] + s->run_states[HS_BLOCKED] + s->run_states[HS_UNKNOWN]);
    if (s->agent_steps) {
        statistics_count(s->usage[HS_TOKENS_IN], s->usage_known[HS_TOKENS_IN] > 0, tokens, sizeof(tokens));
        text_append(text, sizeof(text), "%stokens in %s", sep, tokens);
    }
    text_append(text, sizeof(text), "%stiming %zu/%zu", sep, s->duration_known, s->steps);
    (void)kt_text_aligned(c, (struct kt_rect){1, y, width, 1}, text, KT_ALIGN_LEFT, KT_BASE);
}

/* One outcome: its name (warning when runs failed or are blocked), its
 * count and its share of the runs. */
static void outcome_row(struct kt_canvas *c, int x, int y, int width, enum hs_state state, const struct hs_summary *s) {
    static const char *names[] = {"Succeeded", "Failed", "Running", "Queued", "Blocked", "Cancelled", "Unknown"};
    size_t count = s->run_states[state];
    bool warn = count && (state == HS_FAILED || state == HS_BLOCKED);
    canvas_text(c, x, y, 12, warn ? KT_WARNING : KT_BASE, "%s", names[state]);
    canvas_text(c, x + 12, y, 5, KT_MUTED, "%3zu", count);
    if (count && width > 30)
        stats_fraction(c, (struct kt_rect){x + 18, y, width - 18 < 40 ? width - 18 : 40, 1},
                       (double)count / (double)(s->runs ? s->runs : 1));
}

static void run_charts(struct kt_canvas *c, int y, int width, const struct hs_summary *s) {
    size_t i, maximum = 1;
    int chart = width / 2 - 4 < CHART_WIDTH ? width / 2 - 4 : CHART_WIDTH, x = chart + 4;
    char label[8];
    for (i = 0; i < 7; i++) if (s->daily[i] > maximum) maximum = s->daily[i];
    kt_text(c, (struct kt_rect){1, y, chart, 1}, "Runs created, last 7 days", KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_BOLD));
    kt_text(c, (struct kt_rect){x, y, width - x, 1}, "Run outcomes", KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_BOLD));
    for (i = 0; i < 7; i++) {
        (void)snprintf(label, sizeof(label), "-%zud", 6 - i);
        stats_bar(c, 1, y + 1 + (int)i, chart, label, s->daily[i], (double)s->daily[i] / (double)maximum);
        outcome_row(c, x, y + 1 + (int)i, width - x, (enum hs_state)i, s);
    }
}

static void run_list(struct app *app, struct kt_canvas *c, struct kt_rect area) {
    const struct statistics_view *v = app->statistics;
    struct stats_list l;
    if (!v->count) {
        stats_empty(app, c, area, "Workflow runs", v->model->run_count, "No workflow runs recorded yet",
                    "A run is recorded each time a plan is executed: its steps, timing, outcomes, retries and tokens.");
        return;
    }
    memset(&l, 0, sizeof(l));
    l.title = "Workflow runs"; l.order = "newest first"; l.columns = run_columns; l.column_count = sizeof(run_columns) / sizeof(run_columns[0]);
    l.count = v->count; l.selected = v->selected; l.cell = run_cell; l.context = app;
    l.focused = !stats_evidence_focused(app);
    stats_list_view(app, c, area, &l);
}

static void run_notes(struct kt_canvas *c, const struct stats_frame *f, const struct hs_summary *s) {
    char text[512];
    bool wide = f->width >= 128;
    run_timing(text, sizeof(text), s, wide);
    stats_note(c, f->top + 4, f->width, text);
    run_tokens(text, sizeof(text), s, wide);
    stats_note(c, f->top + 5, f->width, text);
    if (!s->remote_steps) return;
    run_resources(text, sizeof(text), s);
    stats_note(c, f->top + 6, f->width, text);
}

void stats_runs_page(struct app *app, struct kt_canvas *c) {
    struct statistics_view *v = app->statistics;
    struct hs_summary summary;
    struct stats_frame f;
    struct kt_rect list, evidence;
    int notes;
    hs_summarize(v->model, &v->filter, &summary);
    notes = summary.remote_steps ? 3 : 2;
    stats_layout(app, c, notes, summary.runs > 0 && !v->detail, &f);
    stats_strip(app, c, &f);
    if (!v->model->run_count) f.tiles = f.summary = f.charts = false;
    if (f.tiles) { run_tiles(c, f.top, f.width, &summary); run_notes(c, &f, &summary); }
    else if (f.summary) run_summary(app, c, f.top, f.width, &summary);
    if (f.charts) run_charts(c, stats_list_row(&f, notes) - 9, f.width, &summary);
    if (v->metric_page) {
        int row = stats_list_row(&f, notes);
        statistics_metrics_render(app, c, (struct kt_rect){0, row, c->width - 1, c->height - 2 - row});
        return;
    }
    stats_areas(app, c, &f, stats_list_row(&f, notes), &list, &evidence);
    run_list(app, c, list);
    if (v->detail) stats_run_evidence(app, c, evidence, stats_evidence_focused(app));
}

/* ---- the screen ---------------------------------------------------------- */

static void stats_loading(struct app *app, struct kt_canvas *c) {
    struct stats_frame f;
    stats_layout(app, c, 0, false, &f);
    stats_rule(c, (struct kt_rect){0, f.strip, f.width + 1, 1}, "Stats", NULL, false);
    canvas_text(c, 2, f.top, f.width - 2, KT_STRONG, "%s",
                   native_observations_loading(app, OBSERVE_STATISTICS) ? "Loading statistics..." : "No statistics yet");
    if (c->height > 12)
        (void)kt_paragraph(c, (struct kt_rect){2, f.top + 2, f.width - 4 < 76 ? f.width - 4 : 76, 4},
            "Stats cover head sessions (each start or resume of a head, and how long its terminal was active or quiet "
            "while a control centre watched) and the workflow runs Hydra recorded: steps, timing, outcomes, retries "
            "and tokens. ctrl+r reads them again.", KT_MUTED);
}

bool render_statistics(struct app *app, unsigned frame, bool headless) {
    struct kt_canvas c;
    const char *page = "Workflow runs";
    if (!native_workspace_init(app) || !stats_begin(app, headless, &c)) return false;
    if (app->fleet) { stats_fleet_page(app, &c); page = "Hosts"; }
    else if (!app->statistics->model) { stats_loading(app, &c); page = "Stats"; }
    else if (statistics_sessions_shown(app)) { stats_sessions_page(app, &c); page = "Head sessions"; }
    else if (statistics_cohorts_shown(app)) { stats_cohorts_page(app, &c); page = "Remote cohorts"; }
    else stats_runs_page(app, &c);
    stats_finish(app, &c, page, frame, headless);
    return true;
}

/* ---- the fleet's hosts page ---------------------------------------------- */

enum { HOST_NAME, HOST_STATE, HOST_HEADS };
static const struct stats_column host_columns[] = {{"HOST", 24, 0}, {"STATE", 12, 0}, {"HEADS", 8, 0}};

static bool host_failed(const struct host_observation *h) { return !strcmp(h->state, "failed"); }

static void host_cell(void *context, size_t row, int column, char *out, size_t size, kt_color *color) {
    struct app *app = context;
    const struct host_observation *h = &app->model.hosts[app->statistics->visible[row]];
    if (column == HOST_NAME) copy_text(out, size, h->name);
    else if (column == HOST_STATE) { copy_text(out, size, h->state); *color = host_failed(h) ? KT_COLOR_WARNING : KT_COLOR_MUTED; }
    else if (host_failed(h)) { copy_text(out, size, "--"); *color = KT_COLOR_MUTED; }
    else { (void)snprintf(out, size, "%u", h->heads); *color = KT_COLOR_MUTED; }
}

struct host_counts { size_t responded, failed, heads; };

static void host_counts(const struct app *app, struct host_counts *n) {
    const struct statistics_view *v = app->statistics;
    size_t i;
    memset(n, 0, sizeof(*n));
    for (i = 0; i < v->count; i++) {
        const struct host_observation *h = &app->model.hosts[v->visible[i]];
        if (host_failed(h)) n->failed++;
        else { n->responded++; n->heads += h->heads; }
    }
}

static void host_tiles(struct kt_canvas *c, int y, int width, const struct host_counts *n, const char *heads) {
    char responded[24], failed[24];
    struct stats_tile tiles[4] = {
        {"Responded", responded, "latest response", KT_SUCCESS}, {"Failed", failed, "no response", KT_WARNING},
        {"Reported heads", heads, "from hosts that responded", KT_BASE},
        {"Remote history", "--", "workflow, CPU, memory, cost", KT_MUTED}};
    (void)snprintf(responded, sizeof(responded), "%zu", n->responded);
    (void)snprintf(failed, sizeof(failed), "%zu", n->failed);
    stats_tiles(c, y, width, tiles);
}

/* The selected host's heads from the step offset, or why there are none. */
static void host_evidence(struct app *app, struct kt_canvas *c, struct kt_rect r) {
    const struct statistics_view *v = app->statistics;
    const char *host;
    size_t i, ordinal = 0;
    int row = r.y + 1, x = r.x + 2, width = r.width - 3;
    stats_rule(c, (struct kt_rect){r.x, r.y, r.width, 1}, "Host evidence", "heads it reported", stats_evidence_focused(app));
    if (!v->count) { canvas_text(c, x, row, width, KT_WARNING, "Selected host no longer matches"); return; }
    host = app->model.hosts[v->visible[v->selected]].name;
    for (i = 0; i < app->model.head_count && row < r.y + r.height; i++) {
        const struct head *h = &app->model.heads[i];
        if (strcmp(h->remote_host, host) || ordinal++ < v->step_scroll) continue;
        canvas_text(c, x, row++, width, KT_BASE, "%s / %s / %s", h->remote_project, h->branch, h->desired);
    }
    if (row == r.y + 1) canvas_text(c, x, row, width, KT_WARNING, "No head evidence for %s here; inspect the host's state", host);
}

void stats_fleet_page(struct app *app, struct kt_canvas *c) {
    const struct statistics_view *v = app->statistics;
    struct stats_frame f;
    struct host_counts n;
    struct stats_list l;
    struct kt_rect list, evidence;
    char heads[24] = "--", text[256];
    host_counts(app, &n);
    if (n.responded) (void)snprintf(heads, sizeof(heads), "%zu", n.heads);
    stats_layout(app, c, 1, false, &f);
    stats_strip(app, c, &f);
    (void)snprintf(text, sizeof(text), "%zu responded   %zu failed   %s reported heads", n.responded, n.failed, heads);
    if (f.tiles) {
        host_tiles(c, f.top, f.width, &n, heads);
        text_append(text, sizeof(text), ". Failed hosts have unknown head counts; remote history, CPU, memory and cost are unavailable.");
        stats_note(c, f.top + 4, f.width, text);
    } else if (f.summary) (void)kt_text_aligned(c, (struct kt_rect){1, f.top, f.width, 1}, text, KT_ALIGN_LEFT, KT_STRONG);
    stats_areas(app, c, &f, stats_list_row(&f, 1), &list, &evidence);
    if (!v->count) stats_empty(app, c, list, "Hosts", app->model.host_count, "No hosts reported yet", NULL);
    else {
        memset(&l, 0, sizeof(l));
        l.title = "Hosts"; l.columns = host_columns; l.column_count = sizeof(host_columns) / sizeof(host_columns[0]);
        l.count = v->count; l.selected = v->selected; l.cell = host_cell; l.context = app;
        l.focused = !stats_evidence_focused(app);
        stats_list_view(app, c, list, &l);
    }
    if (v->detail) host_evidence(app, c, evidence);
}
