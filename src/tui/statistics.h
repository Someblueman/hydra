#ifndef HYDRA_TUI_STATISTICS_H
#define HYDRA_TUI_STATISTICS_H
#include "statistics_cohorts.h"
/* The Stats screen: one frame shared by its pages (head sessions, workflow
 * runs, remote cohorts when the feed records remote steps, and in fleet mode
 * hosts). From the top: a page strip in a titled
 * rule with what the filters cover at its right, four tiles (or one summary
 * line when narrow), notes saying how the numbers are known, two charts when
 * tall, then the page's list. Enter opens the selected row's evidence: beside
 * the list when wide, in its place otherwise. */

/* Mouse targets of the page strip, after the list's row numbers. */
#define STATS_HIT_SESSIONS ((size_t)-2)
#define STATS_HIT_RUNS ((size_t)-3)
#define STATS_HIT_COHORTS ((size_t)-4)

/* The local pages, in strip order; [ and ] go through the ones the sample
 * has (statistics_pages). */
enum stats_page { STATS_SESSIONS, STATS_RUNS, STATS_COHORTS };

struct stats_frame {
    int strip;              /* row of the page strip                          */
    int top;                /* first row below the strip                      */
    bool tiles, summary;    /* four tiles, or one summary line instead        */
    bool charts;            /* the two charts fit and there is data           */
    bool side;              /* evidence beside the list rather than over it   */
    int width;              /* columns from x = 1 to the frame's last column  */
};

struct stats_tile { const char *label, *value, *note; struct kt_style tone; };

/* A list column: its heading, its width and the narrowest list that shows
 * it. The last column shown takes what is left. */
struct stats_column { const char *title; int width, from; };
/* One cell's text into out; color is the cell's tone (KT_DEFAULT: the row's). */
typedef void (*stats_cell_fn)(void *context, size_t row, int column, char *out, size_t size, kt_color *color);
struct stats_list {
    const char *title, *empty, *order;   /* order: how rows are sorted, or NULL */
    const struct stats_column *columns;
    size_t column_count, count, selected;
    stats_cell_fn cell;
    void *context;
    bool focused;
};

/* Where everything goes for a page with `notes` note lines; data says the
 * page has something in scope to chart. */
void stats_layout(const struct app *app, const struct kt_canvas *c, int notes, bool data, struct stats_frame *f);
/* The row the list's rule takes after the parts above it. */
int stats_list_row(const struct stats_frame *f, int notes);
/* The list area and, when open, the evidence area from the list's row down. */
void stats_areas(const struct app *app, const struct kt_canvas *c, const struct stats_frame *f, int row,
                 struct kt_rect *list, struct kt_rect *evidence);
void stats_strip(struct app *app, struct kt_canvas *c, const struct stats_frame *f);
void stats_tiles(struct kt_canvas *c, int y, int width, const struct stats_tile tiles[4]);
void stats_note(struct kt_canvas *c, int y, int width, const char *text);
/* A bar of fraction (0 to 1) of an area's width. */
void stats_fraction(struct kt_canvas *c, struct kt_rect r, double fraction);
/* One chart row: a label, a count and a bar of value over maximum. */
void stats_bar(struct kt_canvas *c, int x, int y, int width, const char *label, size_t count, double fraction);
void stats_rule(struct kt_canvas *c, struct kt_rect r, const char *title, const char *note, bool focused);
/* The list with its rule, the bar on the selection and the filter's matches
 * marked; records each row for the mouse. */
void stats_list_view(struct app *app, struct kt_canvas *c, struct kt_rect area, const struct stats_list *l);
/* Why a list is empty: nothing recorded (what, then how it gets recorded) or
 * every row filtered out. */
void stats_empty(struct app *app, struct kt_canvas *c, struct kt_rect area, const char *title, size_t recorded,
                 const char *nothing, const char *how);
/* The status line: the freshness chip, then an error, a notice, the
 * sample's warning or what the page covers. */
void stats_status(struct app *app, struct kt_canvas *c, const char *page, bool headless);
/* What the filters cover: "all recorded · all heads" and the like; with
 * location, the location filter as well (the strip shows it as a chip). */
void stats_scope(const struct app *app, bool location, char *out, size_t size);
const char *stats_range(const struct statistics_view *v);
void stats_time(uint64_t epoch, bool known, char *out, size_t size);
void stats_seconds(uint64_t seconds, bool known, char *out, size_t size);
void stats_percent(uint64_t part, uint64_t whole, char *out, size_t size);
/* Whether the evidence has the keys: open, and either over the list or
 * focused beside it. */
bool stats_evidence_focused(const struct app *app);
bool stats_side(const struct app *app);
/* Both pages' frames end the same way. */
void stats_finish(struct app *app, struct kt_canvas *c, const char *page, unsigned frame, bool headless);
bool stats_begin(struct app *app, bool headless, struct kt_canvas *c);

/* The pages this sample has: head sessions from schema 6, runs, and
 * cohorts when a step ran as a remote task. Returns how many. */
size_t statistics_pages(const struct app *app, enum stats_page pages[3]);
enum stats_page statistics_page(const struct app *app);
void statistics_page_show(struct statistics_view *v, enum stats_page page);
bool statistics_cohorts_shown(const struct app *app);
/* The cohorts page's rows for the filters, and the selection kept in range. */
size_t statistics_cohort_count(struct statistics_view *v);

/* The pages. */
void stats_runs_page(struct app *app, struct kt_canvas *c);
void stats_sessions_page(struct app *app, struct kt_canvas *c);
void stats_fleet_page(struct app *app, struct kt_canvas *c);
void stats_cohorts_page(struct app *app, struct kt_canvas *c);
void stats_run_evidence(struct app *app, struct kt_canvas *c, struct kt_rect r, bool focused);
#endif
