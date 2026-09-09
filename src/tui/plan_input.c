#define _POSIX_C_SOURCE 200809L
#include "internal.h"

static void plan_load_dialog(struct app *app) {
    char path[4096], policy[4096];
    if (app->fleet) return;
    if (prompt_text(app, "Draft JSON path: ", path, sizeof(path)) || !path[0]) return;
    if (prompt_text(app, "Policy JSON path: ", policy, sizeof(policy)) || !policy[0]) return;
    (void)native_plan_load(app, path, policy);
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
        case 'P': plan_load_dialog(app); return true;
        case 'V':
            if (!native_plan_compile(app)) copy_text(app->notice,sizeof(app->notice),"Load draft and policy with P before validation; an active compilation must finish first");
            return true;
        case 'E': plan_execute_dialog(app); return true;
        default: return false;
    }
}
