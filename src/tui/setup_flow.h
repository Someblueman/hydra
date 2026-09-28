#ifndef HYDRA_TUI_SETUP_FLOW_H
#define HYDRA_TUI_SETUP_FLOW_H
#include "setup_model.h"
#include "../termviz/input.h"
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
/* In-app remote setup (U10). The control centre runs `hydra remote ...
 * --json` children, shows their envelopes and hands the terminal to steps
 * that need it; the CLI owns every mutation, plan hash and SSH decision. */

/* Why a child runs, which decides what its result leads to. */
enum setup_job {
    SETUP_JOB_NONE,
    SETUP_JOB_GUIDED,   /* hydra remote setup NAME [DEST] --json */
    SETUP_JOB_APPROVED, /* data.next.argv after the user approved its plan */
    SETUP_JOB_STATUS,   /* read-only status for "Continue setup" */
    SETUP_JOB_RETURNED, /* read-only status after a terminal hand-off */
    SETUP_JOB_INSPECT   /* read-only preflight or agent inventory */
};

#define SETUP_FORM_FIELDS 3
enum { SETUP_FIELD_DESTINATION, SETUP_FIELD_NAME, SETUP_FIELD_CONFIG };
struct setup_field { char text[1024]; size_t cursor; };

#define SETUP_RECORDS 16
struct setup_record { char name[128], summary[160]; bool complete, checked; };

struct setup_capture {
    pid_t pid;
    int fd, status;
    FILE *output;
    struct timespec started;
    long budget_ms;
    size_t bytes;
    bool active, eof, reaped, failed, timed_out;
};

/* Members are ordered by alignment; see the comments for their roles. */
struct native_setup {
    /* What the screen shows, and the buffer the next result is parsed into;
     * the sweep result belongs to the Hosts tab status refresh. */
    struct setup_envelope *current, *incoming, *sweep_result;
    time_t job_started;
    size_t handoff_argc, selected, scroll, typed_cursor, record_count, sweep_index;
    struct setup_capture job, sweep;
    struct tv_input input;
    struct setup_field fields[SETUP_FORM_FIELDS];
    enum setup_screen screen;
    enum setup_job job_kind;
    int focus, handoff_exit;
    unsigned automatic;
    bool open, more, sweep_pending;
    char typed[16];
    /* Running step, and the terminal hand-off step with its exact argv
     * (without --json). */
    char job_step[80], handoff_step[80];
    char name[128], sweep_name[128], hint[160], problem[200], destination[256], notice[256], config[1024];
    /* Hosts tab: setup records found in $HYDRA_HOME/fleet/setup. */
    struct setup_record records[SETUP_RECORDS];
    char handoff[SETUP_ARG_MAX][SETUP_ARG_TEXT];
};

/* Child capture (setup_capture.c): stdout only, stdin and stderr /dev/null,
 * private session. A running capture is never killed by the UI except on its
 * deadline, so quitting Hydra leaves a remote mutation to finish. */
bool setup_capture_start(struct setup_capture *c, char *const argv[], long budget_ms);
/* Advances without blocking; true once the child has exited and been reaped. */
bool setup_capture_step(struct setup_capture *c);
/* Reads the finished capture into a malloc'd, NUL-terminated buffer (caller
 * frees; NULL for no usable output) and returns the exit status (128+n for a
 * signal, 124 for the deadline). Releases the capture. */
char *setup_capture_finish(struct setup_capture *c, size_t *length, int *exit_status);
/* Closes descriptors without signalling a still-running child. */
void setup_capture_release(struct setup_capture *c);
#endif
