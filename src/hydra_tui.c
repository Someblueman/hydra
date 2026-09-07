#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

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
#include "termviz/termviz.h"
#include "termviz/workspace.h"
#include "termviz/tree.h"
#include "termviz/terminal.h"
#include "termviz/pty_posix.h"
#include "termviz/input.h"
#include "hydra_tui_workflow.h"
#include "hydra_statistics.h"

#define HYDRA_TUI_VERSION "2.1.0"
#define HYDRA_TUI_PROTOCOL 2
#define MAX_HEADS 512
#define MAX_RECOVERY 512
#define TEXT 256
#define SOURCE_TEXT 768
#define MAX_DATA_BYTES (8U * 1024U * 1024U)

extern char **environ;

struct head {
    char remote_host[128], remote_project[SOURCE_TEXT], remote_branch[TEXT];
    char branch[TEXT], session[TEXT], profile[TEXT], group[TEXT], pr[TEXT];
    char status[32], liveness[32], declared[64], observed[64], confidence[64];
    char instance[TEXT], desired[64], source[SOURCE_TEXT], head_id[TEXT];
    char adapter[64], adapter_confidence[64], adapter_source[SOURCE_TEXT];
    char notification_source[SOURCE_TEXT];
    unsigned notifications;
    unsigned events, signals, messages, claims, scopes, queue, resources, diff, gates, approved;
};

struct recovery {
    char kind[64], label[TEXT], source[SOURCE_TEXT], confidence[64], action[TEXT];
};
struct host_observation { char name[128], state[40], error[128]; unsigned heads; };

struct model {
    struct head heads[MAX_HEADS];
    struct recovery recovery[MAX_RECOVERY];
    size_t head_count, recovery_count;
    struct host_observation hosts[16];
    size_t host_count;
};

struct native_workspace;
struct statistics_view;
struct native_terminals;
struct native_observations;
struct native_plan;
struct native_evidence;
struct native_links;

struct app {
    struct model model;
    struct native_workspace *workspace;
    struct statistics_view *statistics;
    struct native_terminals *terminals;
    struct native_observations *observations;
    struct native_plan *plan;
    struct native_evidence *evidence;
    struct native_links *links;
    pid_t control_pids[4];
    char control_labels[4][128];
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
    int graph_x, graph_y;
    bool workflow_stale, graph_follow;
    time_t workflow_at;
    char workflow_error[TEXT];
    char marked[MAX_HEADS][TEXT];
    size_t marked_count;
    char current_session[TEXT];
    char search[TEXT];
    char notice[TEXT];
    char snapshot_error[TEXT];
    char preview_text[4096];
    struct termios saved;
};

static struct app *active_app;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t stop_signal;

static struct head *selected_head(struct app *app);
static void retarget_selection(struct app *app);
static void native_workspace_move(struct app *app, int direction);
static void native_workspace_invalidate(struct app *app);
static bool native_workspace_monitoring(struct app *app);
static void statistics_move(struct app *app, int direction);
static void native_observations_cancel(struct app *app, size_t source);

#include "hydra_tui_process.inc"
#include "hydra_tui_model.inc"
#include "hydra_tui_workflow_model.inc"
#include "hydra_tui_statistics_model.inc"
#include "hydra_tui_plan_model.inc"
#include "hydra_tui_plan_projection.inc"
#include "hydra_tui_plan_launch.inc"
#include "hydra_tui_evidence.inc"
#include "hydra_tui_controls.inc"
#include "hydra_tui_links.inc"
#include "hydra_tui_observations.inc"
#include "hydra_tui_terminals.inc"
#include "hydra_tui_ui.inc"
#include "hydra_tui_main.inc"
