#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "plan_summary.h"
/* The two review decisions on a validated or failed proposal: execute the exact
 * revision shown, or send it back with requested changes. The shell CLI records
 * both; this view only presents them and passes exact bindings. */

static size_t plan_spawns(const struct native_plan *p) {
    size_t i, spawns=0;
    for (i=0;i<p->graph.node_count;i++) if (!strcmp(p->graph.nodes[i].kind,"spawn")) spawns++;
    return spawns;
}

/* Shows the short digest, policy and consequences, then passes the full digest
 * of this displayed revision. Launch still refuses a changed draft or policy. */
/* The execution approval: one question, its policy and consequences. */
static bool plan_execute_confirmed(struct app *app, const char *const lines[], size_t count) {
    char body[2048]="";
    size_t i;
    for (i=0;i<count;i++) text_append(body,sizeof(body),"%s%s",i ? "\n\n" : "",lines[i]);
    return dialog_confirm(app,"Execute plan",body,"Execute",false);
}

static void plan_confirm_execution(struct app *app) {
    struct native_plan *p=app->plan;
    struct plan_summary summary;
    char digest[65], first[TEXT+128], body[512], policy[600], consequences[600];
    const char *lines[4];
    unsigned revision=p->revision;
    if (!plan_summary_parse(p->text,p->text_length,&summary)) {
        copy_text(app->notice,sizeof(app->notice),"Execution summary unavailable in the compiled preview; inspect it with workflow plan show.");
        return;
    }
    copy_text(digest,sizeof(digest),p->digest);
    snprintf(first,sizeof(first),"Execute %s revision %u, digest %.12s?",p->proposal_head[0] ? p->proposal_head : "imported plan",revision,digest);
    plan_summary_policy(&summary,body,sizeof(body));
    snprintf(policy,sizeof(policy),"Policy: %s",body);
    plan_summary_consequences(&summary,p->graph.node_count,plan_spawns(p),consequences,sizeof(consequences));
    lines[0]=first; lines[1]=policy; lines[2]=consequences;
    lines[3]="y executes exactly this validated revision. A changed draft or policy must be validated again.";
    if (!plan_execute_confirmed(app,lines,4)) {
        copy_text(app->notice,sizeof(app->notice),"Execution not approved; nothing was submitted.");
        return;
    }
    if (!app->plan || app->plan->revision!=revision || strcmp(app->plan->digest,digest) || !native_plan_launch(app,digest))
        snprintf(app->notice,sizeof(app->notice),"Execution not submitted: the draft, policy or validation changed after review of revision %u. Validate again with V.",revision);
}

void native_plan_execute(struct app *app) {
    struct native_plan *p=app->plan;
    if (p && p->digest[0] && !strcmp(p->digest,p->launched_digest) && !native_plan_launch_failed(p))
        copy_text(app->notice,sizeof(app->notice),p->launched_run[0] ? "This revision was already submitted. Open C to inspect its recorded run." :
            "This revision was already submitted; its launch has not recorded a run yet. No automatic retry.");
    else if (p && p->state==PLAN_RETURNED)
        copy_text(app->notice,sizeof(app->notice),"This revision was returned for changes; nothing runs until the agent publishes a revision.");
    else if (p && p->state==PLAN_READY) plan_confirm_execution(app);
    else copy_text(app->notice,sizeof(app->notice),"Validate a local plan with V and review its full scope before execution approval.");
}

/* Validation diagnostics travel with the feedback, flattened and bounded. */
static void plan_feedback_text(const struct native_plan *p, const char *feedback, char *out, size_t size) {
    char diagnostics[800]="";
    size_t i;
    if (p->state==PLAN_INVALID && p->text) {
        for (i=0;i+1<sizeof(diagnostics) && i<p->text_length;i++) diagnostics[i]=p->text[i]=='\n' ? ' ' : p->text[i];
        diagnostics[i]='\0';
    }
    snprintf(out,size,"Hydra: the user requested changes to plan revision %u: %s%s%s%s Revise the draft and republish it with "
        "hydra workflow plan propose <draft.json>; nothing was executed.",
        p->revision,feedback,strchr(".!?",feedback[strlen(feedback)-1]) ? "" : ".",
        diagnostics[0] ? " Validation reported: " : "",diagnostics);
}

static bool plan_can_return(struct app *app) {
    struct native_plan *p=app->plan;
    const char *refusal=NULL;
    if (!p || !p->proposal_head[0]) refusal="F returns an agent proposal; review one with P first (: imports files without an agent).";
    else if (p->digest[0] && !strcmp(p->digest,p->launched_digest) && !native_plan_launch_failed(p)) refusal="This revision was already submitted; request changes in the agent's conversation.";
    else if (p->state==PLAN_RETURNED) refusal="Already returned for changes; waiting for the agent's revision.";
    else if (p->job.pid) refusal="Validation is running; request changes when it finishes.";
    if (refusal) copy_text(app->notice,sizeof(app->notice),refusal);
    return !refusal;
}

/* Records the return through the CLI, clears this revision's validation and
 * types the feedback into the agent's conversation. The user presses Enter
 * there: the text is theirs to review before the agent acts on it. */
static bool plan_feedback_form(struct app *app, const char *body, char *feedback, size_t size) {
    struct kt_textinput input;
    struct kt_field field;
    kt_textinput_init(&input,feedback,size);
    memset(&field,0,sizeof(field));
    field.kind=KT_FIELD_TEXT; field.label="What should change"; field.input=&input;
    return dialog_form_body(app,"Request changes",body,&field,1)==0 && feedback[0];
}

void native_plan_request_changes(struct app *app) {
    char feedback[2048]="", output[4096], branch[TEXT], prompt[TEXT+96], text[4096];
    unsigned revision;
    struct head *head;
    if (!plan_can_return(app)) return;
    copy_text(branch,sizeof(branch),app->plan->proposal_head);
    revision=app->plan->revision;
    snprintf(prompt,sizeof(prompt),"Changes to request from %s. Your feedback is typed into its conversation; "
             "you press Enter there.",branch);
    if (!plan_feedback_form(app,prompt,feedback,sizeof(feedback))) return;
    if (!app->plan || app->plan->revision!=revision || app->plan->job.pid) {
        copy_text(app->notice,sizeof(app->notice),"The proposal changed while you typed; nothing was returned. Review the new revision.");
        return;
    }
    char *argv[]={(char *)app->hydra,"workflow","plan","proposal",branch,"--return",feedback,NULL};
    if (run_captured(app,argv,output,sizeof(output),5000L)) { show_result(app,"Changes were not recorded",output); return; }
    plan_feedback_text(app->plan,feedback,text,sizeof(text));
    native_plan_returned(app->plan);
    head=native_plan_head(app,branch);
    if (head && native_plan_converse(app,head,text,false,true))
        snprintf(app->notice,sizeof(app->notice),"Returned. Feedback typed into %.100s's conversation; review it and press Enter to send.",branch);
    else snprintf(app->notice,sizeof(app->notice),"Returned. Feedback saved in %.100s's planning/feedback; its conversation could not be opened.",branch);
}
