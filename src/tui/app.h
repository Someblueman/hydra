#ifndef HYDRA_TUI_APP_H
#define HYDRA_TUI_APP_H
#include "model.h"
#include "keys.h"
#include "../hydra_tui_workflow.h"
#include "../kettle/kettle.h"
#include "../kettle/components.h"
#include <termios.h>
#include <sys/types.h>
#include <time.h>
struct kt_program;
struct kt_cmds;
struct app;
/* A helper that waits inside a key handler (a prompt, a confirmation, a
 * captured action) while the loop keeps running around it (loop.c). tick runs
 * with the other background pollers and says what the helper waits for; draw
 * paints it over the frame. Either may be NULL. */
struct modal {
    void (*tick)(struct app *app, void *context);
    void (*draw)(struct app *app, void *context);
    void *context;
};
#define MODAL_DEPTH 6
#define FAILED_ACTIONS 8
/* The tag of the toasts' timers. */
#define TOAST_TAG 0x746f617374ULL
struct native_workspace;
struct statistics_view;
struct native_terminals;
struct native_observations;
struct native_plan;
struct native_evidence;
struct native_links;
struct native_attention;
struct native_review;
struct native_setup;
struct native_snapshot;
struct native_activity;
struct transcript;

struct app {
    struct model model;
    struct native_workspace *workspace;
    struct statistics_view *statistics;
    struct native_terminals *terminals;
    struct native_observations *observations;
    struct native_plan *plan;
    struct native_evidence *evidence;
    struct native_links *links;
    struct native_attention *attention;
    struct native_review *review;
    /* In-app remote setup (U10); allocated when Hosts is first used. */
    struct native_setup *setup;
    /* Configurable keys; read through native_keys(), which fills the defaults. */
    struct native_keys keys;
    /* The local head snapshot read from the state store in this process
     * (snapshot.h), when the launching CLI said its store may be read. */
    struct native_snapshot *snapshot;
    bool store;
    /* Lazily allocated interpreter for read-only captured output. */
    struct transcript *transcript;
    /* Inferred activity of interactive heads (activity.c); allocated when
     * the control centre first observes one. */
    struct native_activity *activity;
    pid_t control_pids[4];
    pid_t action_pid;
    /* Full 127-byte task/run identity plus action and label text. */
    char control_labels[4][160];
    const char *hydra;
    size_t selected, recovery_selected;
    int view, theme;
    int hit_rows[MAX_HEADS], hit_cols, hit_height, hit_view;
    int hit_left[MAX_HEADS], hit_right[MAX_HEADS], hit_bottom[MAX_HEADS];
    size_t hit_items[MAX_HEADS], hit_count;
    bool hit_tabs;
    int rows, cols, line, limit;
    bool no_color, preview, help, running, fleet, diagnostics;
    bool ascii, snapshot_stale;
    /* The interactive loop; NULL for deterministic rendering and in tests
     * that call the handlers directly. cmds is valid while the loop runs. */
    struct kt_program *program;
    struct kt_cmds *cmds;
    struct modal modals[MODAL_DEPTH];
    size_t modal_count;
    /* Dialogs on kettle's modal stack (forms.c), and the toasts that report
     * an action's outcome without taking input. */
    struct kt_modals dialogs;
    struct kt_toasts toasts;
    bool dialogs_ready, toasts_ready;
    /* In-app actions that failed this session (actions.c), oldest first. */
    struct failed_action { char title[TEXT]; char output[4096]; time_t at; } failed[FAILED_ACTIONS];
    size_t failed_count;
    unsigned long long modal_frame_at, clock_timer;
    unsigned service_serial;
    /* Captured actions in progress (action_capture.c). */
    unsigned action_depth;
    /* started: the loop took the terminal. sequence_tail: the rest of a
     * stalled escape sequence is being dropped. clock_drawn: the frame shows
     * an age or elapsed time, so it is redrawn every second. */
    bool started, modal_dirty, sequence_tail, clock_drawn, clock_armed;
    double queue_history[120];
    bool history_valid[120];
    size_t history_count;
    time_t snapshot_at;
    struct workflow_model *workflows;
    size_t workflow_run, workflow_node;
    size_t host_selected;
    size_t task_selected;
    int graph_x, graph_y;
    bool workflow_stale, graph_follow;
    time_t workflow_at;
    char workflow_error[TEXT];
    char marked[MAX_HEADS][TEXT];
    size_t marked_count;
    char current_session[TEXT];
    char search[TEXT];
    char pending_task[TEXT];
    char notice[TEXT];
    char snapshot_error[TEXT];
    char preview_text[32768];
    /* Rows the Details transcript is scrolled back from its newest output. */
    size_t preview_back;
    /* The head whose output preview_text holds, or is being read for. */
    char preview_head[TEXT];
    /* Work outline: runs the user expanded, and whether a run row rather
     * than a head is selected in Work or Overview (workflow_run names it). */
    char expanded_runs[WF_RUNS][80];
    size_t expanded_count;
    bool run_row;
    /* The terminal's settings as found; attach clients start with them. */
    struct termios saved;
    /* One frame canvas serves every view. The loop lends it for each frame
     * (target); deterministic rendering draws into frame_cells instead. */
    struct kt_canvas frame;
    struct kt_canvas *target;
    struct kt_cell *frame_cells;
    size_t frame_capacity;
    int frame_theme, tone, previous_view;
    int content_x, content_width;
    int tab_left[12], tab_right[12], tab_view[12];
    size_t tab_count;
    /* Captured output of the last in-app action; shown on demand or on failure. */
    char result_title[TEXT];
    char result_text[8192];
    size_t result_scroll;
    /* Rows of the help text scrolled off its top (render.c). */
    int help_scroll;
    bool help_more;
    bool result_open;
};
#endif
