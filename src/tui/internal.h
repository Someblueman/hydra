#ifndef HYDRA_TUI_INTERNAL_H
#define HYDRA_TUI_INTERNAL_H
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <langinfo.h>
#include "../kettle/kettle.h"
#include "../kettle/workspace.h"
#include "../kettle/tree.h"
#include "../kettle/terminal.h"
#include "../kettle/pty_posix.h"
#include "../kettle/input.h"
#include "../kettle/keymap.h"
#include "../kettle/program.h"
#include "../hydra_tui_workflow.h"
#include "../hydra_statistics.h"

#include "app.h"
#include "review.h"
#include "process.h"
#include "terminal.h"
#include "transcript.h"
extern char **environ;
#define HYDRA_TUI_VERSION "3.0.0"
#define HYDRA_TUI_PROTOCOL 2
#define NATIVE_PLAN_LIMIT (256U*1024U)
#define NATIVE_PLAN_TEXT (1024U*1024U)
#define NATIVE_EVIDENCE_LIMIT (1024U*1024U)
#define NATIVE_TERMINALS 4
#define NATIVE_TERMINAL_CELLS (512U * 256U)
#define WORKSPACE_CAPACITY (512U * 256U)
#define NATIVE_ATTENTION_ITEMS 512U
#define NATIVE_ATTENTION_SEEN 512U
struct native_capture {
    pid_t pid;
    int fd, status;
    FILE *output;
    struct timespec started, stop_started;
    long budget_ms;
    size_t bytes;
    bool eof, reaped, failed, timed_out, stopping;
};
enum tone { TONE_BASE, TONE_BORDER, TONE_TITLE, TONE_SELECTED, TONE_WARNING, TONE_STRONG, TONE_SUCCESS, TONE_MUTED, TONE_FOCUS };
struct statistics_view {
    struct hs_model *model;
    struct hs_filter filter;
    size_t selected, visible[HS_RUNS], count;
    size_t step_scroll;
    int back_view;
    unsigned metric_page;
    bool stale, detail, graph_open;
    char error[TEXT], selected_id[128];
    /* The run whose graph opens when the workflow read asked for arrives. */
    char graph_run[128];
    /* The head sessions page (statistics_sessions.c): whether it is shown
     * rather than workflow runs, whether that has been chosen (by the first
     * sample or the user), and its rows. */
    bool sessions, sessions_chosen;
    size_t session_rows[HS_SESSIONS], session_count, session_selected;
    char session_id[80];
    /* typing: the filter has the keys (statistics_input.c); evidence_focus:
     * the evidence beside the list has them; head_open: g showed a
     * session's head in Details, so Esc there comes back. received: when
     * the sample arrived, for the status line's freshness. */
    bool typing, evidence_focus, head_open;
    time_t received;
};
/* RETURNED: the user requested changes to this exact draft; it cannot be
 * validated or executed until the agent publishes a different draft. */
enum native_plan_state { PLAN_DRAFT, PLAN_VALIDATING, PLAN_INVALID, PLAN_READY, PLAN_UNAVAILABLE, PLAN_RETURNED };
#define NATIVE_PLAN_NOTICES 24
struct native_plan {
    char path[4096], policy[4096], directory[4096], compiled[4096];
    char digest[65], objective[4096], notice[TEXT], proposal_head[TEXT];
    unsigned revision, compilation;
    enum native_plan_state state;
    struct native_capture job;
    struct native_capture launch_job;
    pid_t owner_pid;
    char launched_digest[65], launched_run[65], launch_state[32];
    /* Why a launch ended without a run, as the launch owner reported it. */
    char launch_failure[512];
    /* The planning conversation bound at launch, and run notices typed into it. */
    char planning_head[TEXT], planning_instance[TEXT], notified[NATIVE_PLAN_NOTICES][112];
    size_t notified_count;
    unsigned launched_revision;
    /* The revision approved before its validation finished; it runs when
     * that validation is ready (plan_actions.c). 0: none. */
    unsigned approved_revision;
    bool follow_launched_run;
    bool projecting;
    char *source_bytes, *policy_bytes, *text;
    size_t source_length, policy_length, text_length, text_scroll, selected;
    unsigned long long assets_stamp;
    struct workflow_model graph;
};
struct native_evidence {
    struct native_capture job;
    char run[80], step[65];
    char *text;
    size_t length;
    unsigned verdict;
    bool stale;
};
struct native_links {
    char project[128], root[SOURCE_TEXT];
    struct { char run[80], branch[TEXT]; } refs[512];
    size_t count;
    bool stale;
};
/* The sources read in the background (observations.c); attention keeps its
 * own request rules (attention.c). OBSERVE_REMOTE is the remote snapshot of
 * the registered hosts (U11). */
enum { OBSERVE_HEADS, OBSERVE_WORKFLOWS, OBSERVE_STATISTICS, OBSERVE_LINKS, OBSERVE_ATTENTION, OBSERVE_REMOTE, OBSERVE_SOURCES };
/* again: a read asked for while its source was being read; it starts when
 * that read ends, so the newest request is answered and reads never pile up.
 * preview reads the selected head's pane or step output (adapter.c) for the
 * head and kind it names. */
struct native_observations {
    struct native_capture jobs[OBSERVE_SOURCES];
    bool again[OBSERVE_SOURCES];
    /* The last complete CLI read of a workflow source: the stamp of the runs
     * it read (asked: of the read in flight), and for how many refreshes it
     * has been shown again without asking (observations.c). */
    struct { bool valid; unsigned long long stamp, asked; unsigned reused; } kept[OBSERVE_SOURCES];
    struct native_capture preview;
    char preview_branch[TEXT];
    bool preview_headless, preview_again;
};
struct native_terminal {
    struct kt_pty client;
    struct kt_terminal_model *screen;
    struct kt_cell *cells, *history;
    char head[TEXT], instance[TEXT], label[TEXT];
    char remote_host[128], remote_project[SOURCE_TEXT];
    size_t scroll;
    bool scrolling;
    /* A remote client was sent into its host's tmux copy mode by Ctrl-B [. */
    bool remote_history;
    /* tmux sized the window to another, smaller client: the drawn view shows
     * tmux's dotted fill beyond that window (set while drawing). */
    bool foreign_size;
    /* Hydra-authored input waiting for the attached client to come up; a
     * submitted entry sends Enter as a separate, later keystroke. */
    char *outbox[4];
    bool outbox_submit[4], received, enter_pending;
    size_t outbox_count;
    struct timespec outbox_at;
    /* Input beyond what the client's queue holds (a large paste), sent on as
     * the client reads; later keys queue behind it so order is kept. */
    char *backlog;
    size_t backlog_length, backlog_sent;
    /* The tmux command that enters or leaves the pane's history, and the one
     * asked for while it ran. Keys typed meanwhile are held in the backlog,
     * so they reach the pane only once it has changed mode. */
    struct native_capture history_job;
    int history_op, history_next;
    bool held;
};
struct native_terminals {
    struct native_terminal slots[NATIVE_TERMINALS];
    size_t selected;
};
/* Work (view 7): the list of heads and runs, the selected head's
 * conversation and a side pane of pages (work.c). The layout's leaves are the
 * list (pane 1), the conversation (3) and the side pane (4); below 120
 * columns the conversation's pane shows the side pane's pages too, and below
 * 70x20 one pane fills the screen. chosen is the page the user picked for
 * the selection named by chosen_for, or -1 for that selection's default. */
enum work_page { WORK_CONVERSATION, WORK_DETAILS, WORK_PLAN, WORK_RUN, WORK_PAGES };
enum { WORK_PANE_LIST = 1, WORK_PANE_CONVERSATION = 3, WORK_PANE_SIDE = 4 };
struct native_workspace {
    struct kt_workspace layout;
    /* Two agents side by side in the conversation's pane: the first of
     * the two leaves and the terminal slot each shows. */
    struct { int first; size_t slots[2]; } agents;
    int chosen;
    char chosen_for[TEXT + 32];
    size_t scroll[WORK_PAGES], list_top;
    /* Where the last frame drew each page's name, for clicks. */
    struct work_page_hit { int x, y, width, page; } page_hits[8];
    size_t page_hit_count;
    /* Rows of recorded evidence the Run page showed: PgUp/PgDn page by it. */
    int evidence_rows;
    /* A run selected only because nothing else was listed: the first head
     * to arrive takes the selection from it. */
    bool run_auto;
    int theme;
    bool zoom, compact, merged, dragged;
};
bool native_workspace_init(struct app *app);
void native_workspace_destroy(struct app *app);
bool work_render(struct app *app, unsigned frame, bool headless);
void work_frame_mode(struct app *app);
void work_arrange(struct app *app, struct kt_rect area);
/* The pages: which apply to the selection, the one each pane shows (side:
 * the side pane, else the shared pane), the user's choice and [ ]. */
bool work_page_available(const struct app *app, int page);
int work_page_shown(const struct app *app, bool side);
int work_page(const struct app *app);
int work_focused_page(const struct app *app);
void work_set_page(struct app *app, int page);
void work_page_step(struct app *app, int direction);
void work_pane_step(struct app *app, int direction);
void work_focus_list(struct app *app);
void work_focus_page(struct app *app, int page);
/* The selection moved: its pages start at the top and its output is read. */
void work_selection_changed(struct app *app);
void work_open(struct app *app, int page);
bool work_back(struct app *app);
enum key_chain_id work_chain(const struct app *app);
bool work_perform(struct app *app, enum key_action action);
bool work_applies(const struct app *app, enum key_action action, bool *applies);
const char *work_open_label(const struct app *app);
void work_mouse(struct app *app, unsigned button, int x, int y, bool release);
void work_page_hits_clear(struct app *app);
void work_page_hit(struct app *app, int page, int x, int y, int width);
bool work_page_click(struct app *app, int x, int y);
void work_rule(struct app *app, struct kt_rect r, const char *title, bool focused, const char *note);
/* The list (work_list.c), its glyphs, and whether remote work is listed. */
void work_list_draw(struct app *app, struct kt_rect r, bool focused);
const char *work_head_glyph(const struct app *app, const struct head *h, struct kt_style *tone);
const char *work_state_glyph(const struct app *app, const char *state, struct kt_style *tone);
const char *work_loading_glyph(const struct app *app);
bool remote_listed(const struct app *app);
/* The conversation (work_conversation.c). */
const struct head *work_output_head(struct app *app);
struct native_terminal *work_terminal(struct app *app);
bool work_preview_wanted(struct app *app);
void work_conversation_note(struct app *app, bool focused, char *out, size_t size);
void work_conversation_draw(struct app *app, struct kt_rect r, bool focused);
void work_agent_draw(struct app *app, struct kt_rect r, struct native_terminal *t, bool focused);
/* The side pane's pages (work_pages.c) and what the Plan and Run pages act on. */
void work_page_draw(struct app *app, int page, struct kt_rect r, bool focused);
size_t work_run(const struct app *app);
bool work_run_shown(const struct app *app);
bool work_head_plan(const struct app *app, const struct head *h);
bool work_head_plan_ready(const struct app *app, const struct head *h);
bool work_land_worker(const struct app *app, char *out, size_t size);
const char *work_plan_run(const struct app *app);
void work_land(struct app *app);
/* Work outline (outline.c): heads the user started, the runs they launched
 * and the heads each run created. */
enum outline_kind { OUTLINE_HEAD, OUTLINE_RUN, OUTLINE_RETIRED, OUTLINE_TASK };
struct outline_row { enum outline_kind kind; size_t head, run, record; int depth; bool open; };
#define OUTLINE_ROWS (MAX_HEADS + WF_RUNS + WF_HEADS + MAX_TASKS)
struct work_counts { size_t heads, run_heads, running, attention, runs, runs_active, remote; };
bool head_headless(const struct head *h);
/* One workspace across local and remote execution (U11): remote.c composes
 * the model from the local and remote observations and keeps a disconnected
 * host's rows as its last observation; location.c filters by location;
 * remote_text.c words remote rows and hosts. */
struct remote_counts { size_t heads, tasks, hosts, hosts_down, retained; };
bool head_remote(const struct head *h);
const struct host_observation *host_for_name(const struct app *app, const char *name);
void model_accept(struct app *app, struct model *next);
void model_release(struct app *app);
void remote_failed(struct app *app, const char *error);
long remote_age(const struct app *app);
const char *remote_action_refusal(const struct app *app, const struct head *h, char *out, size_t size);
void remote_counts(const struct app *app, struct remote_counts *n);
bool location_remote_only(const struct app *app);
bool head_visible(const struct app *app, const struct head *h);
bool task_matches(const struct task_observation *t, const char *search);
bool task_visible(const struct app *app, const struct task_observation *t);
const struct task_observation *selected_task(const struct app *app);
const char *location_label(const struct app *app, char *out, size_t size);
void location_set(struct app *app, const char *value);
void location_cycle(struct app *app);
const char *head_where(const struct head *h);
const char *remote_head_status(const struct app *app, const struct head *h);
const char *remote_agent_name(const struct head *h);
struct kt_style remote_head_tone(const struct app *app, const struct head *h);
void remote_observation_text(const struct app *app, const struct head *h, char *out, size_t size);
void host_state_text(const struct app *app, const struct host_observation *host, char *out, size_t size);
const char *task_status(const struct app *app, const struct task_observation *t);
struct kt_style task_tone(const struct app *app, const struct task_observation *t);
bool remote_footer_text(const struct app *app, char *out, size_t size, struct kt_style *tone);
void remote_retry(struct app *app, const char *host);
size_t head_owner_run(const struct app *app, const struct head *h, size_t *record);
size_t run_owner_head(const struct app *app, size_t run);
const struct workflow_node *head_step(const struct app *app, const struct head *h);
bool head_needs_attention(const struct app *app, const struct head *h);
bool run_needs_attention(const struct app *app, size_t run);
void work_counts(const struct app *app, struct work_counts *n);
bool run_expanded(const struct app *app, size_t run);
void run_set_expanded(struct app *app, size_t run, bool expand);
size_t outline_build(struct app *app, struct outline_row *rows, size_t capacity);
size_t outline_position(const struct app *app, const struct outline_row *rows, size_t count);
void outline_select(struct app *app, const struct outline_row *row);
void outline_move(struct app *app, int direction, bool heads_only);
bool outline_toggle(struct app *app, int direction);
void outline_release_run(struct app *app);
void outline_release_task(struct app *app);
bool text_matches(const char *text, const char *search);
/* Run and headless-head presentation (run_text.c). Outputs are caller buffers. */
struct outline_text { char name[TEXT + 64], count[32]; const char *status, *agent, *reported, *where; struct kt_style tone; };
void outline_row_text(const struct app *app, const struct outline_row *row, struct outline_text *t);
const char *head_agent_short(const struct app *app, const struct head *head);
const char *run_head_absence(const struct app *app, const struct workflow_head *r);
void format_duration(long long seconds, char *out, size_t size);
void format_tokens(long long value, char *out, size_t size);
const char *run_label_kind(const struct workflow_run *run);
void run_summary_text(const struct app *app, size_t run, char *out, size_t size);
void run_next_text(const struct app *app, size_t run, char *out, size_t size);
size_t overview_run(struct app *app);
void head_agent_text(const struct app *app, const struct head *h, char *out, size_t size);
const char *head_list_status(const struct app *app, const struct head *h);
struct kt_style head_list_tone(const struct app *app, const struct head *h);
const char *head_run_role(const struct app *app, const struct head *h);
void exec_model_text(const struct workflow_exec *e, bool running, char *out, size_t size);
void exec_effort_text(const struct workflow_exec *e, bool running, char *out, size_t size);
void exec_tokens_text(const struct workflow_exec *e, char *out, size_t size);
bool exec_configuration_text(const struct workflow_exec *e, char *out, size_t size);
void step_duration_text(const struct workflow_node *n, char *out, size_t size);
void plan_approval_text(const struct app *app, size_t run, char *out, size_t size);
/* Shared text layout (layout_text.c): labelled rows that wrap, names that
 * end in an ellipsis rather than a cut word, paths that keep head and tail,
 * and the lead block a detail screen opens with. */
#define DETAIL_LABEL 12
void fit_text(const struct app *app, const char *text, size_t width, char *out, size_t size);
void fit_path(const struct app *app, const char *path, size_t width, char *out, size_t size);
void labelled_at(struct app *app, int label_width, const char *label, const char *text, struct kt_style tone);
void labelled(struct app *app, const char *label, const char *text, struct kt_style tone);
void labelled_skip(struct app *app, int label_width, const char *label, const char *text, struct kt_style tone, size_t *skip);
void paragraph_skip(struct app *app, const char *text, struct kt_style tone, size_t *skip);
void labelled_pair(struct app *app, const char *left_label, const char *left, struct kt_style left_tone,
                   const char *right_label, const char *right, struct kt_style right_tone);
void lead_block(struct app *app, const char *eyebrow, const char *title);
void key_row(struct app *app, const char *key, const char *action);
const char *fit_hints(const char *hints, size_t width, char *out, size_t size);
size_t wrapped_rows(const char *text, size_t width);
void render_help(struct app *app);
bool help_scroll_key(struct app *app, char key);
/* Shared text-view helpers (render.c) and the head details view (detail.c). */
void column(struct app *app, int x, int width, struct kt_style tone, const char *text);
void paragraph(struct app *app, const char *text, struct kt_style tone);
void section(struct app *app, const char *label);
/* Read-only captured output in the remaining content rows; see transcript.h. */
void transcript_view(struct app *app, const char *text, size_t length, struct transcript_layout *layout);
/* One logical line of a scrolled document: rows before scroll are counted,
 * visible rows drawn with tone; returns the line's wrapped rows. */
size_t transcript_line_view(struct app *app, const char *text, size_t length, struct kt_style tone, size_t row, size_t scroll);
void transcript_view_reset(struct app *app);
void pair(struct app *app, const char *left_label, const char *left, struct kt_style left_tone,
          const char *right_label, const char *right, struct kt_style right_tone);

void render_head_detail(struct app *app);
/* Internal views borrow app; destroy functions release their owned view state. */
bool native_terminal_focused(struct app *app);
/* The agent-pane layer (terminal_input.c): whether a key or mouse event is
 * its to handle, the handling, and a paste (true when the layer took it). */
bool native_terminal_wants(struct app *app, const struct kt_event *e);
void native_terminal_event(struct app *app, const struct kt_event *e);
bool native_terminal_paste(struct app *app, const char *bytes, size_t length);
void statistics_toggle(struct app *app);
/* Stats actions from the binding table: whether one applies now, running
 * it, and the graph key's label for the page shown. */
bool statistics_applies(const struct app *app, enum key_action action);
void statistics_perform(struct app *app, enum key_action action);
const char *statistics_graph_label(const struct app *app);
const char *statistics_page_label(const struct app *app);
/* While the Stats filter is typed, text keys and paste edit it. */
bool statistics_typing(const struct app *app);
bool statistics_filter_event(struct app *app, const struct kt_event *e);
bool statistics_paste(struct app *app, const char *bytes, size_t length);
bool statistics_back(struct app *app);
bool native_plan_key(struct app *app, char key);
bool native_control_key(struct app *app, char key);
int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size);
/* Dialogs (forms.c): each waits for its answer. A question's default button
 * is Cancel; a form returns 0 when submitted and -1 when cancelled. */
bool dialog_confirm(struct app *app, const char *title, const char *body, const char *accept, bool destructive);
int dialog_form(struct app *app, const char *title, struct kt_field *fields, size_t count);
int dialog_form_body(struct app *app, const char *title, const char *body, struct kt_field *fields, size_t count);
void dialogs_draw(struct app *app);
/* Runs a kettle modal until final() accepts its result: that result, or -1
 * when it was closed, cancelled or the program is ending. */
typedef bool (*dialog_final_fn)(int result);
int dialog_run(struct app *app, const struct kt_modal *dialog, dialog_final_fn final);
bool confirm_choice(struct app *app, const char *title, const char *const lines[], size_t count, const char *choices);
bool render_statistics(struct app *app, unsigned frame, bool headless);
/* The head sessions page and what the two pages share (statistics_sessions.c). */
bool statistics_sessions_shown(const struct app *app);
void statistics_sessions_default(struct statistics_view *v);
void statistics_sessions_visible(struct app *app);
void statistics_sessions_move(struct app *app, int direction);
void statistics_select(struct app *app, size_t item);
void statistics_metrics_render(struct app *app, struct kt_canvas *c, struct kt_rect r);
int native_workspace_agent_index(struct native_workspace *w, int pane);
struct native_terminal *native_workspace_terminal(struct app *app, int pane);
void native_workspace_sync_terminal(struct app *app);
void go_back(struct app *app);
void enter_view(struct app *app, int view);
size_t tab_order(const struct app *app, int *views);
void select_tab(struct app *app, int direction);
/* Opens a screen as a jump: Esc there comes back to the one it left. */
void screen_jump(struct app *app, int view);
/* Opens screen index (0 Work, 1 Inbox, 2 Hosts, 3 Stats). */
void enter_screen(struct app *app, int index);
/* The letters of earlier releases that opened a screen (W, I, H, D): a jump
 * to the screen that took over, with a toast naming its key now. False for
 * any other action. */
bool screen_alias(struct app *app, enum key_action action);
int run_captured(struct app *app, char *const argv[], char *out, size_t size, long budget_ms);
void action_capture_reap(struct app *app);
void remove_heads_action(struct app *app);
void new_task_action(struct app *app);
void new_task_attach(struct app *app);
void remove_key_action(struct app *app);
void native_workspace_show_terminal(struct app *app, bool focus);
void native_workspace_split_agents(struct app *app);
void native_workspace_evidence_text(struct app *app, struct kt_canvas *c, size_t *scroll);
void native_workspace_graph(struct app *app, struct kt_canvas *c, bool planning);
/* Remote setup (U10): setup_flow.c, setup_hosts.c, setup_render.c and
 * setup_capture.c. The flow shows in the Hosts detail pane, its approvals
 * and its form as modals; every mutation is a `hydra remote ...` child. */
struct native_setup *native_setup_state(struct app *app);
bool native_setup_active(const struct app *app);
void native_setup_open_form(struct app *app);
void native_setup_resume(struct app *app, const char *name);
/* Hosts tab e / x on an unfinished setup record (index into records). */
void native_setup_edit(struct app *app, size_t record);
void native_setup_remove(struct app *app, size_t record);
void native_setup_event(struct app *app, const struct kt_event *e);
void native_setup_paste(struct app *app, const char *bytes, size_t length);
void native_setup_tick(struct app *app);
void native_setup_sweep_tick(struct app *app);
void native_setup_hosts_refresh(struct app *app);
void native_setup_render(struct app *app);
void native_setup_modal(struct app *app);
/* The binding table's setup actions, as hosts_perform runs them. */
bool native_setup_applies(const struct app *app, enum key_action action);
void native_setup_perform(struct app *app, enum key_action action);
const char *native_setup_label(const struct app *app, enum key_action action);
bool native_setup_modal_open(const struct app *app);
/* The flow closes; a step that runs keeps running. */
void native_setup_close(struct app *app);
void native_setup_destroy(struct app *app);
int setup_terminal_handoff(struct app *app, char *const argv[], const char *step);
/* Onboarding operator view (H5): onboarding_flow.c, onboarding_render.c,
 * onboarding_model.c, onboarding_text.c and onboarding_form.c. A layer over
 * the Hosts tab like setup; every action is a `hydra fleet ...` child. */
bool native_onboarding_active(const struct app *app);
void native_onboarding_open(struct app *app);
bool native_onboarding_event(struct app *app, const struct kt_event *e);
void native_onboarding_tick(struct app *app);
void native_onboarding_render(struct app *app);
const char *native_onboarding_hints(const struct app *app, bool narrow);
/* The view closes (Esc on its list, [ or ]); a child that runs keeps running. */
void native_onboarding_close(struct app *app);
void native_onboarding_destroy(struct app *app);
int native_onboarding_fixture(struct app *app, const char *export_file, const char *intent_file, const char *screen);
void native_onboarding_dump(struct app *app, FILE *out);
#include "hosts.h"
struct native_terminal *native_terminal_selected(struct app *app);
void native_terminal_attention(struct app *app, const struct native_terminal *t, char *out, size_t size);
struct native_terminal *native_terminal_for_head(struct app *app, const struct head *h);
/* The client attached to the head, connected or not; NULL when none is. */
struct native_terminal *native_terminal_any_for_head(struct app *app, const struct head *h);
bool native_terminal_attach_head(struct app *app, const struct head *h);
bool native_terminal_deliver(struct app *app, struct native_terminal *t, const char *text, bool submit);
const char *native_agent_name(const struct head *h);
bool head_terminal_gone(const struct app *app, const struct head *h);
void native_terminal_close(struct native_terminal *t);
/* Enters (or leaves) the agent's own tmux history for a local attachment
 * without waiting for tmux; where that history is unavailable the client's
 * model scrolls. */
void native_terminal_history(struct app *app, struct native_terminal *t, bool enter);
void native_terminals_destroy(struct app *app);
bool native_terminal_attach(struct app *app);
void native_terminal_send(struct app *app, struct native_terminal *t, const void *bytes, size_t length);
void native_terminals_pump(struct app *app);
void native_terminal_draw(struct app *app, struct native_terminal *t, struct kt_canvas *c, bool focused);
void native_observations_cancel(struct app *app, size_t source);
void native_observations_destroy(struct app *app);
void native_observations_tick(struct app *app, bool request);
/* Asks for a fresh read of one source without waiting for it; loading says
 * whether one is in flight. */
void native_observations_request(struct app *app, size_t source);
bool native_observations_loading(const struct app *app, size_t source);
/* Inferred head activity (activity.c), from the tmux answer the head snapshot
 * already asks for: one hydra child per change, none per refresh. */
void native_activity_tick(struct app *app);
void native_activity_destroy(struct app *app);
void native_attention_destroy(struct app *app);
void native_attention_tick(struct app *app, bool request);
void native_attention_read(struct app *app);
/* Whether a refresh reads attention: always while Inbox is shown, otherwise
 * every 15 s for its badge, and not when the store says nothing changed. */
bool native_observations_attention_due(struct app *app, time_t last);
bool native_attention_run_request(const struct app *app, const char *run, char *request, size_t size,
                                  char *step, size_t step_size);
bool native_links_accept(struct app *app, FILE *input);
bool native_attention_load(struct app *app, const char *path);
bool native_attention_detail_open(const struct app *app);
bool native_attention_listed(const struct app *app);
bool native_attention_review_load(struct app *app, const char *path);
/* Headless fixtures replay keys through the interactive dispatcher. */
void headless_key(struct app *app, char key);
void native_controls_tick(struct app *app);
bool native_control_submit(struct app *app, const char *run, const char *action, const char *request);
/* Remote task decisions outside Work (Inbox): the refusal reason for key
 * (Y, N, R or X) on a task, or NULL; and the task as observed now when it is
 * still exactly the one confirmed, else NULL. */
const char *native_fleet_task_refusal(const struct app *app, const struct task_observation *task, char key);
const struct task_observation *native_fleet_task_current(const struct app *app, const struct task_observation *target);
bool native_fleet_control_submit(struct app *app, const struct task_observation *task,
                                 const char *action, const char *request, const char *trust);
bool native_fleet_control_key(struct app *app, char key, const char *action);
void native_evidence_destroy(struct app *app);
void native_evidence_tick(struct app *app, bool watch);
bool native_plan_launch(struct app *app, const char *digest);
/* The launched revision ended without a run receipt, so it may be launched again. */
bool native_plan_launch_failed(const struct native_plan *p);
void native_plan_launch_tick(struct app *app, bool watch);
struct head *native_plan_head(struct app *app, const char *branch);
bool native_plan_converse(struct app *app, const struct head *h, const char *text, bool submit, bool focus);
void native_plan_notify_started(struct app *app);
void native_plan_notices_tick(struct app *app);
void native_plan_tick(struct app *app, bool watch);
bool native_plan_changed(struct native_plan *p);
void native_plan_message(struct native_plan *p, const char *text);
void native_plan_destroy(struct app *app);
bool native_plan_load(struct app *app, const char *path, const char *policy);
bool native_plan_compile(struct app *app);
void native_plan_returned(struct native_plan *p);
void native_plan_execute(struct app *app);
void native_plan_request_changes(struct app *app);
void native_plan_import(struct app *app);
void native_plan_proposal(struct app *app);
bool native_plan_proposal_load(struct app *app, const char *branch);
void native_plan_validate(struct app *app);
/* Planning actions (plan_actions.c): what screens and keys call. */
void plan_action_load_proposal(struct app *app);
void plan_action_import(struct app *app);
void plan_action_validate(struct app *app);
void plan_action_request_changes(struct app *app);
void plan_action_approve(struct app *app);
void plan_actions_tick(struct app *app);
void plan_action_review(struct app *app, const char *run);
void plan_action_status(struct app *app, const char *run);
void plan_action_land(struct app *app, const char *worker);
/* A plan the head's agent published under planning (its draft's stat), not
 * sent back; Inbox lists it as "Plan ready". */
bool plan_action_published(const char *planning, struct stat *draft);
/* Inbox on a published plan: open it under the guided policy and approve
 * exactly that revision (Y), or send it back with feedback (F). */
void plan_action_approve_published(struct app *app, const char *branch);
void plan_action_return_published(struct app *app, const char *branch);
/* Inbox F on a failure: feedback typed into the planning agent's conversation. */
void plan_action_return_failure(struct app *app, const char *branch, const char *run, const char *what);
bool statistics_init(struct app *app);
void statistics_destroy(struct app *app);
void statistics_visible(struct app *app);
int accept_statistics(struct app *app, FILE *input);
int load_statistics_fixture(struct app *app, const char *fixture);
void statistics_request(struct app *app);
void statistics_graph_arrived(struct app *app, bool ok);
void statistics_move(struct app *app, int direction);
void statistics_count(uint64_t value, bool known, char *out, size_t size);
void statistics_tokens(const struct hs_usage *u, char *out, size_t size);
void statistics_scope_tokens(const struct hs_summary *s, char *out, size_t size);
int statistics_wrap(struct kt_canvas *c, int x, int y, int width, int rows, struct kt_style tone, const char *text);
void statistics_step_agent(const struct hs_step *s, bool compact, char *out, size_t size);
void statistics_run_tokens(const struct hs_model *m, size_t run, char *out, size_t size);
void statistics_step_detail(struct kt_canvas *c, struct kt_rect r, const struct hs_model *m, const struct hs_step *s);
bool workflow_id(const char *s);
size_t workflow_nodes(const struct workflow_model *m, size_t run, size_t *indices);
bool workflow_edges(const struct workflow_model *m, const size_t *indices, size_t n,
                            struct kt_edge *edges, size_t *count);
int accept_workflows(struct app *app, FILE *input);
int load_workflow_fixture(struct app *app, const char *fixture);
void workflow_move(struct app *app, int direction);
bool workflow_key(struct app *app, char key);
void native_capture_destroy(struct native_capture *p);
bool native_capture_start(struct native_capture *p, char *const argv[], long budget_ms);
bool native_capture_step(struct native_capture *p);
FILE *native_capture_result(struct native_capture *p, bool *success);
FILE *native_capture_take(struct native_capture *p);
bool native_detached_start(pid_t *pid, char *const argv[], int input_fd);
/* The interactive loop (loop.c) and what handlers use of it. */
extern const struct kt_app tui_app;
void tui_options(const struct app *app, struct kt_options *options);
int interactive_main(struct app *app);
void loop_service(struct app *app, bool request);
void input_event(struct app *app, const struct kt_event *e);
void input_paste(struct app *app, const char *bytes, size_t length, bool truncated);
/* A key for the views' own handlers, as bindings.c performs an action: a
 * character, or a named key (an arrow, a page key, Tab) with modifiers. */
void input_character(struct app *app, char key);
void input_named(struct app *app, uint32_t key, unsigned mods);
/* A frame that shows an age or elapsed time says so, to be redrawn as it advances. */
void clock_mark(void);
enum modal_event { MODAL_STOPPED, MODAL_INPUT, MODAL_WOKE };
bool modal_push(struct app *app, const struct modal *modal);
void modal_pop(struct app *app);
enum modal_event modal_wait(struct app *app, struct kt_msg *msg);
struct terminal_loan { struct sigaction stop, interrupt, quit; bool lent, shield, stop_changed; };
bool terminal_lend(struct app *app, struct terminal_loan *loan, bool shield);
bool terminal_return(struct app *app, struct terminal_loan *loan);
void copy_text(char *dst, size_t size, const char *src);
bool parse_unsigned(const char *value, unsigned *result);
int output_start(struct output_child *child, char *const argv[], long budget_ms);
ssize_t output_read(struct output_child *child, char *buffer, size_t size);
int output_finish(struct output_child *child, bool failed);
void group_marked_action(struct app *app);
void kill_marked_action(struct app *app);
void palette_open(struct app *app);
void fleet_action(struct app *app);
void remote_detail(struct app *app, const struct head *head);
void remote_technical(struct app *app, const struct head *head);
void task_detail(struct app *app, const struct task_observation *task);
bool parse_theme(const char *name, int *theme);
void style(struct app *app, enum tone tone);
/* The theme behind --theme and the t key (0 terminal, 1 dark, 2 light), and
 * the style a tone draws in. */
const struct kt_theme *tui_theme(int theme);
struct kt_style tone_style(int tone);
enum tone style_tone(struct kt_style style);
const char *theme_name(int theme);
void linef(struct app *app, const char *format, ...);
const char *display_status(const struct head *head);
const char *status_label(const struct head *head);
struct kt_style status_tone(const struct head *head);
size_t attention_count(const struct app *app);
const char *dot(const struct app *app);
/* Footer freshness of data observed at observed_at. Returns the tone. */
struct kt_style snapshot_freshness(const struct app *app, time_t observed_at, bool stale, bool headless, char *out, size_t size);
void text_append(char *buffer, size_t size, const char *format, ...);
bool frame_alloc(struct app *app);
void frame_free(struct app *app);
bool frame_begin(struct app *app, bool headless);
void frame_end(struct app *app, unsigned frame, bool headless);
bool frame_content(struct app *app, struct kt_canvas *out);
/* The top bar: the brand, the four screens as numbered tabs and the project summary. blank_row: the row under it is left blank, so the summary may not move there. */
void chrome_header(struct app *app, struct kt_canvas *c, bool blank_row);
/* A pane's titled rule (render.c), accent when the pane has the focus. */
void pane_rule(struct kt_canvas *c, struct kt_rect r, const char *title);
void pane_rule_styled(struct kt_canvas *c, struct kt_rect r, const char *title, bool focused);
void chrome_footer(struct app *app, struct kt_canvas *c, const char *status, struct kt_style tone, const char *hints);
/* The status line alone (its chip and text), and what it says on a screen without its own. */
void chrome_status(struct app *app, struct kt_canvas *c, const char *status, struct kt_style tone);
void screen_status(struct app *app, char *out, size_t size, struct kt_style *tone, bool headless);
void canvas_text(struct kt_canvas *c, int x, int y, int width, struct kt_style tone, const char *format, ...);
void recovery_explain(const struct recovery *item, char *title, size_t title_size, char *detail, size_t detail_size);
void show_result(struct app *app, const char *title, const char *text);
/* Outcomes of in-app actions (actions.c): a toast for one that finished, the
 * session list of those that did not, and that list in the output viewer. */
void toast(struct app *app, enum kt_toast_kind kind, const char *text);
void action_failed(struct app *app, const char *title, const char *output);
void actions_failed_show(struct app *app);
/* Scrolls the help text by rows (negative: up). */
void help_scroll(struct app *app, int rows);
void render(struct app *app, unsigned frame, bool headless);
bool head_matches(const struct head *head, const char *search);
struct head *selected_head(struct app *app);
void retarget_selection(struct app *app);
void move_selection(struct app *app, int direction);
size_t marked_index(const struct app *app, const char *branch);
void toggle_mark(struct app *app);
void select_all_visible(struct app *app);
const struct head *head_for_branch(const struct app *app, const char *branch);
void record_snapshot(struct app *app, bool valid);
int accept_model_data(struct app *app, FILE *input);
void refresh_current_session(struct app *app);
void capture_preview(struct app *app);
void preview_tick(struct app *app);
bool preview_loading(const struct app *app);
bool preview_unread(struct app *app);
bool head_stale(const struct app *app, const struct head *h);
size_t split_fields(char *line, char **fields, size_t capacity);
int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size);
int load_fixture(const char *path, struct model *model, char *error, size_t error_size);
#endif
