#ifndef HYDRA_TUI_ONBOARDING_FLOW_H
#define HYDRA_TUI_ONBOARDING_FLOW_H
#include "onboarding.h"
#include <time.h>
/* The onboarding operator view as a layer over the Hosts tab (like remote
 * setup): it owns input while open, runs one CLI child at a time and shows
 * that child's envelope. Review (discover, qualify, status, the lists) and
 * mutation (`enroll apply`, only after a typed confirmation of the intent
 * digest) are separate screens and separate commands. */
enum onb_screen { ONB_SCREEN_LIST, ONB_SCREEN_DETAIL, ONB_SCREEN_CHANGES, ONB_SCREEN_SELECT, ONB_SCREEN_REVIEW, ONB_SCREEN_INTENT, ONB_SCREEN_RUNNING, ONB_SCREEN_ERROR };
enum onb_job { ONB_JOB_NONE, ONB_JOB_HOME, ONB_JOB_DISCOVER, ONB_JOB_QUALIFY, ONB_JOB_REVIEW, ONB_JOB_STATUS, ONB_JOB_APPLY };
#define ONB_FORM_FIELDS 5
enum { ONB_SELECT_ALIASES, ONB_SELECT_SOURCE, ONB_SELECT_NAMES, ONB_SELECT_INVENTORY, ONB_SELECT_CONFIG };
enum { ONB_REVIEW_PROJECT, ONB_REVIEW_ALIAS, ONB_REVIEW_PACKAGE, ONB_REVIEW_SHA256, ONB_REVIEW_PREFIX };
struct onb_field { char text[ONB_SELECTION_TEXT]; size_t cursor; };

struct native_onboarding {
    struct onb_model *model;
    struct onb_argv argv;
    struct setup_capture job;
    struct tv_input input;
    struct onb_filter filter;
    struct onb_field select_fields[ONB_FORM_FIELDS], review_fields[ONB_FORM_FIELDS];
    time_t job_started;
    size_t selected, scroll, detail_index;
    enum onb_screen screen, back;
    enum onb_job job_kind;
    int focus, exit_status;
    /* detail_record: the detail screen shows a source record, not a candidate.
     * loaded: the records of the onboarding directory were read once.
 * form_qualify: the selection form was opened by Q and submits a qualify. */
    bool open, more, detail_record, directory_known, loaded, form_qualify;
    char directory[ONB_PATH], intent_path[ONB_PATH], candidate[80], job_label[96], notice[256], problem[200], text[256];
};

struct native_onboarding *native_onboarding_state(struct app *app);
bool native_onboarding_active(const struct app *app);
/* O on Hosts: opens the view over the records of $HYDRA_HOME/fleet/onboarding. */
void native_onboarding_open(struct app *app);
bool native_onboarding_byte(struct app *app, unsigned char byte);
void native_onboarding_flush_input(struct app *app);
void native_onboarding_tick(struct app *app);
void native_onboarding_render(struct app *app);
const char *native_onboarding_title(const struct app *app);
const char *native_onboarding_hints(const struct app *app, bool narrow);
void native_onboarding_destroy(struct app *app);
/* Headless: open the view on recorded envelopes (an export, optionally an
 * intent/status envelope) at a named screen; no child runs. */
int native_onboarding_fixture(struct app *app, const char *export_file, const char *intent_file, const char *screen);
/* Headless: one line per listed row (candidate_id, target, status, typed
 * state, source, platform) for comparison with the CLI's JSON. */
void native_onboarding_dump(struct app *app, FILE *out);
/* Form editing (onboarding_form.c). */
struct onb_field *onb_form_fields(struct native_onboarding *s);
bool onb_form_key(struct native_onboarding *s, const struct tv_event *e);
void onb_form_insert(struct native_onboarding *s, const struct tv_event *e);
void onb_form_set(struct onb_field *f, const char *text);
/* The job each screen leads to, for the hint line and the running label. */
void onb_job_label(enum onb_job kind, char *out, size_t size);
#endif
