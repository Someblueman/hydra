#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
#include "attention.h"
#include "inbox.h"
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

/* The tone a stock style is drawn with; anything else is the base tone. */
enum tone style_tone(struct kt_style s) {
    int tone;
    for (tone = TONE_BASE; tone <= TONE_FOCUS; tone++) if (kt_style_equal(tone_style(tone), s)) return (enum tone)tone;
    return TONE_BASE;
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

static void overlays(struct app *app);

/* A pane's title: a rule on the first row of its area, the title at its
 * left, nothing around the sides. Its content keeps the box's inside. */
void pane_rule(struct kt_canvas *c, struct kt_rect r, const char *title) {
    pane_rule_styled(c, r, title, false);
}

void pane_rule_styled(struct kt_canvas *c, struct kt_rect r, const char *title, bool focused) {
    char text[TEXT + 96];
    if (r.width < 2 || r.height < 1) return;
    snprintf(text, sizeof(text), "%s", title ? title : "");
    kt_rule_styled(c, (struct kt_rect){r.x, r.y, r.width - 1, 1}, text[0] ? text : NULL, KT_ALIGN_LEFT,
                   focused ? KT_FOCUS : KT_STRONG, focused ? KT_FOCUS : KT_BORDER);
}

/* Deterministic rendering prints the frame as text; the loop presents its own. */
void frame_end(struct app *app, unsigned frame, bool headless) {
    int y;
    overlays(app);
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
         "Hydra could not read the saved state for %s. It is listed so nothing is dropped silently. The check below reports the exact problem."},
        {"fleet-unavailable", "Remote hosts cannot be observed: %s",
         "Remote hosts are registered (%s), but the optional hydra-fleet helper is not built or installed, so their work cannot appear here. Nothing on those hosts is affected. The check below builds the helper."},
        {"observation", "Host did not answer: %s",
         "The latest remote observation could not reach host %s. Rows shown for it are its last observation, with their age; nothing there was stopped or completed. r retries the host."}
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
static const char *view_titles[] = {"WORK", "DETAILS", "COORDINATION", "INBOX", "OVERVIEW", "WORKFLOWS", "HOSTS", "WORKSPACE", "STATISTICS", "INBOX"};
static const char *tab_labels[] = {"Work", "Details", "Coordination", "Inbox", "Overview", "Workflows", "Hosts", "Workspace", "Statistics", "Inbox"};
/* Below TAB_SHORT_BELOW columns the tab bar uses these so every tab, Hosts
 * included, fits at 80 columns; keys and view titles are unchanged. */
static const char *tab_short[] = {"Work", "Details", "Coord", "Inbox", "Overview", "Flows", "Hosts", "Workspace", "Stats", "Inbox"};
#define TAB_SHORT_BELOW 140

/* One tab order for local and remote work; Hosts lists the machines, adds
 * one and continues its setup. */
size_t tab_order(const struct app *app, int *views) {
    size_t count = 0;
    views[count++] = 0; views[count++] = 1;
    if (app->view == 2) views[count++] = 2;
    views[count++] = 4;
    views[count++] = 9;
    views[count++] = 5;
    views[count++] = 8; views[count++] = 7;
    views[count++] = 6;
    return count;
}

static void record_tab(struct app *app, int x, int length, int view) {
    if (app->tab_count >= 12) return;
    app->tab_left[app->tab_count] = x; app->tab_right[app->tab_count] = x + length - 1;
    app->tab_view[app->tab_count++] = view;
}

/* Inbox's tab carries a badge: how many items wait (warning; danger when one
 * is a failure), or a dot until the first read answered. */
static int tab_badge(struct app *app, int view, char *out, size_t size, struct kt_style *tone) {
    size_t count;
    bool failure, known;
    out[0] = '\0';
    if (view != 9) return 0;
    inbox_badge(app, &count, &failure, &known);
    if (!known) snprintf(out, size, "%s", app->ascii ? "." : "\xc2\xb7");
    else if (count) snprintf(out, size, "%zu", count);
    *tone = KT_STYLE(failure ? KT_COLOR_DANGER : KT_COLOR_WARNING, KT_COLOR_SURFACE, KT_BOLD);
    return out[0] ? kt_text_width(out) + 1 : 0;
}

/* A tab is "N Label": its number (the key that opens it) and its label; the
 * active one is a pill with a blank on each side. */
static int tab_width(struct app *app, const char *const *labels, int view, bool current, int number) {
    char badge[24];
    struct kt_style tone;
    return (int)strlen(labels[view]) + (number < 10 ? 2 : 0) + (current ? 2 : 0) + tab_badge(app, view, badge, sizeof(badge), &tone);
}

static void tab_draw(struct app *app, struct kt_canvas *c, int x, int row, int view, bool current, int number,
                     const char *const *labels) {
    char label[32], badge[24];
    struct kt_style tone = KT_WARNING;
    int length = tab_width(app, labels, view, current, number), extra = tab_badge(app, view, badge, sizeof(badge), &tone);
    if (extra) kt_text(c, (struct kt_rect){x + length - extra + 1, row, extra - 1, 1}, badge, tone);
    length -= extra;
    if (current) {
        snprintf(label, sizeof(label), " %d %s ", number, labels[view]);
        /* Without colour the pill is reverse video, as a selection is. */
        kt_text(c, (struct kt_rect){x, row, length, 1}, label, app->no_color ?
                KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, KT_BOLD) : KT_STYLE(KT_COLOR_ON_ACCENT, KT_COLOR_ACCENT, KT_BOLD));
    } else {
        snprintf(label, sizeof(label), "%d", number);
        kt_text(c, (struct kt_rect){x, row, 1, 1}, label, KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0));
        kt_text(c, (struct kt_rect){x + 2, row, length - 2, 1}, labels[view], KT_MUTED);
    }
    record_tab(app, x, length + extra, view);
}

/* The first tab drawn, so the active one fits in room columns. */
static size_t tabs_first(struct app *app, const char *const *labels, const int *views, size_t active, int gap, int room) {
    size_t first = 0, i;
    while (first < active) {
        int needed = 0;
        for (i = first; i <= active; i++) needed += tab_width(app, labels, views[i], i == active, (int)i + 1) + gap;
        if (needed <= room) break;
        first++;
    }
    return first;
}

/* The tabs after the brand; they give up their gap, then their long labels,
 * then the tabs far from the active one, before the active tab would go. */
static int chrome_tabs(struct app *app, struct kt_canvas *c, int row, int x, int width) {
    int views[12], gap = 2, total = 0;
    const char *const *labels = app->cols < TAB_SHORT_BELOW ? tab_short : tab_labels;
    size_t count = tab_order(app, views), i, first = 0, active = 0;
    for (i = 0; i < count; i++) { total += tab_width(app, labels, views[i], views[i] == app->view, (int)i + 1); if (views[i] == app->view) active = i; }
    if (total + (int)(count - 1) * gap > width - x) gap = 1;
    app->tab_count = 0;
    first = tabs_first(app, labels, views, active, gap, width - x);
    for (i = first; i < count; i++) {
        int length = tab_width(app, labels, views[i], views[i] == app->view, (int)i + 1);
        if (x + length > width) { kt_text(c, (struct kt_rect){x, row, width - x, 1}, app->ascii ? ".." : "\xe2\x80\xa6", KT_MUTED); break; }
        tab_draw(app, c, x, row, views[i], views[i] == app->view, (int)i + 1, labels);
        x += length + gap;
    }
    return x;
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
static void header_remote(const struct app *app, char *right, size_t size) {
    const char *sep = dot(app);
    struct remote_counts r;
    remote_counts(app, &r);
    if (!r.hosts && !r.heads && !r.tasks) return;
    text_append(right, size, "%sRemote%s%zu head%s", sep, sep, r.heads, r.heads == 1 ? "" : "s");
    if (r.tasks) text_append(right, size, ", %zu task%s", r.tasks, r.tasks == 1 ? "" : "s");
    if (r.hosts) text_append(right, size, " on %zu host%s", r.hosts, r.hosts == 1 ? "" : "s");
    if (app->remote_stale) text_append(right, size, " (stale)");
    else if (r.hosts_down) text_append(right, size, " (%zu disconnected)", r.hosts_down);
}

static void header_summary(const struct app *app, const char *project, char *right, size_t size) {
    const char *sep = dot(app);
    struct work_counts n;
    work_counts(app, &n);
    if (!n.heads && !n.run_heads) snprintf(right, size, "%sLocal%sno agent work yet", project, sep);
    else {
        snprintf(right, size, "%sLocal%s%zu head%s", project, sep, n.heads, n.heads == 1 ? "" : "s");
        if (n.run_heads) text_append(right, size, " (+%zu in runs)", n.run_heads);
        text_append(right, size, "%s%zu running%s%zu need attention%s%zu to repair", sep, n.running, sep, n.attention, sep, app->model.recovery_count);
    }
    header_remote(app, right, size);
}

/* Display columns of UTF-8 text counting one per scalar (chrome uses narrow glyphs). */
static int text_columns(const char *text) {
    int length = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) { if ((*cursor & 0xc0U) != 0x80U) length++; cursor++; }
    return length;
}

/* The top bar: the brand, the views as numbered tabs (the active one a
 * pill) and at the right what is shown: the view's title, which names the
 * workspace layout, followed by the project's summary when there is room.
 * The title keeps its place; the tabs give way before it. The row under the
 * bar is left blank. */
void chrome_header(struct app *app, struct kt_canvas *c, const char *title) {
    char right[384] = "", summary[256] = "", project[128];
    int width = c->width, length, end, room;
    kt_text(c, (struct kt_rect){1, 0, 8, 1}, app->ascii ? "# hydra" : "\xe2\x97\x86 hydra", KT_TITLE);
    length = text_columns(title);
    room = width - 1 - (length && width - length > 24 ? length + 3 : 0);
    end = chrome_tabs(app, c, 0, 10, room);
    header_project(app, project, sizeof(project));
    header_summary(app, project, summary, sizeof(summary));
    snprintf(right, sizeof(right), "%s%s%s", title, dot(app), summary);
    if (end + text_columns(right) + 3 >= width) {
        /* No room beside the tabs: the summary takes the row under the bar. */
        snprintf(right, sizeof(right), "%s", title);
        length = text_columns(summary);
        if (c->height > 2 && width >= 60 && length + 2 < width)
            kt_text(c, (struct kt_rect){width - length - 1, 1, length, 1}, summary, KT_MUTED);
    }
    length = text_columns(right);
    if (end + length + 2 < width) kt_text(c, (struct kt_rect){width - length - 1, 0, length, 1}, right, KT_MUTED);
}

/* The status line starts with a chip saying how fresh the data is: live,
 * updated a while ago, stale, or still loading; the text after it says the
 * rest (what failed, a notice, the search). */
static struct kt_style chip_tone(int kind) {
    if (kind == 0) return KT_SUCCESS;
    if (kind == 3) return KT_STYLE(KT_COLOR_INFO, KT_COLOR_SURFACE, 0);
    return KT_WARNING;
}

static const char *chip_for(const struct app *app, const char **status, struct kt_style *chip) {
    static const struct { const char *prefix, *utf8, *ascii; int kind; } chips[] = {
        {"Live", "\xe2\x97\x8f live", "* live", 0},
        {"STALE", "\xe2\x96\xb2 stale", "! stale", 1},
        {"Updated", "\xe2\x97\x8c updated", ". updated", 2},
        {"Waiting for the first snapshot", "\xe2\x97\x8c loading", ". loading", 3}};
    size_t i, n;
    for (i = 0; i < sizeof(chips) / sizeof(chips[0]); i++) {
        n = strlen(chips[i].prefix);
        if (strncmp(*status, chips[i].prefix, n)) continue;
        *status += n;
        while (**status == ' ' || **status == ':' || !strncmp(*status, "\xc2\xb7", 2)) *status += **status == ' ' || **status == ':' ? 1 : 2;
        *chip = chip_tone(chips[i].kind);
        return app->ascii ? chips[i].ascii : chips[i].utf8;
    }
    return NULL;
}

static void status_chip(struct app *app, struct kt_canvas *c, int row, const char *status, struct kt_style tone) {
    struct kt_style chip_tone;
    const char *chip = chip_for(app, &status, &chip_tone);
    int x = 1;
    if (chip) {
        int width = kt_text_width(chip);
        kt_text(c, (struct kt_rect){x + 1, row, width, 1}, chip, chip_tone);
        x += width + 4;
    }
    kt_text(c, (struct kt_rect){x, row, c->width - x - 1, 1}, status, tone);
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
    if (height >= 3) status_chip(app, c, height - 2, status, tone);
    if (hints) {
        /* A hand-written hint line drops whole hints from its right to fit. */
        char fitted[512];
        kt_text(c, (struct kt_rect){1, height - 1, c->width - 2, 1},
                fit_hints(hints, c->width > 2 ? (size_t)(c->width - 2) : 1U, fitted, sizeof(fitted)), KT_MUTED);
    }
    else key_footer(app, c, (struct kt_rect){1, height - 1, c->width - 2, 1});
}

/* Columns: HEAD, STATUS, AGENT, then WHERE (the machine) from 95 columns and
 * REPORTED from 110; a remote head's name already carries its host. */
struct head_columns { int status_x, agent_x, where_x, reported_x, name_width; bool wide, medium, reported; };

static void head_row_layout(int width, struct head_columns *k) {
    k->wide = width >= 95; k->medium = width >= 65; k->reported = width >= 110;
    k->reported_x = width - 12;
    k->where_x = k->reported ? k->reported_x - 13 : width - 12;
    k->agent_x = k->wide ? k->where_x - 16 : width - 15;
    k->status_x = (k->wide || k->medium ? k->agent_x : width) - 15;
    if (k->status_x < 20) k->status_x = 20;
    k->name_width = k->status_x - 4;
}

static void outline_heading(const struct app *app, struct outline_text *t) {
    (void)app;
    copy_text(t->name, sizeof(t->name), "HEAD");
    t->status = "STATUS"; t->agent = "AGENT"; t->where = "WHERE";
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
    snprintf(cell, sizeof(cell), "%s%c %s", selected ? (app->ascii ? ">" : "\xe2\x96\x8c") : " ", outline_marked(app, row) ? '*' : ' ', t.name);
    /* The selection fills its row; the bar at its left edge marks it too. */
    if (selected) kt_fill(&app->frame, (struct kt_rect){app->content_x, app->line, width, 1}, KT_SELECTED);
    column(app, 0, k.name_width + 3, row_tone, cell);
    column(app, k.status_x, 14, selected ? KT_SELECTED : t.tone, t.status);
    if (k.medium || k.wide) column(app, k.agent_x, k.wide ? 15 : 14, row_tone, t.agent);
    if (k.wide) column(app, k.where_x, 12, row_tone, t.where);
    if (k.reported) column(app, k.reported_x, 12, row_tone, t.reported);
    app->line++;
}

void render_empty_work(struct app *app) {
    char label[160];
    linef(app, "");
    style(app, TONE_STRONG);
    if (app->search[0]) linef(app, "No heads match the search.");
    else if (app->location[0]) linef(app, "No %s to show.", location_label(app, label, sizeof(label)));
    else linef(app, "No agent work in this project yet.");
    style(app, TONE_BASE);
    linef(app, "");
    if (app->search[0]) { paragraph(app, "Esc clears the search.", KT_BASE); return; }
    if (location_remote_only(app)) {
        paragraph(app, app->model.host_count ? "Hosts shows each machine's connection and last observation; r there retries a disconnected host. Esc or L shows all work."
                  : "No remote host is registered yet. Hosts (H) adds a machine over SSH; Esc or L shows all work.", KT_BASE);
        return;
    }
    if (app->location[0]) { paragraph(app, "Esc or L shows all work.", KT_BASE); return; }
    paragraph(app, "A head is one piece of work: its own branch and worktree, a terminal, and an agent working inside it. You can follow several heads here at once: for example, one head adds tests while another fixes a bug, each on its own branch.", KT_BASE);
    linef(app, "");
    paragraph(app, "Press n to start one: Hydra creates the branch and worktree, opens the terminal and starts your agent in it. Press : for other actions.", KT_BASE);
    linef(app, "");
    paragraph(app, "To run agents on another machine, press H for Hosts and add it there.", KT_MUTED);
}

/* One-panel summary of the selected head under the list. */
static void summary_line(struct app *app, const struct head *head, char *line, size_t size) {
    const char *sep = dot(app);
    char agent[TEXT + 96];
    if (head_remote(head)) {
        char observation[TEXT];
        remote_observation_text(app, head, observation, sizeof(observation));
        snprintf(line, size, "On %.100s%s%s%sdesired %.40s%sobservation %s%sproject %.300s", head->remote_host, sep,
                 remote_head_status(app, head), sep, head->desired, sep, observation, sep, head->remote_project);
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
    /* A remote head's keys are the footer's; the card says what it cannot. */
    if (head_remote(head)) snprintf(line, size, "Liveness unobserved: a desired state, not a live process.");
    else if (!head->head_id[0]) snprintf(line, size, "Enter details%sx remove", sep);
    else if (head_headless(head)) snprintf(line, size, "Enter details%sp step output%sx %s%s: more actions", sep, sep,
                                           head_run_role(app, head) && !strcmp(head_run_role(app, head), "worker") ? "dismiss (keeps the branch)" : "remove", sep);
    else snprintf(line, size, "Enter details%sa %s%sx remove%s: more actions", sep, open_action(app, head), sep, sep);
}

/* A bordered summary at most max_rows tall (borders included). */
static void summary_panel_rows(struct app *app, const char *title, const char *first, const char *second, int max_rows) {
    struct kt_rect r = {app->content_x - 1, app->line + 1, app->content_width + 2, app->limit - app->line - 1};
    int limit = app->limit;
    char heading[TEXT + 64];
    if (r.height > max_rows) r.height = max_rows;
    fit_text(app, title, r.width > 6 ? (size_t)r.width - 6U : 1U, heading, sizeof(heading));
    pane_rule(&app->frame, r, heading);
    app->line = r.y + 1;
    app->content_x++; app->content_width -= 2;
    /* Prose stops above the panel's bottom edge, leaving the last row to second. */
    app->limit = r.y + r.height - (second ? 2 : 1);
    paragraph(app, first, KT_BASE);
    app->limit = r.y + r.height - 1;
    if (second && app->line < app->limit) { style(app, TONE_MUTED); linef(app, "%s", second); style(app, TONE_BASE); }
    app->limit = limit;
    app->content_x--; app->content_width += 2;
    app->line = r.y + r.height;
}

static void summary_panel(struct app *app, const char *title, const char *first, const char *second) {
    summary_panel_rows(app, title, first, second, 5);
}

/* The selected remote task: receiver-owned, observed through its host. */
static void task_summary(struct app *app, const struct task_observation *task, char *first, size_t first_size, char *second, size_t second_size) {
    const char *sep = dot(app);
    snprintf(first, first_size, "On %.100s%s%s%sowner %s%sobservation %s%swaiting %s%snext %s", task->host, sep, task_status(app, task), sep,
             task->owner, sep, task->retained ? "retained; host disconnected" : task->freshness, sep, task->waiting_reason, sep, task->next_action);
    snprintf(second, second_size, "Enter details%sY approve%sN reject%sR resume%sX cancel%sr retry its host", sep, sep, sep, sep, sep);
}

static void render_selected_summary(struct app *app) {
    char first[1024], second[256], title[TEXT + 64];
    const struct head *head = selected_head(app);
    const struct task_observation *task = selected_task(app);
    if (task) {
        snprintf(title, sizeof(title), "task %.200s on %.40s", task->task_id, task->host);
        task_summary(app, task, first, sizeof(first), second, sizeof(second));
        summary_panel(app, title, first, second);
        return;
    }
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
    if (n.remote) linef(app, "%zu heads  |  Row %zu of %zu  |  %zu remote", n.heads + n.remote, position + 1U, count, n.remote);
    else if (n.run_heads) linef(app, "%zu head%s you started%s%zu in runs  |  Row %zu of %zu", n.heads, n.heads == 1 ? "" : "s", dot(app), n.run_heads, position + 1U, count);
    else linef(app, "%zu heads  |  Row %zu of %zu", n.heads, position + 1U, count);
    style(app, TONE_BASE);
}

static void list_rows(struct app *app, const struct outline_row *rows, size_t count, size_t position, int available) {
    size_t index, start = position >= (size_t)available ? position - (size_t)available + 1U : 0U;
    outline_line(app, NULL, false);
    for (index = start; index < count && available-- > 0; index++) {
        if (rows[index].kind == OUTLINE_HEAD) record_hit(app, rows[index].head);
        else if (rows[index].kind == OUTLINE_RUN) record_hit(app, MAX_HEADS + 1U + rows[index].run);
        else if (rows[index].kind == OUTLINE_TASK) record_hit(app, MAX_HEADS + 1U + WF_RUNS + rows[index].record);
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
    if (position == count && (app->run_row || app->task_row)) { app->run_row = app->task_row = false; retarget_selection(app); position = outline_position(app, rows, count); }
    if (count == 0U) { render_empty_work(app); return; }
    list_rows(app, rows, count, position, available < 1 ? 1 : available);
    list_footer(app, position < count ? position : 0, count);
    if (summary && app->limit - app->line >= 5) render_selected_summary(app);
}

static void render_coordination(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { render_empty_work(app); return; }
    style(app, TONE_STRONG); linef(app, "COORDINATION  %s", head->branch); style(app, TONE_BASE);
    if (head_remote(head)) {
        linef(app, "");
        paragraph(app, "This head runs on another machine. Claims, scopes, queue and resources are coordinated on that host; hydra fleet workflow inspects assignments there.", KT_BASE);
        return;
    }
    linef(app, "");
    paragraph(app, "This head is a single agent session. Coordination between heads matters when several heads work in the same project: claims (files a head intends to change), scopes (areas it stays within), a shared work queue and allocated resources let Hydra flag overlap early. For example, two heads that both claim lib/ are reported before they conflict. A workflow adds steps, dependencies and handoffs on top.", KT_BASE);
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
    else paragraph(app, "No workflow run is linked to this head. A single conversation does not need one.", KT_BASE);
}

/* The agent-pane commands with the chords bound to them now, and where to
 * change them. A keys file that was not applied as written is named. */
/* Overlays over the finished view: the help and the output viewer, each a
 * dialog panel over the view dimmed behind it, then the toasts. Dialogs that
 * wait for an answer (forms.c) are drawn after these, on top. */
static struct kt_rect overlay_panel(struct app *app, const char *title, int width, int height, const char *hints) {
    struct kt_modals backdrop;
    struct kt_rect whole = {0, 0, app->frame.width, app->frame.height}, panel;
    kt_modals_init(&backdrop);
    kt_restyle(&app->frame, whole, backdrop.backdrop);
    if (width > whole.width - 2) width = whole.width - 2;
    if (height > whole.height - 2) height = whole.height - 2;
    panel = (struct kt_rect){(whole.width - width) / 2, (whole.height - height) / 2, width, height};
    kt_fill(&app->frame, panel, KT_BASE);
    kt_panel_styled(&app->frame, panel, title, KT_FOCUS, KT_STRONG);
    if (hints && width > 8) {
        int length = kt_text_width(hints);
        if (length > width - 4) length = width - 4;
        kt_text(&app->frame, (struct kt_rect){panel.x + width - length - 2, panel.y + height - 1, length, 1}, hints, KT_MUTED);
    }
    return (struct kt_rect){panel.x + 2, panel.y + 1, width - 4, height - 2};
}

/* Help for where the user is, generated from the binding table: the keys
 * file and any problem with it first, then every key of the focused chain
 * that applies, grouped, then the chords that work while typing to an agent.
 * It scrolls with j / k, the arrows and the page keys. */
static void overlay_help(struct app *app) {
    const struct native_keys *keys = native_keys(app);
    struct kt_canvas view;
    struct kt_rect inside;
    int rows, total, top;
    inside = overlay_panel(app, "Keyboard", app->cols >= 100 ? 96 : app->cols - 2, app->rows - 2,
                           app->ascii ? " up/down scroll | ? or esc close " : " \xe2\x86\x91/\xe2\x86\x93 scroll \xe2\x80\xa2 ? or esc close ");
    if (inside.width < 4 || inside.height < 3) return;
    kt_text(&app->frame, (struct kt_rect){inside.x, inside.y, inside.width, 1}, "KEYBOARD HELP", KT_STRONG);
    kt_text(&app->frame, (struct kt_rect){inside.x, inside.y + 1, inside.width, 1},
            keys->problem[0] ? keys->problem : "hydra tui --print-keys lists every key; keys.conf in $HYDRA_HOME/tui sets your own.",
            keys->problem[0] ? KT_WARNING : KT_MUTED);
    top = inside.y + 3;
    rows = inside.y + inside.height - top;
    if (rows < 1) return;
    total = key_help(app, NULL, (struct kt_rect){0, 0, inside.width, rows}, 0);
    if (app->help_scroll > total - rows) app->help_scroll = total > rows ? total - rows : 0;
    if (!kt_canvas_view(&view, &app->frame, (struct kt_rect){inside.x, top, inside.width, rows})) return;
    (void)key_help(app, &view, (struct kt_rect){0, 0, inside.width, rows}, app->help_scroll);
    app->help_more = total > rows;
}

static void render_result(struct app *app);

/* The output viewer: a command's captured output, wrapped and scrolled. */
static void overlay_result(struct app *app) {
    struct kt_rect inside = overlay_panel(app, app->result_title, app->cols >= 100 ? 96 : app->cols - 2, app->rows - 2,
        app->ascii ? " up/down scroll | esc close " : " \xe2\x86\x91/\xe2\x86\x93 scroll \xe2\x80\xa2 esc close ");
    int x = app->content_x, width = app->content_width, line = app->line, limit = app->limit;
    if (inside.width < 4 || inside.height < 1) return;
    app->content_x = inside.x; app->content_width = inside.width;
    app->line = inside.y; app->limit = inside.y + inside.height;
    render_result(app);
    app->content_x = x; app->content_width = width; app->line = line; app->limit = limit;
}

static void overlays(struct app *app) {
    if (!app->frame.cells) return;
    if (app->help) overlay_help(app);
    else if (app->result_open) overlay_result(app);
    if (app->toasts_ready && app->toasts.count) {
        struct kt_rect area = {0, 0, app->frame.width - 1, app->frame.height - 3};
        kt_toasts_place(&app->toasts, area);
        kt_toasts_view(&app->toasts, &app->frame, area);
    }
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
    if (app->view == 6 && native_onboarding_active(app)) { native_onboarding_render(app); return; }
    switch (app->view) {
        case 0: render_list(app); break;
        case 1: render_head_detail(app); break;
        case 2: render_coordination(app); break;
        case 4: render_dashboard(app); break;
        case 5: render_workflow_graph(app); break;
        case 6: render_hosts(app); break;
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
    (void)app;
    return 2L * HYDRA_TUI_REFRESH_SECONDS;
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
    /* The answer to the user's last action stays visible beside a stale
     * snapshot's warning; neither hides the other. */
    if (stale && app->snapshot_error[0]) {
        if (app->notice[0]) snprintf(out, size, "%.200s%s%s%s%.200s", app->notice, sep, fresh, sep, app->snapshot_error);
        else snprintf(out, size, "%s%s%s", fresh, sep, app->snapshot_error);
        *tone = KT_WARNING; return;
    }
    if (app->notice[0]) { snprintf(out, size, "%s", app->notice); *tone = KT_BASE; return; }
    if (app->search[0]) { snprintf(out, size, "Search: %s%sEsc clears", app->search, sep); *tone = KT_BASE; return; }
    snprintf(out, size, "%s", fresh);
    if (app->view == 9) inbox_status(app, out, size);
    *tone = fresh_tone;
    /* The remote observation has its own age; a stale one is said beside
     * the local freshness so remote rows are never read as current. */
    {
        char remote[TEXT];
        struct kt_style remote_tone;
        if (app->view != 5 && remote_footer_text(app, remote, sizeof(remote), &remote_tone)) {
            text_append(out, size, "%s%s", sep, remote);
            if (kt_style_equal(remote_tone, KT_WARNING) || kt_style_equal(*tone, KT_MUTED)) *tone = remote_tone;
        }
    }
    if (app->location[0]) {
        char label[160];
        text_append(out, size, "%sShowing %s", sep, location_label(app, label, sizeof(label)));
    }
}

/* The footer is generated from the binding table (key_footer); the remote
 * setup flow and the onboarding view keep their own hints while they own
 * input. NULL: generated. */
static const char *hint_line(struct app *app) {
    if (app->view == 6 && native_setup_active(app)) return native_setup_hints(app, app->cols < 70);
    if (app->view == 6 && native_onboarding_active(app)) return native_onboarding_hints(app, app->cols < 70);
    return NULL;
}

static void detail_title(const struct app *app, const struct head *head, char *out, size_t size) {
    const char *label = app->diagnostics ? "Technical details" : "Details";
    const struct task_observation *task = selected_task(app);
    if (task) { snprintf(out, size, "%s: task %.100s on %.40s", label, task->task_id, task->host); return; }
    if (!head) { snprintf(out, size, "%s", label); return; }
    if (head_remote(head)) { snprintf(out, size, "%s: %.120s on %.40s", label, head->remote_branch[0] ? head->remote_branch : head->branch, head->remote_host); return; }
    snprintf(out, size, "%s: %.200s", label, head->branch);
}

static const char *panel_title(struct app *app, char *out, size_t size) {
    static const char *names[] = {NULL, NULL, "Coordination", "Inbox", "Overview", "Workflows", "Hosts", NULL, NULL, "Inbox"};
    const struct head *head = selected_head(app);
    if (app->view == 6 && native_setup_active(app)) return native_setup_title(app);
    if (app->view == 6 && native_onboarding_active(app)) return native_onboarding_title(app);
    if (app->view == 0 && !app->location[0]) snprintf(out, size, "%s", "Heads in this project");
    else if (app->view == 0) { char label[160]; snprintf(out, size, "Heads: %s", location_label(app, label, sizeof(label))); }
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
    if (!app->diagnostics) {
        if (render_workspace_view(app, frame, headless)) return;
    }
    if (!frame_begin(app, headless)) return;
    app->limit = app->rows - 3;
    chrome_header(app, &app->frame, view_titles[app->view]);
    if (app->view == 9) inbox_render(app);
    else if (app->rows >= 6) pane_rule(&app->frame, (struct kt_rect){1, 2, app->frame.width - 1, app->frame.height - 4},
                                  panel_title(app, title, sizeof(title)));
    app->line = 3;
    style(app, TONE_BASE);
    if (app->view != 9) render_content(app);
    app->limit = app->rows;
    status_line(app, status, sizeof(status), &tone, headless);
    chrome_footer(app, &app->frame, status, tone, hint_line(app));
    frame_end(app, frame, headless);
}
