#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* One frame canvas serves every view. Views draw into it; the presenter paints
 * only changed cells, so idle refreshes never clear or flicker the screen.
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

/* Palette order follows enum tone: base, border, title, selected, warning,
 * strong, success, muted, focus. Terminal mode keeps the user's base colors. */
void emit_style(const struct app *app, enum tone tone) {
    static const char *palettes[3][9] = {
        {"\033[0m", "\033[0;36m", "\033[0;1;36m", "\033[0;1;7m", "\033[0;33m", "\033[0;1m",
         "\033[0;32m", "\033[0;2m", "\033[0;1;36m"},
        {"\033[0;38;5;252;48;5;234m", "\033[0;38;5;66;48;5;234m", "\033[0;1;38;5;117;48;5;234m",
         "\033[0;38;5;16;48;5;117m", "\033[0;38;5;214;48;5;234m", "\033[0;1;38;5;252;48;5;234m",
         "\033[0;38;5;114;48;5;234m", "\033[0;38;5;245;48;5;234m", "\033[0;1;38;5;117;48;5;234m"},
        {"\033[0;38;5;16;48;5;255m", "\033[0;38;5;67;48;5;255m", "\033[0;1;38;5;24;48;5;255m",
         "\033[0;1;38;5;231;48;5;24m", "\033[0;38;5;130;48;5;255m", "\033[0;1;38;5;16;48;5;255m",
         "\033[0;38;5;28;48;5;255m", "\033[0;38;5;245;48;5;255m", "\033[0;1;38;5;24;48;5;255m"}
    };
    if (tone < TONE_BASE || tone > TONE_FOCUS) tone = TONE_BASE;
    if (app->paint) fputs(palettes[app->theme][tone], stdout);
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

/* Frame lifecycle. The canvas leaves the terminal's last column unused. */
bool frame_alloc(struct app *app) {
    if (app->frame_cells) return true;
    app->frame_cells = calloc(WORKSPACE_CAPACITY, sizeof(*app->frame_cells));
    app->frame_previous = calloc(WORKSPACE_CAPACITY, sizeof(*app->frame_previous));
    if (!app->frame_cells || !app->frame_previous) { frame_free(app); return false; }
    (void)tv_present_init(&app->presenter, app->frame_previous, WORKSPACE_CAPACITY);
    app->frame_theme = app->theme;
    return true;
}

void frame_free(struct app *app) {
    free(app->frame_cells); free(app->frame_previous);
    app->frame_cells = NULL; app->frame_previous = NULL;
}

void frame_invalidate(struct app *app) { tv_present_invalidate(&app->presenter); }

bool frame_begin(struct app *app, bool headless) {
    int width = app->cols > 512 ? 511 : app->cols - 1;
    int height = app->rows > 256 ? 256 : app->rows;
    if (width < 1 || height < 1 || !frame_alloc(app)) return false;
    app->paint = !headless && !app->no_color;
    if (!tv_init(&app->frame, app->frame_cells, WORKSPACE_CAPACITY, width, height, !app->ascii)) return false;
    tv_clear(&app->frame, TV_BASE);
    app->line = 0; app->tone = TONE_BASE; app->tab_count = 0;
    app->content_x = 2; app->content_width = width - 4;
    if (app->content_width < 1) app->content_width = 1;
    return true;
}

void frame_end(struct app *app, unsigned frame, bool headless) {
    int y;
    if (headless) {
        printf("FRAME %u %dx%d\n", frame, app->cols, app->rows);
        for (y = 0; y < app->frame.height; y++) { (void)tv_write_row(&app->frame, y, stdout, NULL, NULL); putchar('\n'); }
        fflush(stdout);
        return;
    }
    if (app->frame_theme != app->theme) { frame_invalidate(app); app->frame_theme = app->theme; }
    if (!app->presenter.valid) { emit_style(app, TONE_BASE); fputs("\033[H", stdout); }
    if (!tv_present(&app->presenter, &app->frame, stdout, dashboard_style, app)) {
        copy_text(app->notice, sizeof(app->notice), "Screen output failed"); app->running = false;
    }
}

/* A borrowed view of the remaining content rows, for canvas-based views. */
bool frame_content(struct app *app, struct tv_canvas *out) {
    struct tv_rect r = {app->content_x, app->line, app->content_width, app->limit - app->line};
    if (r.height < 1) return false;
    return tv_canvas_view(out, &app->frame, r);
}

static void text_at(struct app *app, int x, int width, enum tv_style tone, const char *text) {
    if (width < 1) return;
    tv_text(&app->frame, (struct tv_rect){x, app->line, width, 1}, text, tone);
}

/* Fixed-width column inside the content area. Text is clipped to width. */
static void column(struct app *app, int x, int width, enum tv_style tone, const char *text) {
    text_at(app, app->content_x + x, width, tone, text);
}

void linef(struct app *app, const char *format, ...) {
    char buffer[2048];
    va_list args;
    if (app->line >= app->limit) return;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    text_at(app, app->content_x, app->content_width, (enum tv_style)app->tone, buffer);
    app->line++;
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

enum tv_style status_tone(const struct head *head) {
    const char *status = display_status(head);
    if (!strcmp(status, "LIVE")) return TV_SUCCESS;
    if (!strcmp(status, "STOPPED")) return TV_MUTED;
    if (!strcmp(status, "UNOBSERVED")) return TV_BASE;
    return TV_WARNING;
}

/* Heads that need a person: not running, or waiting for an approval. */
size_t attention_count(const struct app *app) {
    size_t i, count = 0;
    for (i = 0; i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        if (app->fleet) continue;
        if (strcmp(display_status(h), "LIVE") || h->gates > h->approved) count++;
    }
    return count;
}

static const char *agent_label(const struct head *head) {
    return head->profile[0] == '\0' || !strcmp(head->profile, "none") || !strcmp(head->profile, "-") ? "shell" : head->profile;
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
         "The terminal session for %s is no longer running. Its worktree and files are kept; nothing was removed from the repository. Open it from Work to restart the agent, or remove the head once its work is finished."},
        {"stale-lock", "Leftover lock: %s",
         "An interrupted command left a state lock behind. No work is affected. The check below clears it safely.%.0s"},
        {"orphan-worktree", "Worktree without a head: %s",
         "A git worktree exists that no head refers to. Review its contents before removing it; the check below previews the cleanup without deleting anything.%.0s"},
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
static void paragraph(struct app *app, const char *text, enum tv_style tone) {
    size_t length = strlen(text), offset = 0;
    size_t width = (size_t)app->content_width;
    int saved = app->tone;
    app->tone = (int)tone;
    while (offset < length && app->line < app->limit) {
        size_t chunk = length - offset, cut;
        if (chunk > width) {
            chunk = width;
            for (cut = chunk; cut > width / 2; cut--) if (text[offset + cut] == ' ') { chunk = cut; break; }
        }
        linef(app, "%.*s", (int)chunk, text + offset);
        offset += chunk;
        while (offset < length && text[offset] == ' ') offset++;
    }
    app->tone = saved;
}

static void section(struct app *app, const char *label) {
    if (app->line < app->limit) linef(app, "");
    style(app, TONE_BORDER); linef(app, "%s", label); style(app, TONE_BASE);
}

static void pair(struct app *app, const char *left_label, const char *left, enum tv_style left_tone,
                 const char *right_label, const char *right, enum tv_style right_tone) {
    if (app->line >= app->limit) return;
    if (app->content_width < 56) {
        column(app, 0, 11, TV_MUTED, left_label); column(app, 12, app->content_width - 12, left_tone, left); app->line++;
        if (app->line >= app->limit) return;
        column(app, 0, 11, TV_MUTED, right_label); column(app, 12, app->content_width - 12, right_tone, right); app->line++;
        return;
    }
    {
        int half = app->content_width / 2;
        column(app, 0, 11, TV_MUTED, left_label); column(app, 12, half - 13, left_tone, left);
        column(app, half, 11, TV_MUTED, right_label); column(app, half + 12, app->content_width - half - 12, right_tone, right);
        app->line++;
    }
}

/* Shared chrome: title row, tab row, footer rows. Tab hits are recorded for the mouse. */
static const char *view_titles[] = {"WORK", "DETAILS", "COORDINATION", "RECOVERY", "OVERVIEW", "WORKFLOWS", "HOSTS", "WORKSPACE", "STATISTICS", "ATTENTION"};
static const char *tab_labels[] = {"Work", "Details", "Coordination", "Recovery", "Overview", "Workflows", "Hosts", "Workspace", "Statistics", "Attention"};

size_t tab_order(const struct app *app, int *views) {
    size_t count = 0;
    views[count++] = 0; views[count++] = 1;
    if (app->view == 2) views[count++] = 2;
    views[count++] = 4;
    if (app->fleet) views[count++] = 6;
    views[count++] = 9; views[count++] = 3;
    if (!app->fleet) views[count++] = 5;
    views[count++] = 8; views[count++] = 7;
    return count;
}

static int tab_width(int view, bool current) {
    return (int)strlen(tab_labels[view]) + (current ? 2 : 0);
}

/* First tab to draw so the active tab stays visible in the available width. */
static size_t tabs_first_visible(const int *views, size_t active, int gap, int width) {
    size_t first = 0, i;
    while (first < active) {
        int needed = 0;
        for (i = first; i <= active; i++) needed += tab_width(views[i], i == active) + gap;
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

static void chrome_tabs(struct app *app, struct tv_canvas *c, int row) {
    int views[12], gap = 2, x = 1, total = 0, width = c->width - 1;
    size_t count = tab_order(app, views), i, first, active = 0;
    for (i = 0; i < count; i++) { total += tab_width(views[i], views[i] == app->view); if (views[i] == app->view) active = i; }
    if (total + (int)(count - 1) * gap > width) gap = 1;
    app->tab_count = 0;
    first = tabs_first_visible(views, active, gap, width);
    for (i = first; i < count; i++) {
        char label[32];
        bool current = views[i] == app->view;
        int length = tab_width(views[i], current);
        if (x + length > width) { tv_text(c, (struct tv_rect){x, row, width - x, 1}, "..", TV_MUTED); break; }
        snprintf(label, sizeof(label), current ? "[%s]" : "%s", tab_labels[views[i]]);
        tv_text(c, (struct tv_rect){x, row, length, 1}, label, current ? TV_SELECTED : TV_MUTED);
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

static void header_summary(const struct app *app, const char *project, char *right, size_t size) {
    const char *sep = dot(app);
    size_t i, running = 0;
    for (i = 0; i < app->model.head_count; i++) if (!strcmp(display_status(&app->model.heads[i]), "LIVE")) running++;
    if (app->fleet) snprintf(right, size, "%sFleet%s%zu remote heads%s%zu hosts", project, sep, app->model.head_count, sep, app->model.host_count);
    else if (!app->model.head_count) snprintf(right, size, "%sLocal%sno agent work yet", project, sep);
    else snprintf(right, size, "%sLocal%s%zu heads%s%zu running%s%zu need attention%s%zu to repair", project, sep,
                  app->model.head_count, sep, running, sep, attention_count(app), sep, app->model.recovery_count);
}

/* Display columns of UTF-8 text counting one per scalar (chrome uses narrow glyphs). */
static int text_columns(const char *text) {
    int length = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) { if ((*cursor & 0xc0U) != 0x80U) length++; cursor++; }
    return length;
}

void chrome_header(struct app *app, struct tv_canvas *c, const char *title) {
    char left[256], right[256] = "", project[128];
    int width = c->width, length;
    snprintf(left, sizeof(left), "HYDRA / %s", title);
    tv_text(c, (struct tv_rect){1, 0, width - 2, 1}, left, TV_TITLE);
    if (width >= 60) {
        header_project(app, project, sizeof(project));
        header_summary(app, project, right, sizeof(right));
        length = text_columns(right);
        if (length + (int)strlen(left) + 4 < width)
            tv_text(c, (struct tv_rect){width - length - 1, 0, length, 1}, right, TV_MUTED);
    }
    chrome_tabs(app, c, 1);
}

void chrome_footer(struct app *app, struct tv_canvas *c, const char *status, enum tv_style tone, const char *hints) {
    int height = c->height;
    if (height >= 3) tv_text(c, (struct tv_rect){1, height - 2, c->width - 2, 1}, status, tone);
    tv_text(c, (struct tv_rect){1, height - 1, c->width - 2, 1}, hints, TV_MUTED);
    (void)app;
}

struct head_columns { const char *name, *status, *agent, *reported; int status_x, agent_x, reported_x, name_width; bool wide, medium; };

static void head_row_text(const struct app *app, const struct head *head, struct head_columns *k) {
    if (head == NULL) {
        k->name = "HEAD"; k->status = app->fleet ? "DESIRED" : "STATUS";
        k->agent = app->fleet ? "HOST" : "AGENT"; k->reported = "REPORTED";
        return;
    }
    k->name = app->fleet && head->remote_branch[0] ? head->remote_branch : head->branch;
    k->status = app->fleet ? head->desired : status_label(head);
    k->agent = app->fleet ? head->remote_host : agent_label(head);
    k->reported = head->declared[0] ? head->declared : "-";
}

static void head_row_layout(int width, struct head_columns *k) {
    k->wide = width >= 95; k->medium = width >= 65;
    k->reported_x = width - 12;
    k->agent_x = k->wide ? k->reported_x - 16 : width - 15;
    k->status_x = (k->wide || k->medium ? k->agent_x : width) - 15;
    if (k->status_x < 20) k->status_x = 20;
    k->name_width = k->status_x - 4;
}

static enum tv_style head_status_tone(const struct app *app, const struct head *head, bool selected) {
    if (head == NULL) return TV_MUTED;
    if (selected) return TV_SELECTED;
    return app->fleet ? TV_BASE : status_tone(head);
}

/* Work list: aligned columns, status tone only on the status cell. */
static void head_row(struct app *app, const struct head *head, bool selected) {
    int width = app->content_width;
    struct head_columns k;
    char lead[TEXT + 4];
    enum tv_style row_tone = head == NULL ? TV_MUTED : selected ? TV_SELECTED : TV_BASE;
    bool marked = head != NULL && marked_index(app, head->branch) < app->marked_count;
    if (app->line >= app->limit) return;
    head_row_text(app, head, &k);
    head_row_layout(width, &k);
    snprintf(lead, sizeof(lead), "%c%c %s", head == NULL ? ' ' : selected ? '>' : ' ', marked ? '*' : ' ', k.name);
    if (selected) column(app, 0, width, TV_SELECTED, "");
    column(app, 0, k.name_width + 3, row_tone, lead);
    column(app, k.status_x, 14, head_status_tone(app, head, selected), k.status);
    if (k.medium || k.wide) column(app, k.agent_x, k.wide ? 15 : 14, row_tone, k.agent);
    if (k.wide && !app->fleet) column(app, k.reported_x, 12, row_tone, k.reported);
    app->line++;
}

static void render_empty_work(struct app *app) {
    linef(app, "");
    style(app, TONE_STRONG);
    linef(app, app->search[0] ? "No heads match the search." : app->fleet ? "No remote heads." : "No agent work in this project yet.");
    style(app, TONE_BASE);
    linef(app, "");
    if (app->search[0]) { paragraph(app, "Esc clears the search.", TV_BASE); return; }
    if (app->fleet) { paragraph(app, "Hosts shows connectivity for each machine; Recovery lists host problems.", TV_BASE); return; }
    paragraph(app, "A head is one piece of work: its own branch and worktree, a terminal, and an agent working inside it. You can follow several heads here at once.", TV_BASE);
    linef(app, "");
    paragraph(app, "Press n to start one: Hydra creates the branch and worktree, opens the terminal and starts your agent in it. Press : for other actions.", TV_BASE);
}

/* One-panel summary of the selected head under the list. */
static void summary_line(struct app *app, const struct head *head, char *line, size_t size) {
    const char *sep = dot(app);
    if (app->fleet) {
        snprintf(line, size, "Host %.100s%sdesired %.40s%sproject %.300s", head->remote_host, sep, head->desired, sep, head->remote_project);
        return;
    }
    if (!head->head_id[0]) {
        snprintf(line, size, "Agent %s%ssession %s%sno head record; counters unknown", agent_label(head), sep, status_label(head), sep);
        return;
    }
    snprintf(line, size, "%s%s%s%s%u changed files%s%u of %u approvals%s", agent_label(head), sep,
             status_label(head), sep, head->diff, sep, head->approved, head->gates, head->declared[0] ? sep : "");
    if (head->declared[0]) text_append(line, size, "reported %.63s", head->declared);
}

static void render_selected_summary(struct app *app, const struct head *head) {
    char line[512];
    const char *sep = dot(app);
    struct tv_rect r = {app->content_x - 1, app->line + 1, app->content_width + 2, app->limit - app->line - 1};
    if (r.height > 5) r.height = 5;
    tv_panel_styled(&app->frame, r, head->branch, TV_BORDER, TV_STRONG);
    app->line = r.y + 1;
    app->content_x++; app->content_width -= 2;
    summary_line(app, head, line, sizeof(line));
    linef(app, "%s", line);
    style(app, TONE_MUTED);
    if (app->fleet) linef(app, "Enter details%sa attach to terminal%sc interrupt", sep, sep);
    else if (!head->head_id[0]) linef(app, "Enter details%sx remove", sep);
    else linef(app, "Enter details%sa talk to the agent%sx remove%s: more actions", sep, sep, sep);
    style(app, TONE_BASE);
    app->content_x--; app->content_width += 2;
    app->line = r.y + r.height;
}

static void render_list(struct app *app) {
    size_t index, ordinal = 0U, selected = 0U, count = 0U;
    int available = app->limit - app->line - 2;
    size_t start;
    bool summary = app->rows >= 20 && app->cols >= 70;
    if (summary && available > app->rows / 3) available = app->rows / 3;
    retarget_selection(app);
    for (index = 0U; index < app->model.head_count; index++) {
        if (!head_matches(&app->model.heads[index], app->search)) continue;
        if (index == app->selected) selected = count;
        count++;
    }
    if (count == 0U) { render_empty_work(app); return; }
    if (available < 1) available = 1;
    start = selected >= (size_t)available ? selected - (size_t)available + 1U : 0U;
    head_row(app, NULL, false);
    for (index = 0U; index < app->model.head_count; index++) {
        const struct head *head = &app->model.heads[index];
        if (!head_matches(head, app->search)) continue;
        if (ordinal++ < start) continue;
        if (available-- <= 0) break;
        record_hit(app, index);
        head_row(app, head, index == app->selected);
    }
    style(app, TONE_MUTED);
    linef(app, "%zu heads  |  Row %zu of %zu", count, selected + 1U, count);
    style(app, TONE_BASE);
    if (summary && selected_head(app) != NULL && app->limit - app->line >= 5) render_selected_summary(app, selected_head(app));
}

static void render_diagnostics(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { linef(app, "No selected head matching the current search."); return; }
    style(app, TONE_STRONG); linef(app, "TECHNICAL DETAILS  %s", head->branch); style(app, TONE_BASE);
    if (app->fleet) {
        linef(app, "Host: %s", head->remote_host);
        linef(app, "Branch: %s", head->remote_branch);
        linef(app, "Project: %s", head->remote_project);
        linef(app, "Desired state: %s", head->desired);
        linef(app, "Instance: %s", head->instance);
        linef(app, "Head: %s", head->head_id);
        return;
    }
    linef(app, "session: %s   profile: %s   group: %s   PR: %s", head->session, head->profile, head->group, head->pr);
    linef(app, "declared: %s   desired: %s", head->declared[0] == '\0' ? "none" : head->declared, head->desired);
    linef(app, "observed: %s   confidence: %s   liveness: %s   display: %s",
          head->observed, head->confidence, head->liveness, display_status(head));
    linef(app, "events: %u   signals: %u   messages: %u   gates: %u (%u approved)",
          head->events, head->signals, head->messages, head->gates, head->approved);
    linef(app, "claims: %u   scopes: %u   queue: %u   resources: %u   changed files: %u",
          head->claims, head->scopes, head->queue, head->resources, head->diff);
    linef(app, "adapter: %s   confidence: %s", head->adapter, head->adapter_confidence);
    linef(app, "adapter source: %s", head->adapter_source);
    linef(app, "notifications: %u configured; delivery delegated", head->notifications);
    linef(app, "notification source: %s", head->notification_source[0] == '\0' ? "unavailable" : head->notification_source);
    linef(app, "lifecycle source: %s", head->source);
    linef(app, "instance: %s   head: %s", head->instance, head->head_id);
    linef(app, "");
    style(app, TONE_MUTED); linef(app, "d closes technical details"); style(app, TONE_BASE);
}

static void render_detail_fleet(struct app *app, const struct head *head) {
    pair(app, "Host", head->remote_host, TV_BASE, "Desired", head->desired, TV_BASE);
    linef(app, "Project     %s", head->remote_project);
    section(app, "NEXT");
    linef(app, "a  attach to the remote terminal to see what it is doing");
    linef(app, "c  interrupt the agent; the worktree and its files are kept");
}

static void detail_group_label(const struct app *app, const struct head *head, char *group, size_t size) {
    const char *sep = dot(app);
    bool has_pr = head->pr[0] && strcmp(head->pr, "-");
    snprintf(group, size, "%s%s%s", head->group[0] && strcmp(head->group, "-") ? head->group : "none",
             has_pr ? sep : "", has_pr ? "PR " : "");
    if (has_pr) text_append(group, size, "%.40s", head->pr);
}

static void render_detail_changes(struct app *app, const struct head *head) {
    char changes[64];
    section(app, "CHANGES");
    if (!head->head_id[0]) { linef(app, "Changed files   unknown (no head record; see Recovery)"); return; }
    snprintf(changes, sizeof(changes), "%u", head->diff);
    linef(app, "Changed files   %-6s in the worktree, not yet committed", changes);
    linef(app, "Full diff       : git diff shows everything since the branch base");
}

static void render_detail_checks(struct app *app, const struct head *head) {
    char approvals[96];
    const char *sep = dot(app);
    unsigned pending;
    section(app, "CHECKS");
    if (!head->head_id[0]) { linef(app, "Approvals       unknown"); return; }
    pending = head->gates > head->approved ? head->gates - head->approved : 0;
    snprintf(approvals, sizeof(approvals), "%u of %u approved%s%s", head->approved, head->gates,
             pending ? sep : "", pending ? "waiting for your decision" : "");
    style(app, pending ? TONE_WARNING : TONE_BASE);
    linef(app, "Approvals       %s", approvals);
    style(app, TONE_BASE);
    linef(app, "Messages        %u exchanged with other heads", head->messages);
}

static void render_detail_preview(struct app *app) {
    char preview[sizeof(app->preview_text)];
    char *line, *save = NULL;
    section(app, "TERMINAL OUTPUT");
    copy_text(preview, sizeof(preview), app->preview_text[0] ? app->preview_text : "No terminal output available.");
    line = strtok_r(preview, "\r\n", &save);
    while (line != NULL && app->line < app->limit) {
        linef(app, "%s", line);
        line = strtok_r(NULL, "\r\n", &save);
    }
}

static void render_detail(struct app *app) {
    const struct head *head = selected_head(app);
    char group[TEXT + 64];
    if (head == NULL) { render_empty_work(app); return; }
    if (app->diagnostics) { render_diagnostics(app); return; }
    style(app, TONE_STRONG); linef(app, "%s", head->branch); style(app, TONE_BASE);
    if (app->fleet) { render_detail_fleet(app, head); return; }
    detail_group_label(app, head, group, sizeof(group));
    pair(app, "Agent", agent_label(head), TV_BASE, "Session", status_label(head), status_tone(head));
    pair(app, "Reported", head->declared[0] ? head->declared : "nothing yet", head->declared[0] ? TV_STRONG : TV_MUTED, "Group", group, TV_BASE);
    if (!strcmp(display_status(head), "STALE"))
        paragraph(app, "The terminal is gone but the last observation said the agent was still working. Files in the worktree are kept; check them before removing the head.", TV_WARNING);
    render_detail_changes(app, head);
    render_detail_checks(app, head);
    if (app->preview) { render_detail_preview(app); return; }
    section(app, "NEXT");
    linef(app, "a  talk to the agent in the workspace     p  show recent terminal output");
    linef(app, "c  coordination with other heads          x  remove this head");
    linef(app, ":  more actions (diff, switch, approvals)  d  technical details");
}

static void render_coordination(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { render_empty_work(app); return; }
    style(app, TONE_STRONG); linef(app, "COORDINATION  %s", head->branch); style(app, TONE_BASE);
    if (app->fleet) {
        paragraph(app, "Remote heads are coordinated on their host. Use hydra fleet workflow to inspect assignments there.", TV_BASE);
        return;
    }
    linef(app, "");
    paragraph(app, "This head is a single agent session. Coordination between heads uses claims (files a head intends to change), scopes (areas it stays within), a shared work queue and allocated resources. A workflow adds steps, dependencies and handoffs on top.", TV_BASE);
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
    style(app, TONE_STRONG); linef(app, "%s", title); style(app, TONE_BASE);
    paragraph(app, detail, TV_BASE);
    linef(app, "");
    linef(app, "Check: %s", item->action);
    style(app, TONE_MUTED);
    linef(app, "Kind: %s   Source: %s   Confidence: %s", item->kind, item->source, item->confidence);
    linef(app, "Inspect: %s", item->action);
    linef(app, "Enter runs the check and shows its output here; d returns to the list");
    style(app, TONE_BASE);
}

static void render_recovery_row(struct app *app, size_t index) {
    const struct recovery *item = &app->model.recovery[index];
    char title[TEXT + 64], detail[1024];
    bool selected = index == app->recovery_selected, wide = app->content_width >= 100;
    recovery_explain(item, title, sizeof(title), detail, sizeof(detail));
    record_hit(app, index);
    if (selected) column(app, 0, app->content_width, TV_SELECTED, ">");
    column(app, 2, wide ? app->content_width - 22 : app->content_width - 2, selected ? TV_SELECTED : TV_WARNING, title);
    if (wide) column(app, app->content_width - 18, 18, selected ? TV_SELECTED : TV_MUTED, item->kind);
    app->line++;
}

static void render_recovery(struct app *app) {
    size_t index, start;
    int available = app->limit - app->line - 2;
    style(app, TONE_STRONG); linef(app, "RECOVERY  %zu finding%s", app->model.recovery_count, app->model.recovery_count == 1 ? "" : "s"); style(app, TONE_BASE);
    if (app->model.recovery_count == 0U) {
        linef(app, "");
        paragraph(app, "Nothing needs repair. Findings appear here when a terminal stops unexpectedly, a removal does not finish, or saved state cannot be read.", TV_BASE);
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

static void render_help(struct app *app) {
    const char *sep = dot(app);
    style(app, TONE_STRONG); linef(app, "KEYBOARD HELP"); style(app, TONE_BASE);
    if (app->rows < 20) {
        linef(app, "Tab/Shift-Tab tabs or panes  arrows select  Enter open  Esc back");
        linef(app, app->fleet ? "a attach  c interrupt  / search" : "n new task  a agent  x remove  / search  : actions");
        linef(app, "t theme  ? close help  q quit");
        return;
    }
    section(app, "MOVE AROUND");
    linef(app, "Tab / Shift-Tab   next / previous tab; inside Workspace, next / previous pane");
    linef(app, "Left / Right      previous / next tab       1-9  jump to a tab");
    linef(app, "Up / Down, j / k  select                    Enter  open the selection");
    linef(app, "Esc               one step back (details%slist, close help, clear search)", sep);
    section(app, app->fleet ? "REMOTE HEADS" : "WORK");
    if (app->fleet) {
        linef(app, "a  attach to a remote terminal        c  interrupt the remote head (confirmed)");
        linef(app, "/  search heads                       I  attention");
    } else {
        linef(app, "n  start a new task                   a  talk to the selected agent");
        linef(app, "x  remove selected or marked heads    Space / A  mark one / all   G  group marked");
        linef(app, "/  search                             :  more actions (diff, switch, approvals...)");
        linef(app, "p  recent terminal output             d  technical details      c  coordination");
    }
    section(app, "WORKSPACE");
    linef(app, "A  conversation   B  plan overview   C  monitor   z  zoom the focused pane   S  two agents");
    linef(app, "While typing to an agent: Ctrl-B Tab returns to Hydra, Ctrl-B x closes the pane,");
    linef(app, "Ctrl-B n switches agent, Ctrl-B [ scrolls history, Ctrl-B q quits.");
    section(app, "OTHER");
    linef(app, "t  theme (terminal / dark / light)    D  statistics    I  attention    ?  close help    q  quit");
}

static void render_result(struct app *app) {
    const char *text = app->result_text;
    size_t line = 0;
    style(app, TONE_BASE);
    if (!text[0]) { linef(app, "No output."); return; }
    while (*text && app->line < app->limit) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (line++ >= app->result_scroll) linef(app, "%.*s", (int)(length > 1024 ? 1024 : length), text);
        text += length + (end ? 1 : 0);
    }
}

static void render_content(struct app *app) {
    if (app->result_open) { render_result(app); return; }
    if (app->help) { render_help(app); return; }
    switch (app->view) {
        case 0: render_list(app); break;
        case 1: render_detail(app); break;
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

static long snapshot_age(const struct app *app, bool *stale) {
    time_t observed_at = app->view == 5 ? app->workflow_at : app->snapshot_at;
    long age = !observed_at ? 0L : (long)(time(NULL) - observed_at);
    *stale = app->view == 5 ? app->workflow_stale : app->snapshot_stale;
    return age < 0 ? 0 : age;
}

static void status_marked(struct app *app, char *out, size_t size) {
    const char *sep = dot(app);
    snprintf(out, size, "%zu marked%sx remove%sG group%sSpace unmark%s%s", app->marked_count, sep, sep, sep,
             app->notice[0] ? sep : "", app->notice);
}

static void status_line(struct app *app, char *out, size_t size, enum tv_style *tone) {
    bool stale;
    long age = snapshot_age(app, &stale);
    const char *sep = dot(app);
    *tone = TV_MUTED;
    if (app->marked_count) { status_marked(app, out, size); *tone = TV_STRONG; return; }
    if (stale && app->snapshot_error[0]) { snprintf(out, size, "STALE: last good snapshot%s%s", sep, app->snapshot_error); *tone = TV_WARNING; return; }
    if (app->notice[0]) { snprintf(out, size, "%s", app->notice); *tone = TV_BASE; return; }
    if (app->search[0]) { snprintf(out, size, "Search: %s%sEsc clears", app->search, sep); *tone = TV_BASE; return; }
    if (stale) { snprintf(out, size, "STALE: last good snapshot%sage %lds", sep, age); *tone = TV_WARNING; return; }
    snprintf(out, size, "Current snapshot%sage %lds", sep, age);
}

/* Hints for views whose keys do not depend on local versus fleet mode. */
static const char *view_hints(struct app *app, bool narrow) {
    if (app->view == 9) {
        if (native_review_active(app)) return "j/k scroll  i IDs  f refs  l/t/p log/transcript/PR  Esc back  ? help  q quit";
        return narrow ? "Enter details  r review  ? help  q quit" : "j/k select  Enter details  r review  s mark seen  I refresh  Esc back  ? help  q quit";
    }
    if (app->view == 5) return narrow ? "j/k step  [/] run  ? help  q quit" : "j/k step  [/] run  h/l/J/K pan  Enter recentre  Esc back  ? help  q quit";
    if (app->view == 6) return "j/k host  Enter show its heads  Esc back  ? help  q quit";
    if (app->view == 3) return narrow ? "Enter check  d detail  ? help  q quit" : app->diagnostics ? "Enter run the check  d back to list  Esc back  ? help  q quit" : "j/k select  Enter run the check  d explain  Esc back  ? help  q quit";
    return NULL;
}

static const char *fleet_hints(struct app *app, bool narrow) {
    if (narrow) return "a attach  c interrupt  ? help  q quit";
    return app->view == 1 ? "a attach  c interrupt  d technical  Esc back  ? help  q quit" : "Enter details  a attach  c interrupt  / search  Tab next tab  ? help  q quit";
}

static const char *local_hints(struct app *app, bool narrow) {
    if (app->view == 1) return narrow ? "a agent  p output  Esc back  ? help  q quit" : app->diagnostics ? "d close technical details  Esc back  ? help  q quit" : "a agent  p output  c coordination  x remove  d technical  Esc back  ? help  q quit";
    if (app->view == 2) return "Esc back  : more actions  ? help  q quit";
    if (app->view == 4) return narrow ? "Enter open  Tab next tab  ? help  q quit" : "j/k select  Enter details  Tab next tab  ? help  q quit";
    if (narrow) return "Enter open  n new  x remove  ? help  q quit";
    return app->model.head_count ? "j/k select  Enter details  a agent  n new task  x remove  / search  : more  ? help  q quit"
                                 : "n new task  : more actions  Tab next tab  ? help  q quit";
}

static const char *hint_line(struct app *app) {
    bool narrow = app->cols < 70;
    const char *hints;
    if (app->result_open) return "Enter or Esc closes  j/k scroll";
    if (app->help) return "? or Esc closes help";
    hints = view_hints(app, narrow);
    if (hints) return hints;
    return app->fleet ? fleet_hints(app, narrow) : local_hints(app, narrow);
}

static void detail_title(const struct app *app, const struct head *head, char *out, size_t size) {
    const char *label = app->diagnostics ? "Technical details" : "Details";
    if (!head) { snprintf(out, size, "%s", label); return; }
    snprintf(out, size, "%s: %.200s", label, head->branch);
}

static const char *panel_title(struct app *app, char *out, size_t size) {
    static const char *names[] = {NULL, NULL, "Coordination", "Recovery", "Overview", "Workflows", "Hosts", NULL, NULL, "Attention"};
    const struct head *head = selected_head(app);
    if (app->result_open) return app->result_title;
    if (app->help) return "HELP";
    if (app->view == 0) snprintf(out, size, "%s", app->fleet ? "Remote heads" : "Heads in this project");
    else if (app->view == 1) detail_title(app, head, out, size);
    else snprintf(out, size, "%s", names[app->view] ? names[app->view] : tab_labels[app->view]);
    return out;
}

void render(struct app *app, unsigned frame, bool headless) {
    char status[512], title[TEXT];
    enum tv_style tone;
    app->hit_count = 0U;
    app->hit_cols = app->cols; app->hit_height = app->rows; app->hit_view = app->view;
    app->hit_tabs = true;
    if (!app->help && !app->diagnostics && !app->result_open) {
        if (render_workspace_view(app, frame, headless)) return;
    }
    if (!frame_begin(app, headless)) return;
    app->limit = app->rows - 3;
    app->boxed = true;
    chrome_header(app, &app->frame, view_titles[app->view]);
    if (app->rows >= 6) tv_panel_styled(&app->frame, (struct tv_rect){0, 2, app->frame.width, app->frame.height - 4},
                                        panel_title(app, title, sizeof(title)), TV_BORDER, TV_STRONG);
    app->line = 3;
    style(app, TONE_BASE);
    render_content(app);
    app->boxed = false;
    app->limit = app->rows;
    status_line(app, status, sizeof(status), &tone);
    chrome_footer(app, &app->frame, status, tone, hint_line(app));
    frame_end(app, frame, headless);
}
