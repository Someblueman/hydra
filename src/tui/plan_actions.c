#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Every planning action the control centre performs, in one place. Screens
 * and keys call these; these call the CLI underneath: hydra workflow plan
 * proposal | compile and the launch owner through plan_input.c, plan_model.c,
 * plan_launch.c and plan_decide.c, and hydra plan review | status and
 * hydra land directly, so the CLI can change without touching a screen.
 *
 * Approval is one action: it approves exactly the revision on screen. A
 * revision that is not validated yet is compiled now, and runs as soon as
 * its validation comes back ready, with the digest that validation produced;
 * if the draft or policy changes in between, or validation fails, nothing
 * runs. A validated revision shows its policy and consequences and runs on
 * that confirmation. */

void plan_action_load_proposal(struct app *app) { native_plan_proposal(app); }
void plan_action_import(struct app *app) { native_plan_import(app); }
void plan_action_validate(struct app *app) { native_plan_validate(app); }
void plan_action_request_changes(struct app *app) { native_plan_request_changes(app); }

static bool approve_draft_confirmed(struct app *app, const struct native_plan *p) {
    char body[1024];
    snprintf(body, sizeof(body), "Approve and run %s revision %u?\n\nHydra validates exactly this revision under the "
             "project's plan policy and runs it as soon as validation is ready. If the draft or policy changes first, or "
             "validation finds a problem, nothing runs.", p->proposal_head[0] ? p->proposal_head : "the imported plan",
             p->revision);
    return dialog_confirm(app, "Approve plan", body, "Approve", false);
}

void plan_action_approve(struct app *app) {
    struct native_plan *p = app->plan;
    if (!p || app->fleet || p->state == PLAN_READY || p->state == PLAN_RETURNED || p->state == PLAN_VALIDATING ||
        (p->digest[0] && !strcmp(p->digest, p->launched_digest))) {
        native_plan_execute(app);
        return;
    }
    if (!approve_draft_confirmed(app, p)) {
        copy_text(app->notice, sizeof(app->notice), "Plan not approved; nothing was submitted.");
        return;
    }
    if (!app->plan || app->plan->revision != p->revision || !native_plan_compile(app)) {
        copy_text(app->notice, sizeof(app->notice), "Plan not approved: the revision changed or cannot be validated now.");
        return;
    }
    app->plan->approved_revision = app->plan->revision;
    snprintf(app->notice, sizeof(app->notice), "Approved revision %u; validating it, then it runs", app->plan->revision);
}

/* An approved revision runs once its validation is ready; any other outcome
 * withdraws the approval and says why. */
void plan_actions_tick(struct app *app) {
    struct native_plan *p = app->plan;
    unsigned revision;
    if (!p || !p->approved_revision || p->state == PLAN_VALIDATING) return;
    revision = p->approved_revision;
    p->approved_revision = 0;
    if (p->state == PLAN_READY && p->revision == revision && native_plan_launch(app, p->digest)) {
        snprintf(app->notice, sizeof(app->notice), "Revision %u validated and submitted", revision);
        return;
    }
    snprintf(app->notice, sizeof(app->notice), "Revision %u did not run: %s", revision,
             p->revision != revision ? "the draft changed after approval" :
             p->state == PLAN_INVALID ? "validation found problems (F sends them to the agent)" : "validation did not complete");
}

/* The result review of a plan run, as hydra plan review prints it. */
void plan_action_review(struct app *app, const char *run) {
    char output[65536], title[160];
    char *argv[] = {(char *)app->hydra, "plan", "review", (char *)run, NULL};
    snprintf(title, sizeof(title), "Result of %.120s", run);
    if (run_captured(app, argv, output, sizeof(output), 30000L)) title[0] = '\0';
    show_result(app, title[0] ? title : "Result review unavailable", output);
}

/* A plan run's steps and what to do next, as hydra plan status prints them. */
void plan_action_status(struct app *app, const char *run) {
    char output[16384];
    char *argv[] = {(char *)app->hydra, "plan", "status", (char *)run, NULL};
    bool failed = run_captured(app, argv, output, sizeof(output), 15000L) != 0;
    show_result(app, failed ? "Run status unavailable" : "Run status", output);
}

/* Landing is one confirmed action: hydra land merges exactly the commit the
 * run verified into the checkout's current branch, never pushes, and refuses
 * while the checkout has tracked changes or the worker has moved. Without a
 * terminal it asks nothing, so the confirmation is this dialog. */
void plan_action_land(struct app *app, const char *worker) {
    char output[8192], body[512];
    char *argv[] = {(char *)app->hydra, "land", (char *)worker, NULL};
    snprintf(body, sizeof(body), "Land %.120s?\n\nHydra merges the commit its checks verified into your current branch, "
             "then removes the worker head and keeps its branch. Nothing is pushed.", worker);
    if (!dialog_confirm(app, "Land result", body, "Land", false)) {
        copy_text(app->notice, sizeof(app->notice), "Not landed; nothing was changed.");
        return;
    }
    if (run_captured(app, argv, output, sizeof(output), 60000L)) {
        show_result(app, "Not landed", output);
        return;
    }
    show_result(app, "Landed", output);
}

/* ---- Inbox: plans an agent published ------------------------------------ */

/* The agent published planning/draft.json and the user has not sent that
 * draft back (planning/returned names the draft it returned). */
bool plan_action_published(const char *planning, struct stat *draft) {
    char path[4096];
    struct stat returned;
    if (snprintf(path, sizeof(path), "%s/draft.json", planning) >= (int)sizeof(path) || stat(path, draft) || !S_ISREG(draft->st_mode))
        return false;
    if (snprintf(path, sizeof(path), "%s/returned", planning) >= (int)sizeof(path)) return false;
    return stat(path, &returned) != 0;
}

/* The published plan becomes the plan on screen in Work, its head selected. */
static bool published_loaded(struct app *app, const char *branch) {
    const struct head *h = head_for_branch(app, branch);
    if (app->fleet || !h || !native_workspace_init(app)) return false;
    app->run_row = false;
    app->selected = (size_t)(h - app->model.heads);
    if (app->plan && !strcmp(app->plan->proposal_head, branch) && !native_plan_changed(app->plan)) return true;
    return native_plan_proposal_load(app, branch);
}

/* Y on "Plan ready": the plan is read under the guided policy and approved
 * exactly as plan_action_approve approves it (one confirmation; nothing runs
 * without it, and a revision that changes first does not run). */
void plan_action_approve_published(struct app *app, const char *branch) {
    if (!published_loaded(app, branch)) return;
    plan_action_approve(app);
}

/* F on "Plan ready": feedback for that exact revision. */
void plan_action_return_published(struct app *app, const char *branch) {
    if (!published_loaded(app, branch)) return;
    plan_action_request_changes(app);
}

/* F on a failure: a note to the agent that planned the run, typed into its
 * conversation (the user presses Enter there). The run is not changed. */
void plan_action_return_failure(struct app *app, const char *branch, const char *run, const char *what) {
    struct kt_textinput input;
    struct kt_field field;
    struct head *h;
    char feedback[2048] = "", body[512], text[3072];
    kt_textinput_init(&input, feedback, sizeof(feedback));
    memset(&field, 0, sizeof(field));
    field.kind = KT_FIELD_TEXT; field.label = "What should change"; field.input = &input;
    snprintf(body, sizeof(body), "%.120s of run %.80s goes back to %.100s with your note. It is typed into that "
             "conversation; you press Enter there.", what, run, branch);
    if (dialog_form_body(app, "Request changes", body, &field, 1) || !feedback[0]) return;
    snprintf(text, sizeof(text), "Hydra: %.200s of run %.100s needs changes: %s Revise the plan and republish it with "
             "hydra workflow plan propose <draft.json>.", what, run, feedback);
    h = native_plan_head(app, branch);
    if (h && native_plan_converse(app, h, text, false, true))
        snprintf(app->notice, sizeof(app->notice), "Typed into %.100s's conversation; review it and press Enter to send.", branch);
    else snprintf(app->notice, sizeof(app->notice), "%.100s's conversation could not be opened; nothing was sent.", branch);
}
