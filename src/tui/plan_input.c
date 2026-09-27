#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "task_name.h"

static void plan_load_dialog(struct app *app) {
    char path[4096], policy[4096];
    if (app->fleet) return;
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
        "60 minutes, 1 MiB artifacts, 1 GiB free disk, no retries or repairs. No execution yet. y/N: ", tools);
    if (n < 0 || (size_t)n >= size) copy_text(prompt, size, "Review with the guided local policy? No execution yet. y/N: ");
}

static void plan_agent_proposal(struct app *app) {
    struct head *head = selected_head(app);
    char answer[TEXT] = "", output[12288], branch[TEXT], prompt[512];
    char *fields[3], *line, *end;
    if (app->fleet || !head) {
        copy_text(app->notice, sizeof(app->notice), "Select a local agent conversation first; n starts one");
        return;
    }
    copy_text(branch, sizeof(branch), head->branch);
    plan_policy_prompt(head->profile, prompt, sizeof(prompt));
    if (prompt_text(app, prompt, answer, sizeof(answer)) ||
        (strcasecmp(answer, "y") && strcasecmp(answer, "yes"))) return;
    char *argv[] = {(char *)app->hydra, "workflow", "plan", "proposal", branch, "--local-policy", NULL};
    if (run_captured(app, argv, output, sizeof(output), 5000L)) {
        show_result(app, "Agent proposal is not ready", output);
        return;
    }
    copy_text(app->notice, sizeof(app->notice), "Agent proposal response is malformed; no plan loaded");
    line = strchr(output, '\n');
    if (!line || strncmp(output, "HYDRA_PLAN_PROPOSAL\t1\n", 22)) return;
    line++;
    end = strchr(line, '\n');
    if (!end || end[1]) return;
    *end = '\0';
    if (split_fields(line, fields, 3) != 3 || strcmp(fields[0], "P")) return;
    app->notice[0] = '\0';
    if (native_plan_load(app, fields[1], fields[2]))
        copy_text(app->plan->proposal_head, sizeof(app->plan->proposal_head), branch);
}

static void plan_validate(struct app *app) {
    if (app->plan && app->plan->state==PLAN_RETURNED && !native_plan_changed(app->plan))
        copy_text(app->notice,sizeof(app->notice),"This revision was returned for changes. Hydra validates the agent's next published revision.");
    else if (!native_plan_compile(app))
        copy_text(app->notice,sizeof(app->notice),"Review the agent proposal with P (I imports files) before validation; an active compilation must finish first");
}

bool native_plan_key(struct app *app, char key) {
    switch (key) {
        case 'P': plan_agent_proposal(app); return true;
        case 'I': plan_load_dialog(app); return true;
        case 'V': plan_validate(app); return true;
        case 'F': native_plan_request_changes(app); return true;
        case 'E': native_plan_execute(app); return true;
        default: return false;
    }
}
