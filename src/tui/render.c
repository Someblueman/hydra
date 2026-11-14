#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
/* One frame canvas serves every view. Views draw into it; the loop's runtime
 * paints only changed cells, so idle refreshes never clear or flicker the screen.
 * Semantic tones stay restrained: cyan chrome, amber attention, green running. */

static const char *theme_names[] = {"terminal", "dark", "light"};

/* Caller owns the output integer; names are fixed internal strings. */
bool parse_theme(const char *name, int *theme) {
    int index;
    for (index = 0; index < 3; index++) {
        if (strcmp(name, theme_names[index]) == 0) { *theme = index; return true; }
    }
    return false;
}

/* The three themes, as palette entries so a 256-colour terminal shows exactly
 * these. Terminal keeps the user's own colours and background: muted is dim
 * and the selection is bold reverse video. Without colour (NO_COLOR) every
 * theme falls back to those same attributes. A 16-colour terminal gets the
 * nearest of its own colours, or the one named here where nearest would be a
 * grey that loses the meaning. */
static const struct kt_theme themes[3] = {
    {"terminal", {
        [KT_TOKEN_MUTED] = {KT_DEFAULT, 0, 0, KT_DIM, 0},
        [KT_TOKEN_SELECTION] = {KT_DEFAULT, 0, 0, KT_REVERSE, 0},
        [KT_TOKEN_ON_SELECTION] = {KT_DEFAULT, 0, 0, KT_BOLD, 0},
        [KT_TOKEN_BORDER] = {KT_INDEX(6), 0, 0, 0, 0},
        [KT_TOKEN_ACCENT] = {KT_INDEX(6), 0, 0, 0, 0},
        [KT_TOKEN_SUCCESS] = {KT_INDEX(2), 0, 0, 0, 0},
        [KT_TOKEN_WARNING] = {KT_INDEX(3), 0, 0, 0, 0}}},
    {"dark", {
        [KT_TOKEN_TEXT] = {KT_INDEX(252), 0, 0, 0, 0},
        [KT_TOKEN_MUTED] = {KT_INDEX(245), 0, 0, 0, KT_DIM},
        [KT_TOKEN_SURFACE] = {KT_INDEX(234), 0, 0, 0, 0},
        [KT_TOKEN_SELECTION] = {KT_INDEX(117), 0, KT_INDEX(14), 0, KT_REVERSE},
        [KT_TOKEN_ON_SELECTION] = {KT_INDEX(16), 0, 0, 0, KT_BOLD},
        [KT_TOKEN_BORDER] = {KT_INDEX(66), 0, KT_INDEX(6), 0, 0},
        [KT_TOKEN_ACCENT] = {KT_INDEX(117), 0, KT_INDEX(14), 0, 0},
        [KT_TOKEN_SUCCESS] = {KT_INDEX(114), 0, KT_INDEX(10), 0, 0},
        [KT_TOKEN_WARNING] = {KT_INDEX(214), 0, 0, 0, 0}}},
    {"light", {
        [KT_TOKEN_TEXT] = {KT_INDEX(16), 0, 0, 0, 0},
        [KT_TOKEN_MUTED] = {KT_INDEX(245), 0, 0, 0, KT_DIM},
        [KT_TOKEN_SURFACE] = {KT_INDEX(255), 0, 0, 0, 0},
        [KT_TOKEN_SELECTION] = {KT_INDEX(24), 0, KT_INDEX(4), 0, KT_REVERSE},
        [KT_TOKEN_ON_SELECTION] = {KT_INDEX(231), 0, 0, KT_BOLD, 0},
        [KT_TOKEN_BORDER] = {KT_INDEX(67), 0, KT_INDEX(4), 0, 0},
        [KT_TOKEN_ACCENT] = {KT_INDEX(24), 0, KT_INDEX(4), 0, 0},
        [KT_TOKEN_SUCCESS] = {KT_INDEX(28), 0, 0, 0, 0},
        [KT_TOKEN_WARNING] = {KT_INDEX(130), 0, KT_INDEX(3), 0, 0}}}
};

const struct kt_theme *tui_theme(int theme) { return &themes[theme >= 0 && theme < 3 ? theme : 0]; }

/* The style of a tone; anything else is the base text. */
struct kt_style tone_style(int tone) {
    switch (tone) {
    case TONE_BORDER: return KT_BORDER;
    case TONE_TITLE: return KT_TITLE;
    case TONE_SELECTED: return KT_SELECTED;
    case TONE_WARNING: return KT_WARNING;
    case TONE_STRONG: return KT_STRONG;
    case TONE_SUCCESS: return KT_SUCCESS;
    case TONE_MUTED: return KT_MUTED;
    case TONE_FOCUS: return KT_FOCUS;
    default: return KT_BASE;
    }
}

void style(struct app *app, enum tone tone) { app->tone = (int)tone; }
const char *theme_name(int theme) { return theme_names[theme]; }
const char *dot(const struct app *app) { return app->ascii ? " / " : " \xc2\xb7 "; }

/* Append formatted text to a bounded buffer with the remaining size explicit. */
void text_append(char *buffer, size_t size, const char *format, ...) {
    size_t used = strlen(buffer);
    va_list args;
    if (used + 1U >= size) return;
    va_start(args, format);
    (void)vsnprintf(buffer + used, size - used, format, args);
    va_end(args);
}

/* Frame lifecycle. The canvas leaves the terminal's last column unused. The
 * interactive loop lends the frame it will present (app->target); otherwise
 * the frame owns cells sized for the screen being rendered. */
bool frame_alloc(struct app *app) {
    size_t columns = app->cols > 512 ? 512U : app->cols > 0 ? (size_t)app->cols : 1U;
    size_t rows = app->rows > 256 ? 256U : app->rows > 0 ? (size_t)app->rows : 1U;
    size_t needed = columns * rows;
    if (app->frame_cells && app->frame_capacity >= needed) return true;
    free(app->frame_cells);
    app->frame_cells = calloc(needed, sizeof(*app->frame_cells));
    app->frame_capacity = app->frame_cells ? needed : 0;
    return app->frame_cells != NULL;
}

void frame_free(struct app *app) {
    free(app->frame_cells);
    app->frame_cells = NULL; app->frame_capacity = 0;
}

static bool frame_canvas(struct app *app, int width, int height) {
    if (app->target) return kt_canvas_view(&app->frame, app->target, (struct kt_rect){0, 0, width, height});
    return frame_alloc(app) && kt_init(&app->frame, app->frame_cells, app->frame_capacity, width, height, !app->ascii);
}

bool frame_begin(struct app *app, bool headless) {
    int width = app->cols > 512 ? 511 : app->cols - 1;
    int height = app->rows > 256 ? 256 : app->rows;
    if (width < 1 || height < 1) return false;
    (void)headless;
    if (!frame_canvas(app, width, height)) return false;
    kt_clear(&app->frame, KT_BASE);
    app->line = 0; app->tone = TONE_BASE; app->tab_count = 0;
    app->content_x = 2; app->content_width = width - 4;
    if (app->content_width < 1) app->content_width = 1;
    return true;
}

/* Deterministic rendering prints the frame as text; the loop presents its own. */
void frame_end(struct app *app, unsigned frame, bool headless) {
    int y;
    if (!headless) return;
    printf("FRAME %u %dx%d\n", frame, app->cols, app->rows);
    for (y = 0; y < app->frame.height; y++) { (void)kt_write_row(&app->frame, y, stdout, NULL); putchar('\n'); }
    fflush(stdout);
}

/* A borrowed view of the remaining content rows, for canvas-based views. */
bool frame_content(struct app *app, struct kt_canvas *out) {
    struct kt_rect r = {app->content_x, app->line, app->content_width, app->limit - app->line};
    if (r.height < 1) return false;
    return kt_canvas_view(out, &app->frame, r);
}

static void text_at(struct app *app, int x, int width, struct kt_style tone, const char *text) {
    if (width < 1) return;
    kt_text(&app->frame, (struct kt_rect){x, app->line, width, 1}, text, tone);
}

/* Fixed-width column inside the content area. Text is clipped to width. */
void column(struct app *app, int x, int width, struct kt_style tone, const char *text) {
    text_at(app, app->content_x + x, width, tone, text);
}

/* One line of the content area, in a style. */
static void line_text(struct app *app, struct kt_style tone, const char *text) {
    if (app->line >= app->limit) return;
    text_at(app, app->content_x, app->content_width, tone, text);
    app->line++;
}

void linef(struct app *app, const char *format, ...) {
    char buffer[2048];
    va_list args;
    if (app->line >= app->limit) return;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    line_text(app, tone_style(app->tone), buffer);
}

/* Internal session tokens; user-facing labels come from status_label. */
const char *display_status(const struct head *head) {
    if (head->remote_host[0] != '\0') return "UNOBSERVED";
    if (strcmp(head->observed, "unavailable") == 0 || strcmp(head->liveness, "unavailable") == 0) return "UNAVAILABLE";
    if (strcmp(head->liveness, "stopped") == 0 &&
        (strcmp(head->observed, "running") == 0 || strcmp(head->observed, "idle") == 0)) return "STALE";
    if (strcmp(head->liveness, "live") == 0) return "LIVE";
    return "STOPPED";
}

const char *status_label(const struct head *head) {
    const char *status = display_status(head);
    if (!strcmp(status, "LIVE")) return "running";
    if (!strcmp(status, "STALE")) return "terminal gone";
    if (!strcmp(status, "UNAVAILABLE")) return "unknown";
    if (!strcmp(status, "UNOBSERVED")) return "remote";
    return "stopped";
}

struct kt_style status_tone(const struct head *head) {
    const char *status = display_status(head);
    if (!strcmp(status, "LIVE")) return KT_SUCCESS;
    if (!strcmp(status, "STOPPED")) return KT_MUTED;
    if (!strcmp(status, "UNOBSERVED")) return KT_BASE;
    return KT_WARNING;
}


static void record_hit(struct app *app, size_t index) {
    if (app->line >= app->limit || app->hit_count >= MAX_HEADS) return;
    app->hit_rows[app->hit_count] = app->line + 1;
    app->hit_bottom[app->hit_count] = app->line + 1;
    app->hit_left[app->hit_count] = 3;
    app->hit_right[app->hit_count] = app->cols - 3;
    app->hit_items[app->hit_count++] = index;
}

/* Recovery findings in plain language. Raw kinds stay in diagnostics. */
void recovery_explain(const struct recovery *item, char *title, size_t title_size, char *detail, size_t detail_size) {
    /* Detail formats consume the head label exactly once ("%.0s" when unused). */
    static const struct { const char *kind, *title, *detail; } known[] = {
        {"dead-session", "Terminal stopped: %s",
         "The terminal session for %s is no longer running. Its worktree and files are kept; nothing was removed from the repository. Open it from Work (a) to restart its terminal and agent in the same worktree, or remove the head once its work is finished."},
        {"stale-lock", "Leftover lock: %s",
         "An interrupted command left a state lock behind. No work is affected. The check below clears it safely.%.0s"},
        {"orphan-worktree", "Leftover worktree from a removed task: %s; no uncommitted changes",
         "Hydra created this worktree for a task whose record is gone. The check below previews the cleanup without deleting anything; x removes only this worktree after you confirm. The branch is kept.%.0s"},
        {"orphan-worktree-dirty", "Leftover worktree from a removed task: %s; has uncommitted changes",
         "Hydra created this worktree for a task whose record is gone, and it still has uncommitted changes, so Hydra keeps it. Review or commit the work there first; the check below previews the cleanup without deleting anything.%.0s"},
        {"teardown-failure", "Removal did not finish: %s",
         "Removing %s stopped part way. Remaining files and state are kept. The check below shows what is left so you can finish or keep it."},
        {"malformed-state", "Unreadable head record: %s",
         "Hydra could not read the saved state for %s. It is listed so nothing is dropped silently. The check below reports the exact problem."}
    };
    const char *kind = item->kind, *head = item->label;
    size_t i;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (strcmp(kind, known[i].kind)) continue;
        snprintf(title, title_size, known[i].title, head);
        snprintf(detail, detail_size, known[i].detail, head);
        return;
    }
    snprintf(title, title_size, "%s: %s", kind, head);
    snprintf(detail, detail_size, "Hydra reported this finding from %s.", item->source[0] ? item->source : "its state");
}

/* Wrap plain text into the content area. */
void paragraph(struct app *app, const char *text, struct kt_style tone) {
    size_t length = strlen(text), offset = 0;
    size_t width = (size_t)app->content_width;
    while (offset < length && app->line < app->limit) {
        char line[2048];
        size_t chunk = length - offset, cut;
        if (chunk > width) {
            chunk = width;
            for (cut = chunk; cut > width / 2; cut--) if (text[offset + cut] == ' ') { chunk = cut; break; }
        }
        snprintf(line, sizeof(line), "%.*s", (int)chunk, text + offset);
        line_text(app, tone, line);
        offset += chunk;
        while (offset < length && text[offset] == ' ') offset++;
    }
}

static struct transcript *view_transcript(struct app *app) {
    if (!app->transcript) app->transcript = transcript_new();
    return app->transcript;
}

void transcript_view(struct app *app, const char *text, size_t length, struct transcript_layout *layout) {
    struct kt_rect area = {app->content_x, app->line, app->content_width, app->limit - app->line};
    struct transcript *t = view_transcript(app);
    layout->color = !app->no_color;
    if (!t) { layout->rows = layout->drawn = 0; linef(app, "Output unavailable: out of memory"); return; }
    transcript_render(t, &app->frame, area, text, length, layout);
    app->line += (int)layout->drawn;
}

void transcript_view_reset(struct app *app) {
    if (view_transcript(app)) transcript_reset(app->transcript);
}

/* Printable ASCII cannot change rendition, so its rows follow from its length. */
static size_t plain_rows(const char *text, size_t length, int width) {
    size_t i, room = width > 2 ? (size_t)width : 2;
    for (i = 0; i < length; i++) if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126) return 0;
    while (length && text[length - 1] == ' ') length--;
    return length <= room ? 1 : 1 + (length - room + room - 2) / (room - 1);
}

size_t transcript_line_view(struct app *app, const char *text, size_t length, struct kt_style tone, size_t row, size_t scroll) {
    struct transcript *t = view_transcript(app);
    size_t rows = plain_rows(text, length, app->content_width);
    if (rows && (row + rows <= scroll || app->line >= app->limit)) return rows;
    if (!t) { if (row >= scroll) linef(app, "%.*s", (int)(length > 1024 ? 1024 : length), text); return 1; }
    transcript_parse(t, text, length);
    rows = transcript_rows(t, app->content_width);
    if (row + rows > scroll && app->line < app->limit)
        app->line += transcript_draw(t, &app->frame, (struct kt_rect){app->content_x, app->line, app->content_width,
            app->limit - app->line}, scroll > row ? scroll - row : 0, tone, !app->no_color);
    return rows;
}

void section(struct app *app, const char *label) {
    if (app->line < app->limit) linef(app, "");
    style(app, TONE_BORDER); linef(app, "%s", label); style(app, TONE_BASE);
}

void pair(struct app *app, const char *left_label, const char *left, struct kt_style left_tone,
                 const char *right_label, const char *right, struct kt_style right_tone) {
    if (app->line >= app->limit) return;
    if (app->content_width < 56) {
        column(app, 0, 11, KT_MUTED, left_label); column(app, 12, app->content_width - 12, left_tone, left); app->line++;
        if (app->line >= app->limit) return;
        column(app, 0, 11, KT_MUTED, right_label); column(app, 12, app->content_width - 12, right_tone, right); app->line++;
        return;
    }
    {
        int half = app->content_width / 2;
        column(app, 0, 11, KT_MUTED, left_label); column(app, 12, half - 13, left_tone, left);
        column(app, half, 11, KT_MUTED, right_label); column(app, half + 12, app->content_width - half - 12, right_tone, right);
        app->line++;
    }
}

/* Shared chrome: title row, tab row, footer rows. Tab hits are recorded for the mouse. */
static const char *view_titles[] = {"WORK", "DETAILS", "COORDINATION", "RECOVERY", "OVERVIEW", "WORKFLOWS", "HOSTS", "WORKSPACE", "STATISTICS", "ATTENTION"};
static const char *tab_labels[] = {"Work", "Details", "Coordination", "Recovery", "Overview", "Workflows", "Hosts", "Workspace", "Statistics", "Attention"};
/* Below TAB_SHORT_BELOW columns the tab bar uses these so every tab, Hosts
 * included, fits at 80 columns; keys and view titles are unchanged. */
static const char *tab_short[] = {"Work", "Details", "Coord", "Recovery", "Overview", "Flows", "Hosts", "Workspace", "Stats", "Attention"};
#define TAB_SHORT_BELOW 100

size_t tab_order(const struct app *app, int *views) {
    size_t count = 0;
    views[count++] = 0; views[count++] = 1;
    if (app->view == 2) views[count++] = 2;
    views[count++] = 4;
    if (app->fleet) views[count++] = 6;
    views[count++] = 9; views[count++] = 3;
    if (!app->fleet) views[count++] = 5;
    views[count++] = 8; views[count++] = 7;
    /* Local work keeps its tab numbers; Hosts adds and continues remote setup. */
    if (!app->fleet) views[count++] = 6;
    return count;
}

static int tab_width(const char *const *labels, int view, bool current) {
    return (int)strlen(labels[view]) + (current ? 2 : 0);
}

/* First tab to draw so the active tab stays visible in the available width. */
static size_t tabs_first_visible(const char *const *labels, const int *views, size_t active, int gap, int width) {
    size_t first = 0, i;
    while (first < active) {
        int needed = 0;
        for (i = first; i <= active; i++) needed += tab_width(labels, views[i], i == active) + gap;
        if (needed <= width) break;
        first++;
    }
    return first;
}

static void record_tab(struct app *app, int x, int length, int view) {
    if (app->tab_count >= 12) return;
    app->tab_left[app->tab_count] = x; app->tab_right[app->tab_count] = x + length - 1;
    app->tab_view[app->tab_count++] = view;
}

static void chrome_tabs(struct app *app, struct kt_canvas *c, int row) {
    int views[12], gap = 2, x = 1, total = 0, width = c->width - 1;
    const char *const *labels = app->cols < TAB_SHORT_BELOW ? tab_short : tab_labels;
    size_t count = tab_order(app, views), i, first, active = 0;
    for (i = 0; i < count; i++) { total += tab_width(labels, views[i], views[i] == app->view); if (views[i] == app->view) active = i; }
    /* Tabs start at column x, so they have width - x columns. */
    if (total + (int)(count - 1) * gap > width - x) gap = 1;
    app->tab_count = 0;
    first = tabs_first_visible(labels, views, active, gap, width - x);
    for (i = first; i < count; i++) {
        char label[32];
        bool current = views[i] == app->view;
        int length = tab_width(labels, views[i], current);
        if (x + length > width) { kt_text(c, (struct kt_rect){x, row, width - x, 1}, "..", KT_MUTED); break; }
        snprintf(label, sizeof(label), current ? "[%s]" : "%s", labels[views[i]]);
        kt_text(c, (struct kt_rect){x, row, length, 1}, label, current ? KT_SELECTED : KT_MUTED);
        record_tab(app, x, length, views[i]);
        x += length + gap;
    }
}

static void header_project(const struct app *app, char *project, size_t size) {
    const char *name;
    project[0] = '\0';
    if (!app->links || !app->links->root[0]) return;
    name = strrchr(app->links->root, '/');
    snprintf(project, size, "%.60s%s", name && name[1] ? name + 1 : app->links->root, dot(app));
}

/* Counts describe tasks the user started; heads a run created are counted
 * once, inside their run. */
static void header_summary(const struct app *app, const char *project, char *right, size_t size) {
    const char *sep = dot(app);
    struct work_counts n;
    if (app->fleet) { snprintf(right, size, "%sFleet%s%zu remote heads%s%zu hosts", project, sep, app->model.head_count, sep, app->model.host_count); return; }
    if (!app->model.head_count) { snprintf(right, size, "%sLocal%sno agent work yet", project, sep); return; }
    work_counts(app, &n);
    snprintf(right, size, "%sLocal%s%zu head%s", project, sep, n.heads, n.heads == 1 ? "" : "s");
    if (n.run_heads) text_append(right, size, " (+%zu in runs)", n.run_heads);
    text_append(right, size, "%s%zu running%s%zu need attention%s%zu to repair", sep, n.running, sep, n.attention, sep, app->model.recovery_count);
}

/* Display columns of UTF-8 text counting one per scalar (chrome uses narrow glyphs). */
static int text_columns(const char *text) {
    int length = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) { if ((*cursor & 0xc0U) != 0x80U) length++; cursor++; }
    return length;
}

void chrome_header(struct app *app, struct kt_canvas *c, const char *title) {
    char left[256], right[256] = "", project[128];
    int width = c->width, length;
    snprintf(left, sizeof(left), "HYDRA / %s", title);
    kt_text(c, (struct kt_rect){1, 0, width - 2, 1}, left, KT_TITLE);
    if (width >= 60) {
        header_project(app, project, sizeof(project));
        header_summary(app, project, right, sizeof(right));
        length = text_columns(right);
        if (length + (int)strlen(left) + 4 < width)
            kt_text(c, (struct kt_rect){width - length - 1, 0, length, 1}, right, KT_MUTED);
    }
    chrome_tabs(app, c, 1);
}

/* A partly entered chord takes the status line until it completes: its keys
 * so far and the keys that may follow. */
void chrome_footer(struct app *app, struct kt_canvas *c, const char *status, struct kt_style tone, const char *hints) {
    char pending[512];
    int height = c->height;
    if (key_pending_text(app, pending, sizeof(pending))) {
        status = pending; tone = KT_STRONG;
        if (height < 3) hints = pending;
    }
    if (height >= 3) kt_text(c, (struct kt_rect){1, height - 2, c->width - 2, 1}, status, tone);
    if (hints) kt_text(c, (struct kt_rect){1, height - 1, c->width - 2, 1}, hints, KT_MUTED);
    else key_footer(app, c, (struct kt_rect){1, height - 1, c->width - 2, 1});
}

struct head_columns { int status_x, agent_x, reported_x, name_width; bool wide, medium; };

static void head_row_layout(int width, struct head_columns *k) {
    k->wide = width >= 95; k->medium = width >= 65;
    k->reported_x = width - 12;
    k->agent_x = k->wide ? k->reported_x - 16 : width - 15;
    k->status_x = (k->wide || k->medium ? k->agent_x : width) - 15;
    if (k->status_x < 20) k->status_x = 20;
    k->name_width = k->status_x - 4;
}

static void outline_heading(const struct app *app, struct outline_text *t) {
    copy_text(t->name, sizeof(t->name), "HEAD");
    t->status = app->fleet ? "DESIRED" : "STATUS"; t->agent = app->fleet ? "HOST" : "AGENT";
    t->reported = "REPORTED"; t->tone = KT_MUTED;
}

static bool outline_marked(const struct app *app, const struct outline_row *row) {
    return row && row->kind == OUTLINE_HEAD && marked_index(app, app->model.heads[row->head].branch) < app->marked_count;
}

static struct kt_style outline_tone(const struct outline_row *row, bool selected) {
    if (!row || row->kind == OUTLINE_RETIRED) return KT_MUTED;
    return selected ? KT_SELECTED : KT_BASE;
}

/* Work list: aligned columns, status tone only on the status cell. A NULL
 * row draws the column headings. */
static void outline_line(struct app *app, const struct outline_row *row, bool selected) {
    int width = app->content_width;
    struct head_columns k;
    struct outline_text t;
    char cell[TEXT + 96];
    struct kt_style row_tone = outline_tone(row, selected);
    if (app->line >= app->limit) return;
    head_row_layout(width, &k);
    if (row) outline_row_text(app, row, &t);
    else outline_heading(app, &t);
    snprintf(cell, sizeof(cell), "%c%c %s", selected ? '>' : ' ', outline_marked(app, row) ? '*' : ' ', t.name);
    if (selected) column(app, 0, width, KT_SELECTED, "");
    column(app, 0, k.name_width + 3, row_tone, cell);
    column(app, k.status_x, 14, selected ? KT_SELECTED : t.tone, t.status);
    if (k.medium || k.wide) column(app, k.agent_x, k.wide ? 15 : 14, row_tone, t.agent);
    if (k.wide && !app->fleet) column(app, k.reported_x, 12, row_tone, t.reported);
    app->line++;
}

void render_empty_work(struct app *app) {
    linef(app, "");
    style(app, TONE_STRONG);
    linef(app, app->search[0] ? "No heads match the search." : app->fleet ? "No remote heads." : "No agent work in this project yet.");
    style(app, TONE_BASE);
    linef(app, "");
    if (app->search[0]) { paragraph(app, "Esc clears the search.", KT_BASE); return; }
    if (app->fleet) { paragraph(app, "Hosts shows connectivity for each machine; Recovery lists host problems. In Hosts, A adds a machine.", KT_BASE); return; }
    paragraph(app, "A head is one piece of work: its own branch and worktree, a terminal, and an agent working inside it. You can follow several heads here at once.", KT_BASE);
    linef(app, "");
    paragraph(app, "Press n to start one: Hydra creates the branch and worktree, opens the terminal and starts your agent in it. Press : for other actions.", KT_BASE);
    linef(app, "");
    paragraph(app, "To run agents on another machine, press H for Hosts and add it there.", KT_MUTED);
}

/* One-panel summary of the selected head under the list. */
static void summary_line(struct app *app, const struct head *head, char *line, size_t size) {
    const char *sep = dot(app);
    char agent[TEXT + 96];
    if (app->fleet) {
        snprintf(line, size, "Host %.100s%sdesired %.40s%sproject %.300s", head->remote_host, sep, head->desired, sep, head->remote_project);
        return;
    }
    head_agent_text(app, head, agent, sizeof(agent));
    if (!head->head_id[0]) {
        snprintf(line, size, "Agent %s%ssession %s%sno head record; counters unknown", agent, sep, status_label(head), sep);
        return;
    }
    snprintf(line, size, "%s%s%s%s%u changed file%s", agent, sep,
             head_headless(head) ? "headless (no terminal)" : status_label(head), sep, head->diff, head->diff == 1 ? "" : "s");
    if (head->gates) text_append(line, size, "%s%u of %u approval requests approved", sep, head->approved, head->gates);
    if (head->declared[0]) text_append(line, size, "%sreported %.63s", sep, head->declared);
}

static const char *open_action(const struct app *app, const struct head *head) {
    return head_terminal_gone(app, head) ? "restart its terminal" : "talk to the agent";
}

static void summary_actions(struct app *app, const struct head *head, char *line, size_t size) {
    const char *sep = dot(app);
    if (app->fleet) snprintf(line, size, "Enter details%sa attach to terminal%sc interrupt", sep, sep);
    else if (!head->head_id[0]) snprintf(line, size, "Enter details%sx remove", sep);
    else if (head_headless(head)) snprintf(line, size, "Enter details%sp step output%sx %s%s: more actions", sep, sep,
                                           head_run_role(app, head) && !strcmp(head_run_role(app, head), "worker") ? "dismiss (keeps the branch)" : "remove", sep);
    else snprintf(line, size, "Enter details%sa %s%sx remove%s: more actions", sep, open_action(app, head), sep, sep);
}

static void summary_panel(struct app *app, const char *title, const char *first, const char *second) {
    struct kt_rect r = {app->content_x - 1, app->line + 1, app->content_width + 2, app->limit - app->line - 1};
    if (r.height > 5) r.height = 5;
    kt_panel_styled(&app->frame, r, title, KT_BORDER, KT_STRONG);
    app->line = r.y + 1;
    app->content_x++; app->content_width -= 2;
    paragraph(app, first, KT_BASE);
    if (app->line < r.y + r.height - 1) { style(app, TONE_MUTED); linef(app, "%s", second); style(app, TONE_BASE); }
    app->content_x--; app->content_width += 2;
    app->line = r.y + r.height;
}

static void render_selected_summary(struct app *app) {
    char first[1024], second[256], title[TEXT + 64];
    const struct head *head = selected_head(app);
    if (app->run_row && app->workflows && app->workflow_run < app->workflows->run_count) {
        run_summary_text(app, app->workflow_run, title, sizeof(title));
        run_next_text(app, app->workflow_run, first, sizeof(first));
        snprintf(second, sizeof(second), "Enter %s%sC monitor%sw workflows%so overview",
                 run_expanded(app, app->workflow_run) ? "collapse" : "expand", dot(app), dot(app), dot(app));
        summary_panel(app, title, first, second);
        return;
    }
    if (!head) return;
    summary_line(app, head, first, sizeof(first));
    summary_actions(app, head, second, sizeof(second));
    summary_panel(app, head->branch, first, second);
}

static void list_footer(struct app *app, size_t position, size_t count) {
    struct work_counts n;
    work_counts(app, &n);
    style(app, TONE_MUTED);
    if (n.run_heads) linef(app, "%zu head%s you started%s%zu in runs  |  Row %zu of %zu", n.heads, n.heads == 1 ? "" : "s", dot(app), n.run_heads, position + 1U, count);
    else linef(app, "%zu heads  |  Row %zu of %zu", n.heads, position + 1U, count);
    style(app, TONE_BASE);
}

static void list_rows(struct app *app, const struct outline_row *rows, size_t count, size_t position, int available) {
    size_t index, start = position >= (size_t)available ? position - (size_t)available + 1U : 0U;
    outline_line(app, NULL, false);
    for (index = start; index < count && available-- > 0; index++) {
        if (rows[index].kind == OUTLINE_HEAD) record_hit(app, rows[index].head);
        else if (rows[index].kind == OUTLINE_RUN) record_hit(app, MAX_HEADS + 1U + rows[index].run);
        outline_line(app, &rows[index], index == position);
    }
}

static void render_list(struct app *app) {
    static struct outline_row rows[OUTLINE_ROWS];
    size_t count, position;
    int available = app->limit - app->line - 2;
    bool summary = app->rows >= 20 && app->cols >= 70;
    if (summary && available > app->rows / 3) available = app->rows / 3;
    retarget_selection(app);
    count = outline_build(app, rows, OUTLINE_ROWS);
    position = outline_position(app, rows, count);
    if (position == count && app->run_row) { app->run_row = false; retarget_selection(app); position = outline_position(app, rows, count); }
    if (count == 0U) { render_empty_work(app); return; }
    list_rows(app, rows, count, position, available < 1 ? 1 : available);
    list_footer(app, position < count ? position : 0, count);
    if (summary && app->limit - app->line >= 5) render_selected_summary(app);
}

static void render_coordination(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { render_empty_work(app); return; }
    style(app, TONE_STRONG); linef(app, "COORDINATION  %s", head->branch); style(app, TONE_BASE);
    if (app->fleet) {
        paragraph(app, "Remote heads are coordinated on their host. Use hydra fleet workflow to inspect assignments there.", KT_BASE);
        return;
    }
    linef(app, "");
    paragraph(app, "This head is a single agent session. Coordination between heads uses claims (files a head intends to change), scopes (areas it stays within), a shared work queue and allocated resources. A workflow adds steps, dependencies and handoffs on top.", KT_BASE);
    section(app, "THIS HEAD");
    if (!head->head_id[0]) { linef(app, "Counters unknown: there is no readable head record for this branch."); }
    else {
        linef(app, "Claims      %u recorded             : claims", head->claims);
        linef(app, "Scopes      %u recorded             : scopes", head->scopes);
        linef(app, "Queue       %u waiting entries      : queue", head->queue);
        linef(app, "Resources   %u allocated            : resources", head->resources);
        linef(app, "Approvals   %u of %u approved        : approvals", head->approved, head->gates);
    }
    section(app, "WORKFLOW");
    if (app->workflows && app->workflows->run_count)
        linef(app, "%zu recorded workflow run(s) in this project; Workflows shows steps and dependencies.", app->workflows->run_count);
    else linef(app, "No workflow run is linked to this head. A single conversation does not need one.");
}

static void render_recovery_detail(struct app *app) {
    const struct recovery *item = &app->model.recovery[app->recovery_selected];
    char title[TEXT + 64], detail[1024];
    recovery_explain(item, title, sizeof(title), detail, sizeof(detail));
    /* Wrapped so long labels, such as a leftover worktree's path, stay readable. */
    paragraph(app, title, KT_STRONG);
    paragraph(app, detail, KT_BASE);
    linef(app, "");
    linef(app, "Check: %s", item->action);
    style(app, TONE_MUTED);
    linef(app, "Kind: %s   Source: %s   Confidence: %s", item->kind, item->source, item->confidence);
    linef(app, "Inspect: %s", item->action);
    linef(app, "Enter runs the check and shows its output here; d returns to the list");
    if (!strcmp(item->kind, "orphan-worktree")) linef(app, "x removes this worktree after confirmation; the branch is kept");
    style(app, TONE_BASE);
}

static void render_recovery_row(struct app *app, size_t index) {
    const struct recovery *item = &app->model.recovery[index];
    char title[TEXT + 64], detail[1024];
    bool selected = index == app->recovery_selected, wide = app->content_width >= 100;
    recovery_explain(item, title, sizeof(title), detail, sizeof(detail));
    record_hit(app, index);
    if (selected) column(app, 0, app->content_width, KT_SELECTED, ">");
    column(app, 2, wide ? app->content_width - 22 : app->content_width - 2, selected ? KT_SELECTED : KT_WARNING, title);
    if (wide) column(app, app->content_width - 18, 18, selected ? KT_SELECTED : KT_MUTED, item->kind);
    app->line++;
}

static void render_recovery(struct app *app) {
    size_t index, start;
    int available = app->limit - app->line - 2;
    style(app, TONE_STRONG); linef(app, "RECOVERY  %zu finding%s", app->model.recovery_count, app->model.recovery_count == 1 ? "" : "s"); style(app, TONE_BASE);
    if (app->model.recovery_count == 0U) {
        linef(app, "");
        paragraph(app, "Nothing needs repair. Findings appear here when a terminal stops unexpectedly, a removal does not finish, or saved state cannot be read.", KT_BASE);
        return;
    }
    if (app->recovery_selected >= app->model.recovery_count) app->recovery_selected = 0U;
    if (app->diagnostics) { render_recovery_detail(app); return; }
    if (available < 1) available = 1;
    start = app->recovery_selected >= (size_t)available ? app->recovery_selected - (size_t)available + 1U : 0U;
    for (index = start; index < app->model.recovery_count && available-- > 0; index++) render_recovery_row(app, index);
    style(app, TONE_MUTED);
    linef(app, "%zu of %zu", app->recovery_selected + 1U, app->model.recovery_count);
    style(app, TONE_BASE);
}

/* The agent-pane commands with the chords bound to them now, and where to
 * change them. A keys file that was not applied as written is named. */
/* Help for where the user is, generated from the binding table: the keys
 * file and any problem with it first, then every key of the focused chain
 * that applies, grouped, then the chords that work while typing to an agent.
 * It scrolls with j / k, the arrows and the page keys. */
static void render_help(struct app *app) {
    const struct native_keys *keys = native_keys(app);
    struct kt_canvas view;
    struct kt_rect area;
    int rows, total;
    style(app, TONE_STRONG); linef(app, "KEYBOARD HELP"); style(app, TONE_BASE);
    style(app, TONE_MUTED);
    linef(app, "Keys file %s; hydra tui --print-keys lists every key.", keys->path[0] ? keys->path : "(none)");
    style(app, TONE_BASE);
    if (keys->problem[0]) paragraph(app, keys->problem, KT_WARNING);
    if (app->line < app->limit) linef(app, "");
    area = (struct kt_rect){app->content_x, app->line, app->content_width, app->limit - app->line};
    if (area.height < 1 || area.width < 1) return;
    total = key_help(app, NULL, area, 0);
    rows = area.height;
    if (app->help_scroll > total - rows) app->help_scroll = total > rows ? total - rows : 0;
    if (!kt_canvas_view(&view, &app->frame, area)) return;
    (void)key_help(app, &view, (struct kt_rect){0, 0, area.width, area.height}, app->help_scroll);
    app->help_more = total > rows;
    app->line = app->limit;
}

void help_scroll(struct app *app, int rows) {
    app->help_scroll += rows;
    if (app->help_scroll < 0) app->help_scroll = 0;
}

/* Command output wraps instead of clipping; j/k scroll by wrapped rows. */
static void render_result(struct app *app) {
    struct transcript_layout layout;
    style(app, TONE_BASE);
    if (!app->result_text[0]) { linef(app, "No output."); return; }
    memset(&layout, 0, sizeof(layout));
    layout.scroll = app->result_scroll;
    transcript_view(app, app->result_text, strlen(app->result_text), &layout);
    app->result_scroll = layout.scroll;
}

static void render_content(struct app *app) {
    if (app->view == 6 && native_setup_active(app)) { native_setup_render(app); return; }
    if (app->result_open) { render_result(app); return; }
    if (app->help) { render_help(app); return; }
    switch (app->view) {
        case 0: render_list(app); break;
        case 1: render_head_detail(app); break;
        case 2: render_coordination(app); break;
        case 3: render_recovery(app); break;
        case 4: render_dashboard(app); break;
        case 5: render_workflow_graph(app); break;
        case 6: render_hosts(app); break;
        case 9: render_attention(app); break;
        default: linef(app, "Workspace needs at least 20 columns and 6 rows"); break;
    }
}

static bool render_workspace_view(struct app *app, unsigned frame, bool headless) {
    if (app->view == 8) return render_statistics(app, frame, headless);
    if (app->view == 7) return render_native_workspace(app, frame, headless);
    return false;
}

/* Each successful refresh takes a new snapshot, so while refreshes keep
 * succeeding an age only restarts every interval and says nothing: a steady
 * "Live" shows instead. The age counts up once the shown data is older than
 * two refresh intervals (plus one remote request in fleet mode), or when the
 * last refresh failed. */
static long snapshot_age(time_t observed_at, bool headless) {
    long age = headless || !observed_at ? 0L : (long)(time(NULL) - observed_at);
    return age < 0 ? 0L : age;
}
static long snapshot_limit(const struct app *app) {
    return 2L * HYDRA_TUI_REFRESH_SECONDS + (app->fleet ? (long)HYDRA_FLEET_TUI_REQUEST_SECONDS : 0L);
}
struct kt_style snapshot_freshness(const struct app *app, time_t observed_at, bool stale, bool headless, char *out, size_t size) {
    long age = snapshot_age(observed_at, headless), limit = snapshot_limit(app);
    if (!headless && observed_at && (stale || age > limit)) clock_mark();
    if (stale && observed_at) { snprintf(out, size, "STALE: last good snapshot, updated %lds ago", age); return KT_WARNING; }
    if (stale) { snprintf(out, size, "STALE: no snapshot yet"); return KT_WARNING; }
    if (!observed_at && !headless) { snprintf(out, size, "Waiting for the first snapshot"); return KT_MUTED; }
    if (age > limit) { snprintf(out, size, "Updated %lds ago", age); return KT_BASE; }
    snprintf(out, size, "Live");
    return KT_MUTED;
}

static void status_marked(struct app *app, char *out, size_t size) {
    const char *sep = dot(app);
    snprintf(out, size, "%zu marked%sx remove%sG group%sSpace unmark%s%s", app->marked_count, sep, sep, sep,
             app->notice[0] ? sep : "", app->notice);
}

static void status_line(struct app *app, char *out, size_t size, struct kt_style *tone, bool headless) {
    bool stale = app->view == 5 ? app->workflow_stale : app->snapshot_stale;
    const char *sep = dot(app);
    char fresh[96];
    struct kt_style fresh_tone = snapshot_freshness(app, app->view == 5 ? app->workflow_at : app->snapshot_at, stale, headless, fresh, sizeof(fresh));
    *tone = KT_MUTED;
    if (app->marked_count) { status_marked(app, out, size); *tone = KT_STRONG; return; }
    if (stale && app->snapshot_error[0]) { snprintf(out, size, "%s%s%s", fresh, sep, app->snapshot_error); *tone = KT_WARNING; return; }
    if (app->notice[0]) { snprintf(out, size, "%s", app->notice); *tone = KT_BASE; return; }
    if (app->search[0]) { snprintf(out, size, "Search: %s%sEsc clears", app->search, sep); *tone = KT_BASE; return; }
    snprintf(out, size, "%s", fresh);
    *tone = fresh_tone;
}

/* The footer is generated from the binding table (key_footer); the remote
 * setup flow keeps its own hints while it owns input. NULL: generated. */
static const char *hint_line(struct app *app) {
    if (app->view == 6 && native_setup_active(app)) return native_setup_hints(app, app->cols < 70);
    return NULL;
}

static void detail_title(const struct app *app, const struct head *head, char *out, size_t size) {
    const char *label = app->diagnostics ? "Technical details" : "Details";
    if (!head) { snprintf(out, size, "%s", label); return; }
    snprintf(out, size, "%s: %.200s", label, head->branch);
}

static const char *panel_title(struct app *app, char *out, size_t size) {
    static const char *names[] = {NULL, NULL, "Coordination", "Recovery", "Overview", "Workflows", "Hosts", NULL, NULL, "Attention"};
    const struct head *head = selected_head(app);
    if (app->view == 6 && native_setup_active(app)) return native_setup_title(app);
    if (app->result_open) return app->result_title;
    if (app->help) return "HELP";
    if (app->view == 0) snprintf(out, size, "%s", app->fleet ? "Remote heads" : "Heads in this project");
    else if (app->view == 1) detail_title(app, head, out, size);
    else snprintf(out, size, "%s", names[app->view] ? names[app->view] : tab_labels[app->view]);
    return out;
}

void render(struct app *app, unsigned frame, bool headless) {
    char status[512], title[TEXT];
    struct kt_style tone;
    app->hit_count = 0U;
    app->hit_cols = app->cols; app->hit_height = app->rows; app->hit_view = app->view;
    app->hit_tabs = true;
    if (!app->help && !app->diagnostics && !app->result_open) {
        if (render_workspace_view(app, frame, headless)) return;
    }
    if (!frame_begin(app, headless)) return;
    app->limit = app->rows - 3;
    chrome_header(app, &app->frame, view_titles[app->view]);
    if (app->rows >= 6) kt_panel_styled(&app->frame, (struct kt_rect){0, 2, app->frame.width, app->frame.height - 4},
                                        panel_title(app, title, sizeof(title)), KT_BORDER, KT_STRONG);
    app->line = 3;
    app->help_more = false;
    style(app, TONE_BASE);
    render_content(app);
    app->limit = app->rows;
    status_line(app, status, sizeof(status), &tone, headless);
    chrome_footer(app, &app->frame, status, tone, hint_line(app));
    frame_end(app, frame, headless);
}
