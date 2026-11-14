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

/* Shared chrome: the top bar with the four screens, the status line and
 * the footer. Tab hits are recorded for the mouse. */
static const char *tab_label(int view) {
    switch (view) {
    case 9: return "Inbox";
    case 6: return "Hosts";
    case 8: return "Stats";
    default: return "Work";
    }
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

/* A tab is " N Label ": its number (the key that opens it) and its label
 * with a blank on each side, then Inbox's badge; the active one is a pill. */
static int tab_width(struct app *app, int view) {
    char badge[24];
    struct kt_style tone;
    return (int)strlen(tab_label(view)) + 4 + tab_badge(app, view, badge, sizeof(badge), &tone);
}

static void tab_draw(struct app *app, struct kt_canvas *c, int x, int view, bool current, int number) {
    char label[32], badge[24];
    struct kt_style tone = KT_WARNING;
    int core = (int)strlen(tab_label(view)) + 2, extra = tab_badge(app, view, badge, sizeof(badge), &tone);
    if (current) {
        snprintf(label, sizeof(label), " %d %s ", number, tab_label(view));
        /* Without colour the pill is reverse video, as a selection is. */
        kt_text(c, (struct kt_rect){x, 0, core + 2, 1}, label, app->no_color ?
                KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, KT_BOLD) : KT_STYLE(KT_COLOR_ON_ACCENT, KT_COLOR_ACCENT, KT_BOLD));
    } else {
        snprintf(label, sizeof(label), "%d", number);
        kt_text(c, (struct kt_rect){x + 1, 0, 1, 1}, label, KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0));
        kt_text(c, (struct kt_rect){x + 3, 0, core - 2, 1}, tab_label(view), KT_MUTED);
    }
    if (extra) kt_text(c, (struct kt_rect){x + core + 2, 0, extra - 1, 1}, badge, tone);
    record_tab(app, x, core + 2 + extra, view);
}

/* The four tabs after the brand; wide screens set them a blank apart. */
static int chrome_tabs(struct app *app, struct kt_canvas *c, int x, int width) {
    int views[12], gap = app->cols >= 100 ? 1 : 0;
    size_t count = tab_order(app, views), i;
    app->tab_count = 0;
    for (i = 0; i < count; i++) {
        int length = tab_width(app, views[i]);
        if (x + length > width) break;
        tab_draw(app, c, x, views[i], views[i] == app->view, (int)i + 1);
        x += length + gap;
    }
    return x;
}

static void header_project(const struct app *app, char *project, size_t size) {
    const char *name;
    project[0] = '\0';
    if (!app->links || !app->links->root[0]) return;
    name = strrchr(app->links->root, '/');
    snprintf(project, size, "%.60s", name && name[1] ? name + 1 : app->links->root);
}

/* The summary's parts in the order shown, each with a rank: as the width
 * shrinks the lowest rank goes first ("shop-api · 3 running" at 80
 * columns). Counts describe tasks the user started; heads a run created are
 * counted once, inside their run. With remote hosts the machines are
 * counted apart: "3 local · 2 remote". */
struct summary { char part[6][128]; int rank[6]; size_t count; };

static void summary_add(struct summary *s, int rank, const char *format, ...) {
    va_list args;
    if (s->count >= 6) return;
    va_start(args, format);
    vsnprintf(s->part[s->count], sizeof(s->part[0]), format, args);
    va_end(args);
    s->rank[s->count++] = rank;
}

static void header_summary(const struct app *app, struct summary *s) {
    struct work_counts n;
    struct remote_counts r;
    char project[128];
    bool remote;
    work_counts(app, &n);
    remote_counts(app, &r);
    remote = r.hosts || r.heads || r.tasks;
    s->count = 0;
    header_project(app, project, sizeof(project));
    if (project[0]) summary_add(s, 9, "%s", project);
    if (!n.heads && !n.run_heads && !remote) { summary_add(s, 8, "nothing running"); return; }
    if (remote) summary_add(s, 6, "%zu local%s%zu remote", n.heads, dot(app), r.heads + r.tasks);
    else {
        summary_add(s, 2, "this machine");
        summary_add(s, 5, "%zu head%s", n.heads, n.heads == 1 ? "" : "s");
    }
    if (n.run_heads) summary_add(s, 3, "+%zu in runs", n.run_heads);
    if (app->remote_stale) summary_add(s, 7, "remote stale %lds", remote_age(app));
    else if (r.hosts_down) summary_add(s, 7, "%zu of %zu host%s disconnected", r.hosts_down, r.hosts, r.hosts == 1 ? "" : "s");
    summary_add(s, 8, "%zu running", n.running);
}

/* Display columns of UTF-8 text counting one per scalar (chrome uses narrow glyphs). */
static int text_columns(const char *text) {
    int length = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) { if ((*cursor & 0xc0U) != 0x80U) length++; cursor++; }
    return length;
}

static void summary_join(const struct app *app, const struct summary *s, int floor, char *out, size_t size) {
    size_t i;
    out[0] = '\0';
    for (i = 0; i < s->count; i++) if (s->rank[i] >= floor) text_append(out, size, "%s%s", out[0] ? dot(app) : "", s->part[i]);
}

/* The parts that fit room columns: the lowest ranks are dropped first. */
static void summary_fit(const struct app *app, const struct summary *s, int room, char *out, size_t size) {
    int floor;
    for (floor = 0; floor <= 10; floor++) {
        summary_join(app, s, floor, out, size);
        if (text_columns(out) <= room) return;
    }
    out[0] = '\0';
}

/* The top bar: the brand, the four screens as numbered tabs (the active one
 * a pill, Inbox with its badge) and the project summary at the right, which
 * drops parts from its end as the width shrinks and is gone below 60. */
void chrome_header(struct app *app, struct kt_canvas *c, bool blank_row) {
    struct summary s;
    char right[512];
    int width = c->width, end;
    bool brand = width >= 60;
    (void)blank_row;
    kt_text(c, (struct kt_rect){1, 0, brand ? 8 : 2, 1}, app->ascii ? (brand ? "# hydra" : "#") : (brand ? "\xe2\x97\x86 hydra" : "\xe2\x97\x86"), KT_TITLE);
    end = chrome_tabs(app, c, brand ? 9 : 3, width - 1);
    if (width < 60) return;
    header_summary(app, &s);
    summary_fit(app, &s, width - end - 4, right, sizeof(right));
    if (right[0]) kt_text(c, (struct kt_rect){width - text_columns(right) - 2, 0, text_columns(right), 1}, right, KT_MUTED);
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
        while (**status == ' ' || **status == ':' || **status == '/' || !strncmp(*status, "\xc2\xb7", 2)) *status += **status == ' ' || **status == ':' || **status == '/' ? 1 : 2;
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
    if (hints && hints[0]) {
        /* A hand-written hint line drops whole hints from its right to fit. */
        char fitted[512];
        kt_text(c, (struct kt_rect){1, height - 1, c->width - 2, 1},
                fit_hints(hints, c->width > 2 ? (size_t)(c->width - 2) : 1U, fitted, sizeof(fitted)), KT_MUTED);
    }
    else key_footer(app, c, (struct kt_rect){1, height - 1, c->width - 2, 1});
}

/* The status line without the footer under it (Work while typing to an agent). */
void chrome_status(struct app *app, struct kt_canvas *c, const char *status, struct kt_style tone) {
    char pending[512];
    if (key_pending_text(app, pending, sizeof(pending))) { status = pending; tone = KT_STRONG; }
    if (c->height >= 3) status_chip(app, c, c->height - 2, status, tone);
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
    if (native_setup_active(app)) native_setup_render(app);
    else if (native_onboarding_active(app)) native_onboarding_render(app);
    else render_hosts(app);
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

void screen_status(struct app *app, char *out, size_t size, struct kt_style *tone, bool headless) {
    bool stale = app->snapshot_stale;
    const char *sep = dot(app);
    char fresh[96];
    struct kt_style fresh_tone = snapshot_freshness(app, app->snapshot_at, stale, headless, fresh, sizeof(fresh));
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
    if (app->search[0]) { snprintf(out, size, "Filter: %s%sEsc clears", app->search, sep); *tone = KT_BASE; return; }
    snprintf(out, size, "%s", fresh);
    if (app->view == 9) inbox_status(app, out, size);
    *tone = fresh_tone;
    /* The remote observation has its own age; a stale one is said beside
     * the local freshness so remote rows are never read as current. */
    {
        char remote[TEXT];
        struct kt_style remote_tone;
        if (remote_footer_text(app, remote, sizeof(remote), &remote_tone)) {
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

static const char *panel_title(struct app *app) {
    if (native_setup_active(app)) return native_setup_title(app);
    if (native_onboarding_active(app)) return native_onboarding_title(app);
    return "Hosts";
}

/* Work and Stats draw their own frame; Inbox and Hosts draw inside this one. */
void render(struct app *app, unsigned frame, bool headless) {
    char status[512];
    struct kt_style tone;
    app->hit_count = 0U;
    app->hit_cols = app->cols; app->hit_height = app->rows; app->hit_view = app->view;
    app->hit_tabs = true;
    if (app->view == 8) { if (render_statistics(app, frame, headless)) return; }
    else if (app->view != 9 && app->view != 6) { (void)work_render(app, frame, headless); return; }
    if (!frame_begin(app, headless)) return;
    app->limit = app->rows - 3;
    chrome_header(app, &app->frame, true);
    if (app->view == 9) inbox_render(app);
    else if (app->rows >= 6) pane_rule(&app->frame, (struct kt_rect){1, 2, app->frame.width - 1, app->frame.height - 4}, panel_title(app));
    app->line = 3;
    style(app, TONE_BASE);
    if (app->view == 6) render_content(app);
    app->limit = app->rows;
    screen_status(app, status, sizeof(status), &tone, headless);
    chrome_footer(app, &app->frame, status, tone, hint_line(app));
    frame_end(app, frame, headless);
}
