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

/* The agents `hydra agent list` reports available, after the project's
 * default; "none" (a plain shell) is always last. */
#define TASK_AGENTS 16
struct task_agents { char names[TASK_AGENTS][32]; const char *options[TASK_AGENTS]; size_t count; };

static void agents_add(struct task_agents *a, const char *name) {
    size_t i;
    if (a->count >= TASK_AGENTS) return;
    for (i = 0; i < a->count; i++) if (!strcmp(a->names[i], name)) return;
    copy_text(a->names[a->count], sizeof(a->names[a->count]), name);
    a->options[a->count] = a->names[a->count];
    a->count++;
}

static bool task_agents(struct app *app, struct task_agents *a) {
    char output[8192], *line, *save = NULL;
    char *argv[] = {(char *)app->hydra, "agent", "list", NULL};
    memset(a, 0, sizeof(*a));
    if (run_captured(app, argv, output, sizeof(output), 5000L)) {
        show_result(app, "Agent selection unavailable", output);
        return false;
    }
    agents_add(a, "project default");
    for (line = strtok_r(output, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char name[32], available[16];
        if (sscanf(line, "%31s %15s", name, available) == 2 && !strcmp(available, "yes") && strcmp(name, "none")) agents_add(a, name);
    }
    agents_add(a, "none");
    return true;
}

/* The task name shows the branch it becomes as it is typed. */
struct task_name_check { char hint[TEXT + 16]; };
static bool task_name_valid(void *context, const char *text, char *error, size_t capacity) {
    struct task_name_check *check = context;
    char branch[TEXT];
    if (!text[0]) { check->hint[0] = '\0'; snprintf(error, capacity, "A task needs a name"); return false; }
    if (!task_branch_name(text, branch, sizeof(branch))) {
        check->hint[0] = '\0';
        snprintf(error, capacity, "Needs a letter or digit to name its branch");
        return false;
    }
    snprintf(check->hint, sizeof(check->hint), "Branch: %s", branch);
    return true;
}

/* One form: the task, its agent and its objective. */
static bool task_form(struct app *app, char name[TEXT], char profile[TEXT], char *objective, size_t objective_size) {
    struct task_agents agents;
    struct task_name_check check = {"Becomes the branch and worktree name"};
    struct kt_textinput name_input, objective_input;
    struct kt_field fields[3];
    if (!task_agents(app, &agents)) return false;
    memset(fields, 0, sizeof(fields));
    kt_textinput_init(&name_input, name, TEXT);
    name_input.validate = task_name_valid; name_input.validate_context = &check;
    kt_textinput_init(&objective_input, objective, objective_size);
    objective_input.placeholder = "optional: leave empty to start with a conversation";
    fields[0].kind = KT_FIELD_TEXT; fields[0].label = "Task"; fields[0].input = &name_input; fields[0].hint = check.hint;
    fields[1].kind = KT_FIELD_CHOICE; fields[1].label = "Agent"; fields[1].options = agents.options;
    fields[1].option_count = agents.count;
    fields[2].kind = KT_FIELD_TEXT; fields[2].label = "Objective"; fields[2].input = &objective_input;
    if (dialog_form_body(app, "New task", "Hydra creates the branch, its worktree and a terminal, and starts the agent there.",
                         fields, 3) != 0) return false;
    profile[0] = '\0';
    if (fields[1].choice > 0 && fields[1].choice < agents.count) copy_text(profile, TEXT, agents.names[fields[1].choice]);
    return true;
}

/* The guided local policy (lib/workflow_plan_proposal.sh) authorizes the
 * head's own profile, so the agent is told exactly which tool to name. */
static void planning_agent(const char *profile, char *tools, size_t tools_size, char *step, size_t step_size) {
    int a, b;
    if (!strcmp(profile, "none")) {
        copy_text(tools, tools_size, " (this head has no agent profile, so no agent step is authorized)");
        copy_text(step, step_size, "exec steps on that head that run the repository's make or sh commands, declare writes <worker>:* "
            "(also listed in the envelope) and need the spawn step; ");
        return;
    }
    a = snprintf(tools, tools_size, " and profile:%s", profile[0] ? profile : "<your profile>");
    b = snprintf(step, step_size, "an exec step on that head with profile %s, the worker's complete instructions inline in prompt, "
        "result_file (the name and path of a declared output of that step) and timeout, needing the spawn step and declaring "
        "writes <worker>:* (also listed in the envelope); ",
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
        "Hydra planning handoff: plan only; do not implement or execute the plan before the user approves it in Hydra. "
        "Never commit while planning. Never commit to or change the user's checkout at %s or any branch; write only your draft and scratch files in this worktree. "
        "Run hydra workflow plan schema for the draft format, then publish from this head with "
        "hydra workflow plan propose <draft.json> [--asset NAME=FILE]...; republish when the user asks for revisions. "
        "The guided local policy allows host local; tools sh, git, make%s; writes only inside heads the plan spawns; parallelism 1; "
        "at most 4 heads; 3600 seconds summed over exec timeouts; 1 MiB of artifacts; envelope disk_mb of at least 1024; no retries or repairs. "
        "Express implementation as a spawn step creating a new worker branch with terminal_mode headless; %s"
        "then verify on the worker head, after that step, with the repository's own checks (for example argv [make, test]). "
        "A check's report step may run a small custom script: publish it with --asset NAME=FILE, declare data.inputs NAME as "
        "{\"asset\": NAME, \"type\": \"file\", \"max_bytes\": N}, map it as a step input and run argv [sh, @input/NAME]; "
        "it writes the schema's object report to $HYDRA_WORKFLOW_OUTPUTS_DIR. Use --asset only for genuinely custom files. "
        "Validation binds that checkout at its current commit (no tracked changes); the plan carries its prompt and assets, so nothing is committed there. "
        "If validation reports invalid_source, relay its message and recovery; never tell the user to start Hydra from a .hydra-worktrees directory. "
        "The user reviews with B then P, validates with V, requests changes with F and approves the exact revision with E. "
        "Requested changes and run updates arrive in this conversation as lines starting with Hydra:; after approval, follow and report the run but do not execute or modify it. "
        "A saved proposal is not approval or completed work.",
        name, branch, objective[0] ? objective : "Ask the user what they want to achieve, then discuss a plan.", source, tools, step);
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
    if (!task_form(app, name, profile, objective, sizeof(objective)) || !name[0]) return;
    if (!task_branch_name(name, branch, sizeof(branch))) {
        copy_text(app->notice, sizeof(app->notice), "Task name needs a letter or digit to name its branch; nothing started");
        return;
    }
    if (profile[0]) { init[2] = "--profile"; init[3] = profile; }
    if (run_captured(app, init, output, sizeof(output), 15000L)) {
        action_failed(app, "Registering this project did not finish", output);
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
        action_failed(app, "Starting a task did not finish", output);
        show_result(app, "Task did not start; inspect the reported outcome", output);
        return;
    }
    task_started(app, name, branch);
    native_observations_cancel(app, 0);
    native_observations_tick(app, true);
}
