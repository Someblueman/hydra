#define _POSIX_C_SOURCE 200809L

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
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define HYDRA_TUI_VERSION "2.0.0"
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

struct model {
    struct head heads[MAX_HEADS];
    struct recovery recovery[MAX_RECOVERY];
    size_t head_count, recovery_count;
};

struct app {
    struct model model;
    const char *hydra;
    size_t selected, recovery_selected;
    int view;
    int hit_rows[MAX_HEADS], hit_cols, hit_height, hit_view;
    size_t hit_items[MAX_HEADS], hit_count;
    bool hit_tabs;
    int rows, cols, line, limit;
    bool raw, no_color, preview, help, running, fleet, diagnostics, paint, boxed;
    char marked[MAX_HEADS][TEXT];
    size_t marked_count;
    char current_session[TEXT];
    char search[TEXT];
    char notice[TEXT];
    char preview_text[4096];
    struct termios saved;
};

static struct app *active_app;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t stop_signal;

static struct head *selected_head(struct app *app);
static void retarget_selection(struct app *app);

#include "hydra_tui_model.inc"
#include "hydra_tui_ui.inc"
#include "hydra_tui_main.inc"
