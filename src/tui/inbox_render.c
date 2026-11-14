#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "attention.h"
#include "inbox.h"
/* Inbox on screen. At 100 columns and more the list (two lines per item,
 * under section headings) and the selected item's detail sit side by side;
 * below that the list takes the width and Enter opens the detail in its
 * place. The detail has pages, Summary, Review, References and Technical,
 * named in its rule; Summary says what waits, what each choice does and the
 * keys that act on it. */

#define DANGER KT_STYLE(KT_COLOR_DANGER, KT_COLOR_SURFACE, 0)
#define FAINT KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0)
#define INFO KT_STYLE(KT_COLOR_INFO, KT_COLOR_SURFACE, 0)
#define WIDE_INBOX 100

static const char *glyph(const struct app *app, const char *utf8, const char *ascii) { return app->ascii ? ascii : utf8; }

const char *inbox_section_name(enum inbox_section section) {
    static const char *const names[] = {"Decisions", "Failed", "Ready for review", "Agents", "Repairs", "Your actions", "Seen"};
    return section < INBOX_SECTIONS ? names[section] : "";
}

static const char *attention_glyph(const struct app *app, const struct attention_item *item, struct kt_style *tone) {
    if (attention_is_approval(item)) { *tone = KT_WARNING; return glyph(app, "\xe2\x97\x90", "~"); }
    if (attention_is_failure(item)) { *tone = DANGER; return glyph(app, "\xe2\x9c\x97", "x"); }
    if (!strcmp(item->kind, "result")) { *tone = KT_SUCCESS; return glyph(app, "\xe2\x9c\x93", "+"); }
    *tone = KT_WARNING;
    return glyph(app, "\xe2\x96\xb2", "!");
}

const char *inbox_glyph(const struct app *app, const struct inbox_entry *e, struct kt_style *tone) {
    switch (e->kind) {
    case INBOX_ATTENTION: return attention_glyph(app, &app->attention->items[e->index], tone);
    case INBOX_GATE: case INBOX_PLAN: *tone = KT_WARNING; return glyph(app, "\xe2\x97\x90", "~");
    case INBOX_HEAD:
        if (head_terminal_gone(app, &app->model.heads[e->index])) { *tone = KT_WARNING; return glyph(app, "\xe2\x96\xb2", "!"); }
        *tone = KT_MUTED; return "?";
    case INBOX_RECOVERY: *tone = KT_WARNING; return glyph(app, "\xe2\x96\xb2", "!");
    case INBOX_LAND: *tone = KT_SUCCESS; return glyph(app, "\xe2\x9c\x93", "+");
    default: *tone = DANGER; return glyph(app, "\xe2\x9c\x97", "x");
    }
}

/* ---- the two lines of an item ---------------------------------------------- */

static const char *item_name(const struct attention_item *item) {
    if (attention_present(item->label)) return item->label;
    if (attention_present(item->task)) return item->task;
    return attention_present(item->run) ? item->run : "unnamed";
}

/* One part of a line of parts joined by the separator; absent values add nothing. */
static void part(const struct app *app, char *line, size_t size, const char *label, const char *value) {
    if (!attention_present(value)) return;
    text_append(line, size, "%s%s%s", line[0] ? dot(app) : "", label, value);
}

static void where_line(const struct app *app, const struct attention_item *item, char *line, size_t size) {
    const char *attempt = !strncmp(item->attempt, "attempt-", 8U) ? item->attempt + 8 : item->attempt;
    line[0] = '\0';
    part(app, line, size, "run ", item->run);
    part(app, line, size, "step ", item->step);
    part(app, line, size, "", attention_is_failure(item) ? item->detail : "-");
    part(app, line, size, "attempt ", attention_present(item->attempt) ? attempt : "-");
    part(app, line, size, "on ", strcmp(item->host, "local") ? item->host : "-");
    /* New items say so at the end; a seen decision says so first, where it
     * stays readable however long the rest is. */
    if (item->unseen && attention_eligible(item)) part(app, line, size, "", "new");
    else if (!item->unseen && attention_needs_decision(item)) {
        char rest[512];
        snprintf(rest, sizeof(rest), "%s", line);
        snprintf(line, size, "seen%s%s", dot(app), rest);
    }
}

static void attention_lines(const struct app *app, const struct attention_item *item, char *title, size_t size, char *line, size_t line_size) {
    const char *stale = !strcmp(item->freshness, "stale") ? "(stale) " : "";
    if (attention_is_approval(item) && attention_present(item->step))
        snprintf(title, size, "%sApprove %s: %s", stale, item->step, item_name(item));
    else if (!strcmp(item->kind, "result")) snprintf(title, size, "%sResult ready: %s", stale, item_name(item));
    else snprintf(title, size, "%s%s: %s", stale, attention_item_title(item), item_name(item));
    where_line(app, item, line, line_size);
}

static const char *repair_hint(const struct recovery *r) {
    if (!strcmp(r->kind, "fleet-unavailable")) return "build or install hydra-fleet to see their work";
    if (!strcmp(r->kind, "observation")) return "its rows are the last observation; r on Hosts retries";
    if (!strcmp(r->kind, "orphan-worktree")) return "x removes it; the branch is kept";
    if (!strcmp(r->kind, "orphan-worktree-dirty")) return "has uncommitted changes, so it is kept";
    if (!strcmp(r->kind, "stale-lock")) return "no work affected; c runs its check";
    if (!strcmp(r->kind, "teardown-failure")) return "c shows what is left";
    return "c runs its check";
}

static void head_lines(const struct app *app, const struct head *h, char *title, size_t size, char *line, size_t line_size) {
    if (head_terminal_gone(app, h)) {
        snprintf(title, size, "Terminal stopped: %s", h->branch);
        snprintf(line, line_size, "worktree and files kept%sR restarts it", dot(app));
        return;
    }
    snprintf(title, size, "State unknown: %s", h->branch);
    snprintf(line, line_size, "Hydra cannot read whether its terminal runs%so opens it", dot(app));
}

static void gate_lines(struct app *app, const struct inbox_entry *e, char *title, size_t size, char *line, size_t line_size) {
    const struct inbox_gate *g = &app->inbox->gates[e->index];
    const struct head *h = head_for_branch(app, g->branch);
    if (g->name[0]) {
        snprintf(title, size, "Approval gate: %s", g->name);
        snprintf(line, line_size, "head %s%sits check passed; Y approves it", g->branch, dot(app));
        return;
    }
    snprintf(title, size, "Approval gates: %s", g->branch);
    snprintf(line, line_size, "%u of %u approved%s: approvals lists them", h ? h->approved : 0U, h ? h->gates : 0U, dot(app));
}

/* "Ready to land: kill-dry-run-worker": a plan run that succeeded, its
 * result on a worker head that is still listed. */
static void land_lines(struct app *app, const struct inbox_entry *e, char *title, size_t size, char *line, size_t line_size) {
    const struct workflow_head *h = &app->workflows->heads[e->index];
    snprintf(title, size, "Ready to land: %s", h->branch);
    snprintf(line, line_size, "plan run %s succeeded%sL lands it", app->workflows->runs[h->run].name, dot(app));
}

static void first_line(const char *text, char *out, size_t size) {
    while (*text == '\n' || *text == ' ') text++;
    snprintf(out, size, "%.*s", (int)strcspn(text, "\n"), text);
}

void inbox_entry_title(struct app *app, const struct inbox_entry *e, char *title, size_t size, char *line, size_t line_size) {
    struct native_inbox *in = app->inbox;
    char detail[1024];
    line[0] = '\0';
    switch (e->kind) {
    case INBOX_ATTENTION: attention_lines(app, &app->attention->items[e->index], title, size, line, line_size); return;
    case INBOX_GATE: gate_lines(app, e, title, size, line, line_size); return;
    case INBOX_PLAN:
        snprintf(title, size, "Plan ready: %s", in->plans[e->index].branch);
        snprintf(line, line_size, "its agent published a plan%sY approves, F sends it back", dot(app));
        return;
    case INBOX_HEAD: head_lines(app, &app->model.heads[e->index], title, size, line, line_size); return;
    case INBOX_LAND: land_lines(app, e, title, size, line, line_size); return;
    case INBOX_RECOVERY:
        recovery_explain(&app->model.recovery[e->index], title, size, detail, sizeof(detail));
        snprintf(line, line_size, "%s", repair_hint(&app->model.recovery[e->index]));
        return;
    default:
        snprintf(title, size, "%s", app->failed[e->index].title);
        first_line(app->failed[e->index].output, line, line_size);
        return;
    }
}

/* ---- the list --------------------------------------------------------------- */

/* A pane: its titled rule on row y, content from row body to bottom. */
struct pane { int x, y, width, bottom, body; bool focused; };

static int row_height(size_t row) { return row < INBOX_ENTRIES ? 2 : 1; }

static void record_hit(struct app *app, const struct pane *p, int y, int height, size_t row) {
    if (app->hit_count >= MAX_HEADS) return;
    app->hit_rows[app->hit_count] = y + 1;
    app->hit_bottom[app->hit_count] = y + height;
    app->hit_left[app->hit_count] = p->x + 1;
    app->hit_right[app->hit_count] = p->x + p->width;
    app->hit_items[app->hit_count++] = row;
}

static void draw_heading(struct app *app, const struct pane *p, int y, enum inbox_section section) {
    char count[24];
    const char *name = inbox_section_name(section);
    int width = kt_text_width(name);
    snprintf(count, sizeof(count), "%zu", app->inbox->section_count[section]);
    kt_text(&app->frame, (struct kt_rect){p->x, y, p->width, 1}, name, KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_BOLD));
    kt_text(&app->frame, (struct kt_rect){p->x + width + 1, y, p->width - width - 1, 1}, count, FAINT);
}

static void draw_group(struct app *app, const struct pane *p, int y, bool selected) {
    char text[160];
    struct native_inbox *in = app->inbox;
    snprintf(text, sizeof(text), "%s Seen %zu  %s", in->seen_open ? glyph(app, "\xe2\x96\xbe", "v") : glyph(app, "\xe2\x96\xb8", ">"),
             in->section_count[INBOX_SEEN], in->seen_open ? "enter hides them" : "marked seen; enter shows them");
    if (selected && p->focused) kt_fill(&app->frame, (struct kt_rect){p->x, y, p->width, 1}, KT_SELECTED);
    kt_text(&app->frame, (struct kt_rect){p->x + (selected ? 1 : 0), y, p->width - 1, 1}, text, selected && p->focused ? KT_SELECTED : KT_MUTED);
    if (selected) kt_text(&app->frame, (struct kt_rect){p->x, y, 1, 1}, glyph(app, "\xe2\x96\x8c", ">"), p->focused ? KT_FOCUS : FAINT);
}

static void draw_entry(struct app *app, const struct pane *p, int y, size_t index, bool selected) {
    const struct inbox_entry *e = &app->inbox->entries[index];
    char title[512], line[512];
    struct kt_style tone, text = KT_BASE, second = KT_MUTED;
    const char *mark = inbox_glyph(app, e, &tone);
    inbox_entry_title(app, e, title, sizeof(title), line, sizeof(line));
    if (selected && p->focused) {
        kt_fill(&app->frame, (struct kt_rect){p->x, y, p->width, 2}, KT_SELECTED);
        tone = text = second = KT_SELECTED;
        text.attrs |= KT_BOLD;
    }
    if (selected) {
        kt_text(&app->frame, (struct kt_rect){p->x, y, 1, 1}, glyph(app, "\xe2\x96\x8c", ">"), p->focused ? KT_FOCUS : FAINT);
        kt_text(&app->frame, (struct kt_rect){p->x, y + 1, 1, 1}, glyph(app, "\xe2\x96\x8c", ">"), p->focused ? KT_FOCUS : FAINT);
    }
    kt_text(&app->frame, (struct kt_rect){p->x + 1, y, 1, 1}, mark, tone);
    kt_text_aligned(&app->frame, (struct kt_rect){p->x + 3, y, p->width - 4, 1}, title, KT_ALIGN_LEFT, text);
    if (y + 1 < p->bottom) kt_text_aligned(&app->frame, (struct kt_rect){p->x + 3, y + 1, p->width - 4, 1}, line, KT_ALIGN_LEFT, second);
}

static void draw_row(struct app *app, const struct pane *p, int y, size_t at) {
    size_t row = app->inbox->rows[at];
    bool selected = at == app->inbox->selected_row;
    if (row == INBOX_ROW_GROUP) draw_group(app, p, y, selected);
    else if (row >= INBOX_ROW_HEADING(INBOX_SECTIONS - 1)) draw_heading(app, p, y, (enum inbox_section)(INBOX_ROW_HEADING(0) - row));
    else draw_entry(app, p, y, row, selected);
    if (row < INBOX_ENTRIES || row == INBOX_ROW_GROUP) record_hit(app, p, y, row_height(row), at);
}

/* Rows scroll by whole items, keeping the selection in view. */
static void list_rows(struct app *app, const struct pane *p, int y) {
    struct native_inbox *in = app->inbox;
    int room = p->bottom - y, offset = 0, start = 0, end;
    size_t i;
    for (i = 0; i < in->row_count; i++) {
        if (i == in->selected_row) { start = offset; break; }
        offset += row_height(in->rows[i]);
    }
    end = start + (in->selected_row < in->row_count ? row_height(in->rows[in->selected_row]) : 1);
    if ((size_t)start < in->scroll) in->scroll = (size_t)start;
    if (end > (int)in->scroll + room) in->scroll = (size_t)(end - room);
    for (i = 0, offset = 0; i < in->row_count && y < p->bottom; i++) {
        int height = row_height(in->rows[i]);
        /* A heading is not left alone on the last row. */
        if (in->rows[i] >= INBOX_ROW_HEADING(INBOX_SECTIONS - 1) && in->rows[i] != INBOX_ROW_GROUP && y + 1 >= p->bottom) break;
        if (offset >= (int)in->scroll) { draw_row(app, p, y, i); y += height; }
        offset += height;
    }
}

static void rule_note(struct app *app, const struct pane *p, const char *note) {
    int width = kt_text_width(note);
    if (width + 16 > p->width) return;
    kt_fill(&app->frame, (struct kt_rect){p->x + p->width - width - 4, p->y, width + 2, 1}, KT_BASE);
    kt_text(&app->frame, (struct kt_rect){p->x + p->width - width - 3, p->y, width, 1}, note, KT_MUTED);
}

static int note(struct app *app, const struct pane *p, int y, const char *text, struct kt_style tone) {
    kt_text_aligned(&app->frame, (struct kt_rect){p->x, y, p->width, 1}, text, KT_ALIGN_LEFT, tone);
    return y + 1;
}

/* Stale: the list is the last good one; one banner across both panes says
 * how old it is and why reading it again failed. */
static void stale_banner(struct app *app, int y) {
    const struct native_attention *a = app->attention;
    char text[TEXT + 96], age[32];
    long seconds = a->good_at ? (long)(time(NULL) - a->good_at) : 0L;
    if (seconds >= 60) snprintf(age, sizeof(age), "%ldm ago", seconds / 60); else snprintf(age, sizeof(age), "%lds ago", seconds);
    snprintf(text, sizeof(text), "Showing the list from %s; reading it failed: %s", age, a->error);
    kt_text_aligned(&app->frame, (struct kt_rect){1, y, app->cols - 2, 1}, text, KT_ALIGN_LEFT, KT_WARNING);
}

/* Notes over the list: the attention read is loading, stale or partial. */
static int list_notes(struct app *app, const struct pane *p, int y) {
    const struct native_attention *a = app->attention;
    char text[TEXT + 96];
    if (a && a->stale) { /* the banner says it */ }
    else if (a && !a->have_good && a->error[0] && !attention_no_runs(app)) {
        kt_text_aligned(&app->frame, (struct kt_rect){p->x, y++, p->width, 1}, a->error, KT_ALIGN_LEFT, KT_WARNING);
        y = note(app, p, y, "Nothing is decided or acknowledged by looking; ctrl+r reads it again.", KT_MUTED);
    } else if (attention_no_runs(app) && app->inbox->row_count) {
        kt_text_aligned(&app->frame, (struct kt_rect){p->x, y++, p->width, 1},
                        "No workflow runs are recorded in this project, so only heads are watched.", KT_ALIGN_LEFT, KT_MUTED);
    } else if ((!a || !a->have_good) && !app->fleet && !attention_no_runs(app)) {
        snprintf(text, sizeof(text), "%s Reading what needs you%s", glyph(app, "\xe2\x97\x8c", "."), glyph(app, "\xe2\x80\xa6", "..."));
        kt_text(&app->frame, (struct kt_rect){p->x, y++, p->width, 1}, text, INFO);
    }
    if (a && a->truncated) y = note(app, p, y, "Only part of the list could be read (a size limit was reached); some items may be missing.", KT_WARNING);
    else if (a && a->partial) y = note(app, p, y, "Some records could not be read completely; they are listed as Needs inspection.", KT_WARNING);
    return y;
}

static void centred(struct app *app, const struct pane *p, int y, const char *text, struct kt_style tone) {
    if (y >= p->bottom) return;
    kt_text_aligned(&app->frame, (struct kt_rect){p->x, y, p->width, 1}, text, KT_ALIGN_CENTER, tone);
}

static void empty_list(struct app *app, const struct pane *p, int y) {
    int top = y + (p->bottom - y) / 2 - 4;
    if (top < y) top = y;
    centred(app, p, top, glyph(app, "\xe2\x9c\x93", "+"), KT_SUCCESS);
    centred(app, p, top + 2, "Nothing needs you", KT_STRONG);
    centred(app, p, top + 4, "Approvals, failed checks, finished results, stopped terminals", KT_MUTED);
    centred(app, p, top + 5, "and repairs appear here, with what you can do about each.", KT_MUTED);
    if (attention_no_runs(app))
        centred(app, p, top + 7, "No workflow runs are recorded in this project, so only heads are watched.", KT_MUTED);
}

static void draw_list(struct app *app, const struct pane *p) {
    struct native_inbox *in = app->inbox;
    char note[64];
    int y;
    size_t waiting = in->count - in->section_count[INBOX_SEEN];
    pane_rule_styled(&app->frame, (struct kt_rect){p->x, p->y, p->width + 1, 1}, "Needs you", p->focused);
    if (app->cols >= WIDE_INBOX) snprintf(note, sizeof(note), "%zu%s%zu seen", waiting, dot(app), in->section_count[INBOX_SEEN]);
    else snprintf(note, sizeof(note), "%zu%senter opens", waiting, dot(app));
    if (in->row_count) rule_note(app, p, note);
    y = list_notes(app, p, p->body);
    if (!in->row_count) { empty_list(app, p, y); return; }
    list_rows(app, p, y);
}

/* ---- the detail --------------------------------------------------------------- */

static void page_strip(struct app *app, const struct pane *p) {
    static const char *const names[] = {"Summary", "Review", "References", "Technical"};
    int x = p->x, i;
    pane_rule_styled(&app->frame, (struct kt_rect){p->x, p->y, p->width + 1, 1}, NULL, p->focused);
    for (i = 0; i < INBOX_PAGES; i++) {
        int width = kt_text_width(names[i]);
        bool active = (int)app->inbox->page == i;
        struct kt_style tone = active ? (p->focused ? KT_FOCUS : KT_STRONG) : KT_MUTED;
        if (x + width + 2 > p->x + p->width) break;
        kt_fill(&app->frame, (struct kt_rect){x, p->y, width + 2, 1}, KT_BASE);
        kt_text(&app->frame, (struct kt_rect){x, p->y, width, 1}, names[i], tone);
        x += width + 2;
    }
}

static void content(struct app *app, const struct pane *p) {
    app->content_x = p->x; app->content_width = p->width;
    app->line = p->body; app->limit = p->bottom;
}

/* A labelled value, wrapped under its label; an absent value adds no row. */
static void label_value(struct app *app, const char *label, const char *value) {
    if (!value || !value[0] || !strcmp(value, "-") || app->line >= app->limit) return;
    labelled(app, label, value, KT_BASE);
}

static void heading(struct app *app, const char *text) {
    if (app->line < app->limit) app->line++;
    style(app, TONE_STRONG); linef(app, "%s", text); style(app, TONE_BASE);
}

/* The keys that act on the item, as the footer names them. */
static void buttons(struct app *app) {
    static const struct { enum key_action action; const char *label; } keys[] = {
        {KEY_INBOX_APPROVE, "Approve"}, {KEY_INBOX_REJECT, "Reject"}, {KEY_INBOX_RESUME, "Resume"},
        {KEY_INBOX_RETURN, "Request changes"}, {KEY_INBOX_LAND, "Land"}, {KEY_INBOX_REVIEW, "Review"},
        {KEY_INBOX_CHECK, "Run check"}, {KEY_INBOX_REMOVE, "Remove"}, {KEY_INBOX_GOTO, "Open in Work"},
        {KEY_INBOX_SEEN, "Seen"}};
    int x = app->content_x;
    size_t i;
    if (app->line + 1 >= app->limit) return;
    app->line++;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        char key[32];
        int width;
        const char *label = keys[i].label;
        if (!inbox_applies(app, keys[i].action) || !key_text(app, keys[i].action, key, sizeof(key))) continue;
        if (keys[i].action == KEY_INBOX_RESUME && inbox_selected(app)->kind == INBOX_HEAD) label = "Restart terminal";
        width = kt_text_width(key) + 1 + kt_text_width(label);
        if (x + width > app->content_x + app->content_width) break;
        kt_text(&app->frame, (struct kt_rect){x, app->line, kt_text_width(key), 1}, key, KT_STRONG);
        kt_text(&app->frame, (struct kt_rect){x + kt_text_width(key) + 1, app->line, kt_text_width(label), 1}, label, KT_MUTED);
        x += width + 4;
    }
    app->line++;
}

static void summary_head(struct app *app, const struct inbox_entry *e) {
    char title[512], line[512];
    struct kt_style tone;
    const char *mark = inbox_glyph(app, e, &tone);
    inbox_entry_title(app, e, title, sizeof(title), line, sizeof(line));
    kt_text(&app->frame, (struct kt_rect){app->content_x, app->line, 1, 1}, mark, tone);
    app->content_x += 2; app->content_width -= 2;
    paragraph(app, title, KT_STRONG);
    app->content_x -= 2; app->content_width += 2;
    if (line[0]) paragraph(app, line, KT_MUTED);
}

/* What the user can do: a failure names only the keys that act on it here. */
static const char *next_text(struct app *app, const struct attention_item *item, char *out, size_t size) {
    if (!attention_is_failure(item) || app->fleet || (attention_present(item->host) && strcmp(item->host, "local")))
        return attention_next_text(app, item);
    snprintf(out, size, "r reviews the failed requirement and its log.%s%s",
             inbox_applies(app, KEY_INBOX_RETURN) ? " F sends it back to the agent that planned the run, with your note." : "",
             inbox_applies(app, KEY_INBOX_RESUME) ? " R resumes the run once it can continue." : "");
    return out;
}

static void attention_summary(struct app *app, const struct attention_item *item) {
    char why[768], request[ATTENTION_TEXT * 3], next[320];
    heading(app, attention_is_failure(item) ? "What failed" : "What is waiting");
    attention_item_why(item, why, sizeof(why));
    paragraph(app, why, KT_BASE);
    if (app->line < app->limit) app->line++;
    snprintf(request, sizeof(request), "%s%sstep %s", item->request, dot(app), item->step);
    label_value(app, "Request", attention_present(item->request) ? request : NULL);
    label_value(app, "Run", item->run);
    label_value(app, "Task", item->task);
    label_value(app, "Host", !strcmp(item->host, "local") || !attention_present(item->host) ? "this machine" : item->host);
    label_value(app, "Status", attention_seen_status(item));
    label_value(app, "Next", next_text(app, item, next, sizeof(next)));
}

static void recovery_summary(struct app *app, const struct recovery *r) {
    char title[TEXT + 64], detail[1024];
    recovery_explain(r, title, sizeof(title), detail, sizeof(detail));
    heading(app, "What happened");
    if (!strcmp(r->kind, "fleet-unavailable"))
        copy_text(detail, sizeof(detail), "Remote hosts are registered, but the optional hydra-fleet helper is not built or "
                  "installed, so their work cannot appear here. Nothing on those hosts is affected. Build it in the Hydra "
                  "source checkout (make build-fleet) or install it; their heads then appear in Work.");
    paragraph(app, detail, KT_BASE);
    if (app->line < app->limit) app->line++;
    label_value(app, !strcmp(r->kind, "fleet-unavailable") ? "Run" : "Check", r->action);
    label_value(app, "Hosts", !strcmp(r->kind, "fleet-unavailable") ? r->label : NULL);
}

static void land_summary(struct app *app, const struct inbox_entry *e) {
    const struct workflow_head *h = &app->workflows->heads[e->index];
    const struct workflow_run *run = &app->workflows->runs[h->run];
    char text[1024];
    heading(app, "What is waiting");
    snprintf(text, sizeof(text), "Plan run %.80s succeeded; its result is on the worker branch %.120s. L lands it: hydra land "
             "merges the commit its checks verified into your current branch, removes the worker head and keeps its "
             "branch. Nothing is pushed, and Hydra refuses while your checkout has tracked changes.", run->name, h->branch);
    paragraph(app, text, KT_BASE);
    if (app->line < app->limit) app->line++;
    label_value(app, "Worker", h->branch);
    label_value(app, "Run", run->id);
    label_value(app, "Planned by", run->planning);
}

static void head_summary(struct app *app, const struct head *h) {
    char text[1024];
    heading(app, "What happened");
    if (head_terminal_gone(app, h))
        snprintf(text, sizeof(text), "The terminal session for %.200s is no longer running. Its worktree and files are kept; "
                 "nothing was removed from the repository. R restarts its terminal and agent in the same worktree "
                 "(hydra resume --terminal); o opens the head in Work.", h->branch);
    else snprintf(text, sizeof(text), "Hydra cannot read whether the terminal of %.200s runs (its recorded state is %.40s). "
                  "o opens the head in Work; : doctor inspects the state.", h->branch, h->liveness);
    paragraph(app, text, KT_BASE);
    if (app->line < app->limit) app->line++;
    label_value(app, "Head", h->branch);
    label_value(app, "Agent", h->profile);
}

static void other_summary(struct app *app, const struct inbox_entry *e) {
    const struct native_inbox *in = app->inbox;
    char text[1024];
    heading(app, "What is waiting");
    if (e->kind == INBOX_GATE && !in->gates[e->index].name[0])
        snprintf(text, sizeof(text), "The head %.200s has approval gates that are not approved yet. Their records cannot "
                 "be read here; : approvals lists them (hydra gate status), and hydra gate approve approves one.",
                 in->gates[e->index].branch);
    else if (e->kind == INBOX_GATE)
        snprintf(text, sizeof(text), "The head %.200s recorded the gate %s, and its check passed. Y approves it as you "
                 "(hydra gate approve); Hydra refuses if the head's commit or worktree changed since the check ran.",
                 in->gates[e->index].branch, in->gates[e->index].name);
    else if (e->kind == INBOX_PLAN)
        snprintf(text, sizeof(text), "The planning agent of %.200s published a plan. Y opens it under the guided policy and "
                 "asks you to approve exactly this revision; F sends it back with your note; o shows it in Work. "
                 "Nothing runs until you approve.", in->plans[e->index].branch);
    else snprintf(text, sizeof(text), "This action did not finish this session. Enter shows its whole output; s puts it "
                  "away under Seen.");
    paragraph(app, text, KT_BASE);
    if (e->kind == INBOX_PLAN) { if (app->line < app->limit) app->line++; label_value(app, "Draft", in->plans[e->index].draft); }
}

static void summary_page(struct app *app, const struct inbox_entry *e) {
    summary_head(app, e);
    if (e->kind == INBOX_ATTENTION) attention_summary(app, &app->attention->items[e->index]);
    else if (e->kind == INBOX_RECOVERY) recovery_summary(app, &app->model.recovery[e->index]);
    else if (e->kind == INBOX_HEAD) head_summary(app, &app->model.heads[e->index]);
    else if (e->kind == INBOX_LAND) land_summary(app, e);
    else other_summary(app, e);
    buttons(app);
}

/* The exact identity, one labelled row per field; the full digests take
 * rows of their own (a 64-digit digest and an 11-wide label fit 80 columns). */
static void attention_technical(struct app *app, const struct attention_item *item) {
    const char *labels[] = {"Source", "Project", "Host", "Task", "Run", "Step", "Attempt", "Head", "Instance", "Request", "Binding", "Freshness"};
    const char *values[] = {item->source, item->project, item->host, item->task, item->run, item->step, item->attempt,
                            item->head, item->instance, item->request, item->binding, item->freshness};
    char kind[400], route[160];
    size_t i;
    snprintf(kind, sizeof(kind), "%s%s%s", item->kind, dot(app), item->reason);
    snprintf(route, sizeof(route), "%s%s%s", item->route_kind, dot(app), !strcmp(item->navigable, "1") ? "navigable" : "not navigable");
    style(app, TONE_STRONG); linef(app, "IDENTITY"); style(app, TONE_BASE);
    labelled(app, "Kind", kind, KT_BASE);
    for (i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
        labelled(app, labels[i], values[i], i == 11 && strcmp(item->freshness, "fresh") ? KT_WARNING : KT_BASE);
    labelled(app, "Route", route, KT_BASE);
    labelled_at(app, DETAIL_LABEL - 1, "Revision", item->revision, KT_MUTED);
    labelled_at(app, DETAIL_LABEL - 1, "Identity", item->identity, KT_MUTED);
}

static void technical_heading(struct app *app, const char *text) {
    if (app->line > app->inbox->detail_top && app->line < app->limit) app->line++;
    style(app, TONE_STRONG); linef(app, "%s", text); style(app, TONE_BASE);
}

static void recovery_technical(struct app *app, const struct recovery *r) {
    technical_heading(app, "DIAGNOSTICS");
    labelled(app, "Kind", r->kind, KT_MUTED);
    labelled(app, "Source", r->source, KT_MUTED);
    labelled(app, "Confidence", r->confidence, KT_MUTED);
    labelled(app, "Inspect", r->action, KT_BASE);
}

/* A head's identity and recorded state, then each finding about it. */
static void head_technical(struct app *app, const struct head *h) {
    size_t i;
    technical_heading(app, "HEAD");
    labelled(app, "Head", h->branch, KT_BASE);
    labelled(app, "ID", h->head_id[0] ? h->head_id : "-", KT_MUTED);
    labelled(app, "Instance", h->instance[0] ? h->instance : "-", KT_MUTED);
    labelled(app, "Liveness", h->liveness, KT_BASE);
    labelled(app, "Desired", h->desired, KT_BASE);
    labelled(app, "Observed", h->observed, KT_BASE);
    labelled(app, "Source", h->source, KT_MUTED);
    for (i = 0; i < app->model.recovery_count; i++)
        if (!strcmp(app->model.recovery[i].label, h->branch)) recovery_technical(app, &app->model.recovery[i]);
}

static void land_technical(struct app *app, const struct inbox_entry *e) {
    const struct workflow_head *h = &app->workflows->heads[e->index];
    technical_heading(app, "RESULT");
    labelled(app, "Worker", h->branch, KT_BASE);
    labelled(app, "Step", h->step, KT_BASE);
    labelled(app, "Role", h->role, KT_BASE);
    labelled(app, "Run", app->workflows->runs[h->run].id, KT_MUTED);
    labelled(app, "Digest", app->workflows->runs[h->run].digest, KT_MUTED);
}

static void technical_page(struct app *app, const struct inbox_entry *e) {
    const struct native_inbox *in = app->inbox;
    app->inbox->detail_top = app->line;
    switch (e->kind) {
    case INBOX_ATTENTION: attention_technical(app, &app->attention->items[e->index]); break;
    case INBOX_RECOVERY: recovery_technical(app, &app->model.recovery[e->index]); break;
    case INBOX_HEAD: head_technical(app, &app->model.heads[e->index]); break;
    case INBOX_GATE:
        technical_heading(app, "GATE");
        labelled(app, "Head", in->gates[e->index].branch, KT_BASE);
        labelled(app, "Gate", in->gates[e->index].name[0] ? in->gates[e->index].name : "unreadable here", KT_BASE);
        break;
    case INBOX_PLAN:
        technical_heading(app, "PLAN");
        labelled(app, "Head", in->plans[e->index].branch, KT_BASE);
        labelled(app, "Draft", in->plans[e->index].draft, KT_MUTED);
        break;
    case INBOX_LAND: land_technical(app, e); break;
    default: paragraph(app, app->failed[e->index].output, KT_BASE); break;
    }
}

static void review_page(struct app *app, bool references) {
    if (native_review_render(app, references)) return;
    style(app, TONE_MUTED);
    paragraph(app, references ? "r reads the exact review; its references, and the logs of its checks, are listed here."
              : "r reads the exact review of this item: what it produced or why it failed, and how it was checked.", KT_MUTED);
    style(app, TONE_BASE);
}

static void draw_detail(struct app *app, const struct pane *p) {
    const struct inbox_entry *e = inbox_selected(app);
    page_strip(app, p);
    content(app, p);
    if (!e) { style(app, TONE_MUTED); paragraph(app, "Select an item to see what it needs.", KT_MUTED); style(app, TONE_BASE); return; }
    switch (app->inbox->page) {
    case INBOX_REVIEW: review_page(app, false); break;
    case INBOX_REFERENCES: review_page(app, true); break;
    case INBOX_TECHNICAL: technical_page(app, e); break;
    default: summary_page(app, e); break;
    }
}

void inbox_render(struct app *app) {
    struct native_inbox *in;
    struct pane list, detail;
    int top = 2, bottom = app->rows - 2, body = top + 1;
    inbox_build(app);
    if (!(in = app->inbox)) return;
    if (app->attention && app->attention->stale && body + 2 < bottom) stale_banner(app, body++);
    /* --diagnostics opens the Technical page. */
    if (app->diagnostics) { in->page = INBOX_TECHNICAL; app->diagnostics = false; }
    if (!inbox_selected(app) && in->page != INBOX_SUMMARY) in->page = INBOX_SUMMARY;
    /* With nothing listed there is no detail: the empty state takes the width. */
    if (app->cols >= WIDE_INBOX && in->row_count) {
        in->detail_open = false;
        list = (struct pane){1, top, app->cols * 44 / 100, bottom, body, !in->focus_detail};
        detail = (struct pane){list.x + list.width + 3, top, 0, bottom, body, in->focus_detail};
        detail.width = app->cols - detail.x - 1;
        draw_list(app, &list);
        draw_detail(app, &detail);
        return;
    }
    in->focus_detail = false;
    list = (struct pane){1, top, app->cols - 2, bottom, body, true};
    if (in->detail_open && inbox_selected(app)) draw_detail(app, &list);
    else { in->detail_open = false; draw_list(app, &list); }
}

void inbox_status(struct app *app, char *out, size_t size) {
    size_t count;
    bool failure, known;
    inbox_badge(app, &count, &failure, &known);
    if (!known) return;
    /* A stale list is the last good one: nobody is counted from it. */
    if (app->attention && app->attention->stale) text_append(out, size, "  the last good list; reading it again failed");
    else if (!count) text_append(out, size, "  nothing waiting");
    else text_append(out, size, "  %zu item%s need%s you%sdecisions first, then failures", count,
                     count == 1 ? "" : "s", count == 1 ? "s" : "", dot(app));
}
