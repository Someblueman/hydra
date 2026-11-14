#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "task_name.h"

/* Import plan files (the palette's "import plan files"): a draft and a
 * policy loaded into the workspace's plan review. */
void native_plan_import(struct app *app) {
    char path[4096], policy[4096];
    if (!native_workspace_init(app)) return;
    if (app->view != 7) enter_view(app, 7);
    if (prompt_text(app, "Draft JSON path: ", path, sizeof(path)) || !path[0]) return;
    if (prompt_text(app, "Policy JSON path: ", policy, sizeof(policy)) || !policy[0]) return;
    (void)native_plan_load(app, path, policy);
}

/* Describes the guided policy that `proposal --local-policy` writes for this head. */
static void plan_policy_prompt(const char *profile, char *prompt, size_t size) {
    char tools[TEXT];
    int n;
    if (!task_profile_tool(profile) || snprintf(tools, sizeof(tools), "sh/git/make and profile:%s", profile) >= (int)sizeof(tools))
        copy_text(tools, sizeof(tools), "sh/git/make only (no agent profile on this head)");
    n = snprintf(prompt, size, "Review with local policy: %s, writes only in heads the plan spawns, 1 worker, 4 heads, "
        "60 minutes, 1 MiB artifacts, 1 GiB free disk, no retries or repairs.\n\nThe plan carries its prompts and assets; "
        "nothing is committed to your checkout. No execution yet.", tools);
    if (n < 0 || (size_t)n >= size) copy_text(prompt, size, "Review with the guided local policy? No execution yet.");
}

/* Loads the head's published proposal under the guided local policy; false
 * (with the reason shown) when it is not ready. */
bool native_plan_proposal_load(struct app *app, const char *head_branch) {
    char output[12288], branch[TEXT];
    char *fields[3], *line, *end;
    char *argv[] = {(char *)app->hydra, "workflow", "plan", "proposal", branch, "--local-policy", NULL};
    copy_text(branch, sizeof(branch), head_branch);
    if (run_captured(app, argv, output, sizeof(output), 5000L)) {
        show_result(app, "Agent proposal is not ready", output);
        return false;
    }
    copy_text(app->notice, sizeof(app->notice), "Agent proposal response is malformed; no plan loaded");
    line = strchr(output, '\n');
    if (!line || strncmp(output, "HYDRA_PLAN_PROPOSAL\t1\n", 22)) return false;
    line++;
    end = strchr(line, '\n');
    if (!end || end[1]) return false;
    *end = '\0';
    if (split_fields(line, fields, 3) != 3 || strcmp(fields[0], "P")) return false;
    app->notice[0] = '\0';
    if (!native_plan_load(app, fields[1], fields[2])) return false;
    copy_text(app->plan->proposal_head, sizeof(app->plan->proposal_head), branch);
    return true;
}

void native_plan_proposal(struct app *app) {
    struct head *head = selected_head(app);
    char branch[TEXT], prompt[512];
    if (!head || head_remote(head)) {
        copy_text(app->notice, sizeof(app->notice), "Select a local agent conversation first; n starts one");
        return;
    }
    copy_text(branch, sizeof(branch), head->branch);
    plan_policy_prompt(head->profile, prompt, sizeof(prompt));
    if (!dialog_confirm(app, "Review proposal", prompt, "Review", false)) return;
    (void)native_plan_proposal_load(app, branch);
}

void native_plan_validate(struct app *app) {
    if (app->plan && app->plan->state==PLAN_RETURNED && !native_plan_changed(app->plan))
        copy_text(app->notice,sizeof(app->notice),"This revision was returned for changes. Hydra validates the agent's next published revision.");
    else if (!native_plan_compile(app))
        copy_text(app->notice,sizeof(app->notice),"Review the agent proposal with P (I imports files) before validation; an active compilation must finish first");
}

/* The planning keys reach their actions through plan_actions.c. */
bool native_plan_key(struct app *app, char key) {
    switch (key) {
        case 'P': plan_action_load_proposal(app); return true;
        case 'V': plan_action_validate(app); return true;
        case 'F': plan_action_request_changes(app); return true;
        case 'E': plan_action_approve(app); return true;
        default: return false;
    }
}
