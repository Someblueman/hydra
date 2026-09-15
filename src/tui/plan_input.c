#define _POSIX_C_SOURCE 200809L
#include "internal.h"

static void plan_load_dialog(struct app *app) {
    char path[4096], policy[4096];
    if (app->fleet) return;
    if (prompt_text(app, "Draft JSON path: ", path, sizeof(path)) || !path[0]) return;
    if (prompt_text(app, "Policy JSON path: ", policy, sizeof(policy)) || !policy[0]) return;
    (void)native_plan_load(app, path, policy);
}

static void plan_agent_proposal(struct app *app) {
    struct head *head = selected_head(app);
    char answer[TEXT] = "", output[12288], branch[TEXT];
    char *fields[3], *line, *end;
    if (app->fleet || !head) {
        copy_text(app->notice, sizeof(app->notice), "Select a local agent conversation first; n starts one");
        return;
    }
    copy_text(branch, sizeof(branch), head->branch);
    if (prompt_text(app, "Review with local policy: sh/git, 1 worker, 4 heads, 5 minutes, 1 MiB artifacts, no retries or repairs. No execution yet. y/N: ", answer, sizeof(answer)) ||
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

static void plan_execute_dialog(struct app *app) {
    struct native_plan *p=app->plan;
    if (p && p->digest[0] && !strcmp(p->digest,p->launched_digest)) {
        copy_text(app->notice,sizeof(app->notice),"This revision was already submitted. Open C to inspect its recorded run.");
    } else if (p && p->state==PLAN_READY && !app->fleet) {
        char digest[65],answer[128],prompt[256];
        unsigned revision=p->revision;
        copy_text(digest,sizeof(digest),p->digest);
        snprintf(prompt,sizeof(prompt),"Execute revision %u? Type exact digest %s: ",revision,digest);
        if (prompt_text(app,prompt,answer,sizeof(answer)) != 0) return;
        if (!app->plan || app->plan->revision!=revision || strcmp(answer,digest) || !native_plan_launch(app,digest))
            copy_text(app->notice,sizeof(app->notice),"Execution not submitted: digest, revision or launch state did not match. Review and validate again.");
    } else copy_text(app->notice,sizeof(app->notice),"Validate a local plan with V and review its full scope before exact-digest execution approval.");
}

bool native_plan_key(struct app *app, char key) {
    switch (key) {
        case 'P': plan_agent_proposal(app); return true;
        case 'I': plan_load_dialog(app); return true;
        case 'V':
            if (!native_plan_compile(app)) copy_text(app->notice,sizeof(app->notice),"Review the agent proposal with P (I imports files) before validation; an active compilation must finish first");
            return true;
        case 'E': plan_execute_dialog(app); return true;
        default: return false;
    }
}
