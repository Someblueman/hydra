#ifndef HYDRA_TUI_INTERNAL_H
#define HYDRA_TUI_INTERNAL_H
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
#include "../termviz/termviz.h"
#include "../termviz/workspace.h"
#include "../termviz/tree.h"
#include "../termviz/terminal.h"
#include "../termviz/pty_posix.h"
#include "../termviz/input.h"
#include "../hydra_tui_workflow.h"
#include "../hydra_statistics.h"

#include "app.h"
#include "process.h"
#include "terminal.h"
extern char **environ;
#define HYDRA_TUI_VERSION "2.2.1"
#define HYDRA_TUI_PROTOCOL 2
#define NATIVE_PLAN_LIMIT (256U*1024U)
#define NATIVE_PLAN_TEXT (1024U*1024U)
#define NATIVE_EVIDENCE_LIMIT (1024U*1024U)
#define NATIVE_TERMINALS 4
#define NATIVE_TERMINAL_CELLS (512U * 256U)
#define WORKSPACE_CAPACITY (512U * 256U)
struct native_capture {
    pid_t pid;
    int fd, status;
    FILE *output;
    struct timespec started;
    long budget_ms;
    size_t bytes;
    bool eof, reaped, failed, timed_out;
};
enum tone { TONE_BASE, TONE_BORDER, TONE_TITLE, TONE_SELECTED, TONE_WARNING, TONE_STRONG };
struct statistics_view {
    struct hs_model *model;
    struct hs_filter filter;
    size_t selected, visible[HS_RUNS], count;
    size_t step_scroll;
    int back_view;
    bool stale, detail, graph_open;
    char error[TEXT], selected_id[128];
};
enum native_plan_state { PLAN_DRAFT, PLAN_VALIDATING, PLAN_INVALID, PLAN_READY, PLAN_UNAVAILABLE };
struct native_plan {
    char path[4096], policy[4096], directory[4096], compiled[4096];
    char digest[65], objective[4096], notice[TEXT];
    unsigned revision, compilation;
    enum native_plan_state state;
    struct native_capture job;
    struct native_capture launch_job;
    pid_t owner_pid;
    char launched_digest[65], launched_run[65], launch_state[32];
    bool follow_launched_run;
    bool projecting;
    char *source_bytes, *policy_bytes, *text;
    size_t source_length, policy_length, text_length, text_scroll, selected;
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
struct native_observations { struct native_capture jobs[4]; };
struct native_terminal {
    struct tv_pty client;
    struct tv_terminal_model *screen;
    struct tv_cell *cells, *history;
    char head[TEXT], instance[TEXT], label[TEXT];
    size_t scroll;
    bool scrolling;
};
struct native_terminals {
    struct native_terminal slots[NATIVE_TERMINALS];
    size_t selected;
    struct tv_input input;
    bool prefix;
};
struct native_workspace {
    struct tv_workspace layout;
    struct tv_workspace saved[3];
    bool saved_zoom[3], initialized[3];
    int mode;
    struct tv_tree tree;
    struct tv_tree_node nodes[MAX_HEADS + 1 + 512 + WF_RUNS];
    char run_labels[WF_RUNS][200];
    char project_label[256];
    char collapsed[MAX_HEADS][TEXT];
    size_t collapsed_count;
    bool run_selected;
    struct { int first; size_t slots[2]; } agents[3];
    struct tv_cell *cells, *previous;
    struct tv_presenter presenter;
    int theme;
    bool root_open, zoom, compact;
};
/* Internal views borrow app; destroy functions release their owned view state. */
bool native_terminal_focused(struct app *app);
bool native_terminal_byte(struct app *app, unsigned char byte);
void native_terminal_flush_input(struct app *app);
void statistics_toggle(struct app *app);
bool statistics_key(struct app *app, char key);
bool statistics_back(struct app *app);
bool native_plan_key(struct app *app, char key);
bool native_control_key(struct app *app, char key);
int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size);
bool render_statistics(struct app *app, unsigned frame, bool headless);
int native_workspace_agent_index(struct native_workspace *w, int pane);
struct native_terminal *native_workspace_terminal(struct app *app, int pane);
void native_workspace_sync_terminal(struct app *app);
void native_workspace_focus_next(struct app *app);
void native_workspace_show_terminal(struct app *app, bool focus);
void native_workspace_split_agents(struct app *app);
bool native_workspace_monitoring(struct app *app);
void native_workspace_destroy(struct app *app);
void native_workspace_invalidate(struct app *app);
bool native_workspace_init(struct app *app);
void native_workspace_mode(struct app *app, int mode);
void native_workspace_move(struct app *app, int direction);
bool native_workspace_key(struct app *app, char key);
void native_workspace_mouse(struct app *app, unsigned button, int x, int y, bool release);
bool render_native_workspace(struct app *app, unsigned frame, bool headless);
void native_workspace_evidence_text(struct app *app, struct tv_canvas *c, size_t *scroll);
void native_workspace_plan_text(struct app *app, struct tv_canvas *c, size_t *scroll);
void native_workspace_graph(struct app *app, struct tv_canvas *c, bool planning);
void render_hosts(struct app *app);
void render_workflow_graph(struct app *app);
void dashboard_style(void *context, enum tv_style tone);
void dashboard_text(struct tv_canvas *c, int x, int y, int width,
                           enum tv_style tone, const char *format, ...);
void dashboard_card(struct tv_canvas *c, int x, int width, const char *title,
                           size_t count, const char *caption, enum tv_style tone);
void render_dashboard(struct app *app);
struct native_terminal *native_terminal_selected(struct app *app);
const char *native_terminal_attention(struct app *app, const struct native_terminal *t);
void native_terminal_close(struct native_terminal *t);
void native_terminals_destroy(struct app *app);
bool native_terminal_attach(struct app *app);
void native_terminal_send(struct app *app, struct native_terminal *t, const void *bytes, size_t length);
void native_terminals_pump(struct app *app);
void native_terminal_draw(struct app *app, struct native_terminal *t, struct tv_canvas *c, bool focused);
void native_observations_cancel(struct app *app, size_t source);
void native_observations_destroy(struct app *app);
void native_observations_tick(struct app *app, bool request);
void native_links_accept(struct app *app, FILE *input);
bool native_links_match(struct app *app, size_t run, size_t head);
void native_controls_tick(struct app *app);
bool native_control_submit(struct app *app, const char *run, const char *action, const char *request);
void native_evidence_destroy(struct app *app);
void native_evidence_tick(struct app *app, bool watch);
bool native_plan_launch(struct app *app, const char *digest);
void native_plan_launch_tick(struct app *app, bool watch);
void native_plan_tick(struct app *app, bool watch);
bool native_plan_changed(struct native_plan *p);
void native_plan_message(struct native_plan *p, const char *text);
void native_plan_destroy(struct app *app);
bool native_plan_load(struct app *app, const char *path, const char *policy);
bool native_plan_compile(struct app *app);
bool statistics_init(struct app *app);
void statistics_destroy(struct app *app);
void statistics_visible(struct app *app);
int accept_statistics(struct app *app, FILE *input);
int refresh_statistics(struct app *app, const char *fixture);
void statistics_move(struct app *app, int direction);
bool workflow_id(const char *s);
size_t workflow_nodes(const struct workflow_model *m, size_t run, size_t *indices);
bool workflow_edges(const struct workflow_model *m, const size_t *indices, size_t n,
                            struct tv_edge *edges, size_t *count);
int accept_workflows(struct app *app, FILE *input);
int refresh_workflows(struct app *app, const char *fixture);
void workflow_move(struct app *app, int direction);
bool workflow_key(struct app *app, char key);
void native_capture_destroy(struct native_capture *p);
bool native_capture_start(struct native_capture *p, char *const argv[], long budget_ms);
bool native_capture_step(struct native_capture *p);
FILE *native_capture_result(struct native_capture *p, bool *success);
FILE *native_capture_take(struct native_capture *p);
bool native_detached_start(pid_t *pid, char *const argv[], int input_fd);
int read_key(int timeout_ms, char *key);
int interactive_main(struct app *app);
void restore_terminal(struct app *app);
int enter_raw(struct app *app);
void update_size(struct app *app);
void terminal_pipe_signal(void);
void terminal_watch(struct app *app);
bool terminal_stopped(void);
int terminal_exit_status(void);
void terminal_request_stop(int number);
void copy_text(char *dst, size_t size, const char *src);
bool parse_unsigned(const char *value, unsigned *result);
int output_start(struct output_child *child, char *const argv[], long budget_ms);
ssize_t output_read(struct output_child *child, char *buffer, size_t size);
int output_finish(struct output_child *child, bool failed);
void group_marked_action(struct app *app);
void kill_marked_action(struct app *app);
void execute_palette(struct app *app, const char *query);
void fleet_action(struct app *app, bool attach);
bool parse_theme(const char *name, int *theme);
void style(const struct app *app, enum tone tone);
const char *theme_name(int theme);
void linef(struct app *app, const char *format, ...);
const char *display_status(const struct head *head);
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
FILE *capture_adapter(struct app *app, const char *command, const char *option, long budget_ms);
int accept_model_data(struct app *app, FILE *input);
int refresh_model(struct app *app);
void refresh_current_session(struct app *app);
void capture_preview(struct app *app);
size_t split_fields(char *line, char **fields, size_t capacity);
int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size);
int load_fixture(const char *path, struct model *model, char *error, size_t error_size);
#endif
