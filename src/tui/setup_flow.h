#ifndef HYDRA_TUI_SETUP_FLOW_H
#define HYDRA_TUI_SETUP_FLOW_H
#include "setup_model.h"
#include "../kettle/input.h"
#include "../kettle/components.h"
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
    SETUP_JOB_INSPECT,  /* read-only preflight or agent inventory */
    SETUP_JOB_CHOICE,   /* install-agent chosen on the agents screen (plan only) */
    SETUP_JOB_PREFLIGHT, /* the guided flow's preflight: pauses only when heads cannot run */
    SETUP_JOB_REMOVE_PLAN, /* hydra remote setup remove NAME --json: the removal plan only */
    SETUP_JOB_REMOVE     /* its data.next.argv after the user approved that plan */
};

struct app;
#define SETUP_FORM_FIELDS 3
enum { SETUP_FIELD_DESTINATION, SETUP_FIELD_NAME, SETUP_FIELD_CONFIG };
struct setup_field { char text[1024]; };

#define SETUP_RECORDS SETUP_LISTED_MAX
/* upgrade: a finished setup whose host runs another Hydra (Enter upgrades it). */
struct setup_record { char name[128], summary[160], destination[256], config[1024]; bool complete, remote_changed, upgrade; };

struct setup_capture {
    pid_t pid;
    int fd, status;
    FILE *output;
    struct timespec started, cancel_started;
    long budget_ms;
    size_t bytes;
    bool active, eof, reaped, failed, timed_out, cancelled;
};

/* The modal a screen shows over the steps: the add-host form, the host-key
 * form (a field that accepts only yes) and a plan's keyed confirmation. The
 * components stay between frames; their texts are written again for each. */
struct setup_modal {
    struct kt_form form;
    struct kt_field fields[SETUP_FORM_FIELDS];
    struct kt_textinput inputs[SETUP_FORM_FIELDS];
    struct kt_confirm question;
    enum setup_screen screen;     /* the screen the components were made for */
    bool removal;                 /* the plan they were made for removes a record */
    char title[200], body[8192];
};

/* Members are ordered by alignment; see the comments for their roles. */
struct native_setup {
    /* What the screen shows, and the buffer the next result is parsed into;
     * the sweep result belongs to the Hosts tab status refresh. held keeps a
     * guided result while its preflight is checked (held_valid). */
    struct setup_envelope *current, *incoming, *sweep_result, *held;
    time_t job_started;
    size_t handoff_argc, selected, scroll, record_count;
    struct setup_capture job, sweep;
    struct setup_field fields[SETUP_FORM_FIELDS];
    struct setup_modal modal;
    enum setup_screen screen;
    enum setup_job job_kind;
    int focus, handoff_exit;
    unsigned automatic;
    /* agents_offered: this flow already showed the agent inventory.
     * paused: the requirements screen stopped a guided run because heads
     * cannot run on the host; Enter continues anyway. replacing: an edited
     * setup's record is being removed; once it is, setup starts again with
     * the form's values. */
    bool open, more, sweep_pending, agents_offered, paused, held_valid, replacing;
    char typed[16];
    /* Running step, and the terminal hand-off step with its exact argv
     * (without --json). */
    char job_step[80], handoff_step[80];
    /* editing: the record the form edits (empty for a new host);
     * duplicate: the unfinished record whose name the form entered. */
    char name[128], editing[128], duplicate[128], hint[160], problem[200], destination[256], notice[256], config[1024];
    /* Hosts tab: setup records from `hydra remote setup list --json`. */
    struct setup_record records[SETUP_RECORDS];
    char handoff[SETUP_ARG_MAX][SETUP_ARG_TEXT];
};

/* The modal's components made for the current screen, and its title and
 * body written for this terminal (setup_modal.c). */
void setup_modal_sync(const struct app *app, struct native_setup *s);

/* Child capture (setup_capture.c): stdout only, stdin and stderr /dev/null,
 * private session. A running capture is stopped only on its deadline or when
 * the user cancels it; quitting Hydra leaves a remote mutation to finish. */
bool setup_capture_start(struct setup_capture *c, char *const argv[], long budget_ms);
/* Advances without blocking; true once the child has exited and been reaped. */
bool setup_capture_step(struct setup_capture *c);
/* Reads the finished capture into a malloc'd, NUL-terminated buffer (caller
 * frees; NULL for no usable output) and returns the exit status (128+n for a
 * signal, 124 for the deadline). Releases the capture. */
char *setup_capture_finish(struct setup_capture *c, size_t *length, int *exit_status);
/* Stops the child's whole session (SIGTERM, then SIGKILL after a grace
 * period); its result reports exit status 130. The CLI records progress
 * before every side effect, so a later status or rerun reconciles it. */
void setup_capture_cancel(struct setup_capture *c);
/* Closes descriptors without signalling a still-running child. */
void setup_capture_release(struct setup_capture *c);
#endif
