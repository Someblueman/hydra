#ifndef HYDRA_TUI_APP_H
#define HYDRA_TUI_APP_H
#include "model.h"
#include "../termviz/termviz.h"
#include <termios.h>
#include <sys/types.h>
#include <time.h>
enum input_mode { INPUT_KEYS, INPUT_DISCARD_CSI, INPUT_DISCARD_PASTE };
struct native_workspace;
struct statistics_view;
struct native_terminals;
struct native_observations;
struct native_plan;
struct native_evidence;
struct native_links;
struct native_attention;
struct native_review;

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
    bool raw, no_color, preview, help, running, fleet, diagnostics, paint, boxed;
    bool ascii, snapshot_stale;
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
    enum input_mode input_mode;
    size_t paste_matched;
    char marked[MAX_HEADS][TEXT];
    size_t marked_count;
    char current_session[TEXT];
    char search[TEXT];
    char pending_task[TEXT];
    char notice[TEXT];
    char snapshot_error[TEXT];
    char preview_text[4096];
    struct termios saved;
    /* One frame canvas and presenter serve every view, so repaints only touch
     * changed cells. The frame owner allocates cells before rendering. */
    struct tv_canvas frame;
    struct tv_cell *frame_cells, *frame_previous;
    struct tv_presenter presenter;
    int frame_theme, tone, previous_view;
    int content_x, content_width;
    int tab_left[12], tab_right[12], tab_view[12];
    size_t tab_count;
    /* Captured output of the last in-app action; shown on demand or on failure. */
    char result_title[TEXT];
    char result_text[8192];
    size_t result_scroll;
    bool result_open;
};
#endif
