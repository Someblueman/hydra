#ifndef HYDRA_TUI_INBOX_H
#define HYDRA_TUI_INBOX_H
/* Inbox (view 9): everything waiting for a human, in one list grouped by
 * what it needs, decisions first and repairs last, each acted on where it is
 * listed. inbox.c builds the list from the attention read, the head snapshot
 * (stopped terminals, approval gates, published plans), the recovery
 * findings and the in-app actions that failed this session, and performs
 * its actions; inbox_render.c draws it. Include after internal.h. */
#include "keys.h"

enum inbox_section {
    INBOX_DECISIONS, INBOX_FAILED, INBOX_RESULTS, INBOX_AGENTS, INBOX_REPAIRS, INBOX_ACTIONS, INBOX_SEEN,
    INBOX_SECTIONS
};
enum inbox_kind { INBOX_ATTENTION, INBOX_GATE, INBOX_PLAN, INBOX_HEAD, INBOX_RECOVERY, INBOX_FAILED_ACTION, INBOX_LAND };
enum inbox_page { INBOX_SUMMARY, INBOX_REVIEW, INBOX_REFERENCES, INBOX_TECHNICAL, INBOX_PAGES };
/* One listed item: what it is and where its data lives (index into the
 * attention items, heads, gates, plans, recovery findings, failed actions or,
 * for a result ready to land, the run's worker head). */
struct inbox_entry { enum inbox_kind kind; enum inbox_section section; size_t index; };
/* An approval gate recorded under a head and not approved yet, and a plan
 * the head's agent published, read from the state store at each refresh. */
#define INBOX_GATES 64
#define INBOX_PLANS 64
struct inbox_gate { char branch[TEXT], name[64]; };
struct inbox_plan { char branch[TEXT], draft[512]; };
#define INBOX_ENTRIES (NATIVE_ATTENTION_ITEMS + 2 * MAX_HEADS + MAX_RECOVERY + FAILED_ACTIONS + INBOX_GATES + INBOX_PLANS + WF_RUNS)
/* A row of the list: an entry, a section heading, or the Seen group. */
#define INBOX_ROW_GROUP ((size_t)-1)
#define INBOX_ROW_HEADING(section) ((size_t)-2 - (size_t)(section))
struct native_inbox {
    struct inbox_entry entries[INBOX_ENTRIES];
    size_t count, section_count[INBOX_SECTIONS];
    size_t rows[INBOX_ENTRIES + INBOX_SECTIONS + 1], row_count;
    struct inbox_gate gates[INBOX_GATES];
    struct inbox_plan plans[INBOX_PLANS];
    size_t gate_count, plan_count;
    /* The selected item by identity (see inbox_key), and its row, which a
     * vanished item's successor takes. */
    char selected[ATTENTION_TEXT + TEXT + 72];
    size_t selected_row, scroll, detail_scroll;
    int detail_top; /* the detail's first content row in the last frame */
    enum inbox_page page;
    /* focus_detail: keys go to the detail pane; detail_open: below 100
     * columns the detail replaces the list. jumped: o opened Work from here,
     * so Esc there comes back. chosen: the user moved the selection; until
     * then it is the first unseen item of the first section with items. */
    bool focus_detail, detail_open, seen_open, jumped, chosen;
};

/* The list as it is now, rebuilt from its sources; the selection follows
 * its item. */
void inbox_build(struct app *app);
/* The badge on the Inbox tab: how many items wait (not counting Seen), and
 * whether one is a failure. known is false until the attention read first
 * answered. */
void inbox_badge(struct app *app, size_t *count, bool *failure, bool *known);
/* The selected entry, or NULL (nothing listed, or the Seen group). */
const struct inbox_entry *inbox_selected(struct app *app);
/* Rereads the gates and published plans from the state store (a refresh). */
void inbox_refresh(struct app *app);
/* The binding table's actions for Inbox and its review pages: whether one
 * applies to the selection, and performing it. inbox_perform returns false
 * for an action that is not Inbox's. */
bool inbox_applies(const struct app *app, enum key_action action);
bool inbox_perform(struct app *app, enum key_action action);
/* The chain the focused part of Inbox is in. */
enum key_chain_id inbox_chain(const struct app *app);
/* Esc: one step back inside Inbox; false when Inbox has nothing to close. */
bool inbox_back(struct app *app);
/* A mouse report on a row recorded by the last frame. */
void inbox_click(struct app *app, size_t row, unsigned button);
void inbox_destroy(struct app *app);
/* A recovery finding's recorded check (c), and removing a leftover worktree
 * through hydra gc after a confirmation (x). */
void recovery_check_action(struct app *app, size_t index);
void recovery_remove_action(struct app *app, size_t index);
/* Drawing (inbox_render.c). */
void inbox_render(struct app *app);
/* Appends what the status line says on Inbox to out. */
void inbox_status(struct app *app, char *out, size_t size);
void inbox_entry_title(struct app *app, const struct inbox_entry *e, char *title, size_t size, char *line, size_t line_size);
const char *inbox_glyph(const struct app *app, const struct inbox_entry *e, struct kt_style *tone);
const char *inbox_section_name(enum inbox_section section);
#endif
