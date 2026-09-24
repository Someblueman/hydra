#define _POSIX_C_SOURCE 200809L
#include "internal.h"

/* Launch is explicit. Opening the control centre only observes the repository. */
void new_task_attach(struct app *app) {
    if (!app->pending_task[0]) return;
    for (size_t i = 0; i < app->model.head_count; i++) {
        if (strcmp(app->model.heads[i].branch, app->pending_task)) continue;
        app->selected = i;
        app->search[0] = '\0';
        app->pending_task[0] = '\0';
        enter_view(app, 7);
        if (native_terminal_attach(app)) native_workspace_show_terminal(app, true);
        return;
    }
}

static bool task_profile(struct app *app, char profile[TEXT]) {
    char output[8192];
    char *argv[] = {(char *)app->hydra, "agent", "list", NULL};
    if (run_captured(app, argv, output, sizeof(output), 5000L)) {
        show_result(app, "Agent selection unavailable", output);
        return false;
    }
    show_result(app, "Choose an available agent; none opens a shell", output);
    int status = prompt_text(app, "Agent profile (blank=project default, none=shell): ", profile, TEXT);
    app->result_open = false;
    return status == 0;
}

void new_task_action(struct app *app) {
    char branch[TEXT] = "", profile[TEXT] = "", objective[4096] = "", output[8192], planning[8192];
    char *init[] = {(char *)app->hydra, "init", NULL, NULL, NULL};
    char *argv[10];
    size_t count = 0;
    if (app->fleet) {
        copy_text(app->notice, sizeof(app->notice), "Remote tasks start on their host; use hydra fleet task");
        return;
    }
    if (prompt_text(app, "Task name: ", branch, sizeof(branch)) || !branch[0]) return;
    if (!task_profile(app, profile)) return;
    if (prompt_text(app, "Objective (blank starts a conversation): ", objective, sizeof(objective))) return;
    if (profile[0]) { init[2] = "--profile"; init[3] = profile; }
    if (run_captured(app, init, output, sizeof(output), 15000L)) {
        show_result(app, "Could not register this project", output);
        return;
    }
    argv[count++] = (char *)app->hydra;
    argv[count++] = "spawn";
    argv[count++] = branch;
    if (profile[0]) { argv[count++] = "--profile"; argv[count++] = profile; }
    if (objective[0] || strcmp(profile, "none")) {
        snprintf(planning, sizeof(planning),
            "Discuss and plan this objective with the user: %s\n\n"
            "Hydra planning handoff: do not implement or execute the proposed workflow before the user approves it in Hydra. "
            "Use hydra workflow plan schema to obtain the executable draft format. Author the draft yourself and publish it "
            "with hydra workflow plan propose <draft.json> from this head. Keep the initial plan local, with at most one worker, "
            "four heads, 300 seconds, 1 MiB artifacts, sh/git tools and no retries or repairs. Use the existing source revision; "
            "do not commit implementation changes while planning. Discuss revisions and republish the draft when the user asks. "
            "The user reviews with B then P, validates with V, and explicitly approves the exact revision with E. "
            "A saved proposal is not approval or completed work.", objective[0] ? objective : "Ask the user what they want to achieve, then discuss a plan.");
        argv[count++] = "--prompt"; argv[count++] = planning;
    }
    argv[count] = NULL;
    if (run_captured(app, argv, output, sizeof(output), 60000L)) {
        show_result(app, "Task did not start; inspect the reported outcome", output);
        return;
    }
    copy_text(app->pending_task, sizeof(app->pending_task), branch);
    copy_text(app->notice, sizeof(app->notice), "Task started; opening its agent pane...");
    native_observations_cancel(app, 0);
    native_observations_tick(app, true);
}
