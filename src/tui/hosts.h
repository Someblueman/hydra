#ifndef HYDRA_TUI_HOSTS_H
#define HYDRA_TUI_HOSTS_H
/* Hosts (view 6): the machines Hydra can run agents on. A list (This
 * machine, the observed fleet hosts, the setups not finished yet and "+ Add
 * a host") beside a detail pane, which shows the selected machine or, while
 * a setup is open, its steps and the current step. The page strip in the
 * list's rule switches between Hosts and Onboarding (the operator view).
 * hosts.c draws and drives the screen, hosts_machine.c reads This machine,
 * setup_hosts.c lists the rows. Included by internal.h. */
#include "setup_flow.h"

/* This machine: its tmux and Hydra versions and `hydra agent list`, read
 * once each time Hosts opens, one child after another, never on a key. */
#define HOSTS_AGENTS 16
struct hosts_agent { char name[32]; bool available; };
struct hosts_machine {
    struct setup_capture job;
    int step;                       /* the read running: tmux, Hydra, agents */
    bool pending, read, agents_read;
    char tmux[64], hydra[64];
    struct hosts_agent agents[HOSTS_AGENTS];
    size_t agent_count;
};

struct native_hosts {
    struct hosts_machine machine;
    /* The first list row shown, and the text the list is filtered by. */
    size_t top;
    char filter[64];
    /* The location filter Work had when Enter followed a host there. */
    char location[128];
    /* focus_detail: keys go to the detail pane (the open setup's steps);
     * detail_open: below 70 columns the detail replaces the list; jumped:
     * Enter followed a host to Work, so Esc there comes back. */
    bool focus_detail, detail_open, jumped;
};

/* Rows of the list: This machine (not in the fleet entry), each observed
 * host, each setup not finished yet, then "+ Add a host". The filter keeps
 * the hosts and setups whose name contains it. */
enum host_row_kind { HOST_ROW_MACHINE, HOST_ROW_HOST, HOST_ROW_SETUP, HOST_ROW_ADD };
struct host_row { enum host_row_kind kind; size_t index; };
size_t hosts_rows(const struct app *app, struct host_row *rows, size_t capacity);
size_t hosts_row_count(const struct app *app);
/* The selected row's kind and index; false with nothing listed. */
bool hosts_selected_row(const struct app *app, struct host_row *row);
/* The selected Hosts row is an unfinished setup record; *record is its index. */
bool hosts_setup_selected(const struct app *app, size_t *record);
void setup_row_text(const struct app *app, const struct host_row *row, char *name, size_t name_size, char *state, size_t state_size);

struct native_hosts *hosts_state(struct app *app);
void hosts_destroy(struct app *app);
/* Hosts opened: the setups are listed and This machine read again. */
void hosts_opened(struct app *app);
/* This machine (hosts_machine.c): the reads Hosts asks for, advanced by the
 * loop's tick; hosts_machine_stop ends a read still running. */
void hosts_machine_request(struct app *app);
void hosts_machine_tick(struct app *app);
void hosts_machine_stop(struct app *app);
/* The machine's heads: running ones and those whose terminal is gone. */
void hosts_machine_heads(const struct app *app, size_t *heads, size_t *running, size_t *gone);
size_t hosts_machine_agents(const struct app *app);

/* The binding table's Hosts actions (hosts.c): whether one applies now,
 * and performing it; false for an action that is not one of Hosts'. */
bool hosts_applies(const struct app *app, enum key_action action, bool *applies);
bool hosts_perform(struct app *app, enum key_action action);
enum key_chain_id hosts_chain(const struct app *app);
const char *hosts_open_label(const struct app *app);
/* Esc: one step back inside Hosts; false when there is nothing to undo. */
bool hosts_back(struct app *app);
/* Enter on a row: This machine and a host's work open in Work (Esc comes
 * back), a setup continues, "+ Add a host" opens its form. */
void hosts_open_row(struct app *app);
/* Up and Down on the list; a setup open in the detail closes (its step
 * keeps running). */
void hosts_move(struct app *app, int direction);
/* Esc in Work after Enter followed a host there: back to Hosts, with the
 * location filter Work had before. */
bool hosts_jump_return(struct app *app);
/* On the Onboarding page: [ and ] back to Hosts, and the screen keys, while
 * its list is shown; false for a key the onboarding view takes. */
bool hosts_onboarding_key(struct app *app, const struct kt_event *e);
/* r: a fresh observation of the selected host, or of every host. */
void hosts_retry_row(struct app *app);
/* A setup opened or closed: the detail takes the focus, or gives it back. */
void hosts_focus_detail(struct app *app, bool focus);
/* A mouse report on a row recorded by the last frame. */
void hosts_click(struct app *app, size_t row, unsigned button);
void hosts_render(struct app *app);
/* The setup flow's modal (its form, the host-key trust, a plan), drawn
 * over the finished frame. */
void hosts_overlay(struct app *app);
/* Appends what the status line says on Hosts. */
void hosts_status(struct app *app, char *out, size_t size);
/* A setup modal has the keys: the add-host form, the host key, a plan. */
bool hosts_modal_open(const struct app *app);
#endif
