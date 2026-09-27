#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "task_name.h"

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

static bool task_profile(struct app *app, const char *branch, char profile[TEXT]) {
    char output[8192], title[TEXT];
    char *argv[] = {(char *)app->hydra, "agent", "list", NULL};
    if (run_captured(app, argv, output, sizeof(output), 5000L)) {
        show_result(app, "Agent selection unavailable", output);
        return false;
    }
    if (snprintf(title, sizeof(title), "Branch %s. Choose an available agent; none opens a shell", branch) >= (int)sizeof(title))
        copy_text(title, sizeof(title), "Choose an available agent; none opens a shell");
    show_result(app, title, output);
    int status = prompt_text(app, "Agent profile (blank=project default, none=shell): ", profile, TEXT);
    app->result_open = false;
    return status == 0;
}

/* The guided local policy (lib/workflow_plan_proposal.sh) authorizes the
 * head's own profile, so the agent is told exactly which tool to name. */
static void planning_agent(const char *profile, char *tools, size_t tools_size, char *step, size_t step_size) {
    int a, b;
    if (!strcmp(profile, "none")) {
        copy_text(tools, tools_size, " (this head has no agent profile, so no agent step is authorized)");
        copy_text(step, step_size, "exec steps on that head that run committed sh or make scripts, declare writes <worker>:* "
            "(also listed in the envelope) and need the spawn step; ");
        return;
    }
    a = snprintf(tools, tools_size, " and profile:%s", profile[0] ? profile : "<your profile>");
    b = snprintf(step, step_size, "an exec step on that head with profile %s, prompt_input, result_file and timeout, "
        "needing the spawn step and declaring writes <worker>:* (also listed in the envelope); ",
        profile[0] ? profile : "set to your own profile (the first line of $HYDRA_STATE_DIR/profile)");
    if (a < 0 || (size_t)a >= tools_size) tools[0] = '\0';
    if (b < 0 || (size_t)b >= step_size) step[0] = '\0';
}

static bool planning_prompt(const char *name, const char *branch, const char *profile, const char *objective, char *out, size_t size) {
    char tools[256], step[512], source[4096];
    int n;
    planning_agent(profile, tools, sizeof(tools), step, sizeof(step));
    if (!getcwd(source, sizeof(source))) copy_text(source, sizeof(source), "the checkout where Hydra runs");
    n = snprintf(out, size,
        "Task: %s\nBranch: %s\nDiscuss and plan this objective with the user: %s\n\n"
        "Hydra planning handoff: do not implement or execute the plan before the user approves it in Hydra, and do not commit implementation changes while planning. "
        "Run hydra workflow plan schema for the draft format, write the draft JSON yourself and publish it from this head with "
        "hydra workflow plan propose <draft.json>; republish when the user asks for revisions. "
        "The guided local policy allows host local; tools sh, git, make%s; writes only inside heads the plan spawns; parallelism 1; "
        "at most 4 heads; 3600 seconds summed over exec timeouts; 1 MiB of artifacts; envelope disk_mb of at least 1024; no retries or repairs. "
        "Express implementation as a spawn step creating a new worker branch with terminal_mode headless; %s"
        "then verify exec steps whose argv runs make or sh with scripts already committed in the source and writes an object report output. "
        "prompt_input names a step input mapped with {\"input\": <name>} to a plan-level data.inputs file (repository-relative path, type file, max_bytes); "
        "result_file must equal the name and path of a declared output of that step. "
        "Validation reads the checkout at %s at its current commit: it must have no tracked changes, and input and script files must exist there. "
        "If validation reports invalid_source, relay its message and recovery; never tell the user to start Hydra from a .hydra-worktrees directory. "
        "The user reviews with B then P, validates with V, requests changes with F and approves the exact revision with E. "
        "Requested changes and run updates arrive in this conversation as lines starting with Hydra:; after approval, follow and report the run but do not execute or modify it. "
        "A saved proposal is not approval or completed work.",
        name, branch, objective[0] ? objective : "Ask the user what they want to achieve, then discuss a plan.", tools, step, source);
    return n > 0 && (size_t)n < size;
}

static void task_started(struct app *app, const char *name, const char *branch) {
    int n = -1;
    if (strcmp(name, branch))
        n = snprintf(app->notice, sizeof(app->notice), "Task \"%s\" started on branch %s; opening its agent pane...", name, branch);
    if (n < 0 || (size_t)n >= sizeof(app->notice))
        n = snprintf(app->notice, sizeof(app->notice), "Task started on branch %s; opening its agent pane...", branch);
    if (n < 0 || (size_t)n >= sizeof(app->notice))
        copy_text(app->notice, sizeof(app->notice), "Task started; opening its agent pane...");
    copy_text(app->pending_task, sizeof(app->pending_task), branch);
}

void new_task_action(struct app *app) {
    char name[TEXT] = "", branch[TEXT] = "", profile[TEXT] = "", objective[4096] = "", output[8192], planning[16384];
    char *init[] = {(char *)app->hydra, "init", NULL, NULL, NULL};
    char *argv[10];
    size_t count = 0;
    if (app->fleet) {
        copy_text(app->notice, sizeof(app->notice), "Remote tasks start on their host; use hydra fleet task");
        return;
    }
    if (prompt_text(app, "Task name: ", name, sizeof(name)) || !name[0]) return;
    if (!task_branch_name(name, branch, sizeof(branch))) {
        copy_text(app->notice, sizeof(app->notice), "Task name needs a letter or digit to name its branch; nothing started");
        return;
    }
    if (!task_profile(app, branch, profile)) return;
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
    if ((objective[0] || strcmp(profile, "none")) && planning_prompt(name, branch, profile, objective, planning, sizeof(planning))) {
        argv[count++] = "--prompt"; argv[count++] = planning;
    }
    argv[count] = NULL;
    if (run_captured(app, argv, output, sizeof(output), 60000L)) {
        show_result(app, "Task did not start; inspect the reported outcome", output);
        return;
    }
    task_started(app, name, branch);
    native_observations_cancel(app, 0);
    native_observations_tick(app, true);
}
