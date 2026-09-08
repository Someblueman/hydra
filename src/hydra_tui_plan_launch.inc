/* Detached launch transport; all admission and execution remain in the CLI. */
static bool native_plan_launch(struct app *app, const char *digest) {
    struct native_plan *p=app->plan;
    int fd;
    bool started;
    char *argv[]={"nohup",(char *)app->hydra,"workflow","plan","--workspace-owner",(char *)digest,NULL};
    if (!p || p->state!=PLAN_READY || p->owner_pid>0 || strcmp(p->digest,digest) || native_plan_changed(p)) return false;
    fd=open(p->compiled,O_RDONLY);
    if (fd<0) return false;
    started=native_detached_start(&p->owner_pid,argv,fd); close(fd);
    if (!started) return false;
    native_capture_destroy(&p->launch_job);
    copy_text(p->launched_digest,sizeof(p->launched_digest),digest);
    p->launched_run[0]='\0'; copy_text(p->launch_state,sizeof(p->launch_state),"requested");
    copy_text(app->notice,sizeof(app->notice),"Launch requested; awaiting the durable run receipt. Closing this UI does not cancel execution.");
    return true;
}

static void native_plan_launch_tick(struct app *app, bool watch) {
    struct native_plan *p=app->plan;
    if (!p) return;
    if (p->owner_pid>0) {
        pid_t result=waitpid(p->owner_pid,NULL,WNOHANG);
        if (result==p->owner_pid || (result<0 && errno==ECHILD)) p->owner_pid=0;
    }
    if (p->launch_job.pid && native_capture_step(&p->launch_job)) {
        FILE *input=native_capture_take(&p->launch_job);
        char header[64],line[256],*fields[6];
        bool valid=false;
        if (input && fgets(header,sizeof(header),input) && !strcmp(header,"HYDRA_PLAN_LAUNCH\t1\n") &&
            fgets(line,sizeof(line),input) && strchr(line,'\n') && fgetc(input)==EOF && !ferror(input)) {
            line[strcspn(line,"\n")]='\0';
            if (split_fields(line,fields,6)==5 && !strcmp(fields[0],"L") && !strcmp(fields[1],p->launched_digest) &&
                (!strcmp(fields[3],"-") || (strlen(fields[3])<65 && workflow_id(fields[3])))) {
                bool changed=strcmp(p->launch_state,fields[2]) || (strcmp(fields[3],"-") && strcmp(p->launched_run,fields[3]));
                copy_text(p->launch_state,sizeof(p->launch_state),fields[2]);
                if (strcmp(fields[3],"-")) {
                    if (strcmp(p->launched_run,fields[3])) p->follow_launched_run=true;
                    copy_text(p->launched_run,sizeof(p->launched_run),fields[3]);
                    if (changed) snprintf(app->notice,sizeof(app->notice),"Run %s / launch owner %s / exit %s",fields[3],fields[2],fields[4]);
                } else if (changed) snprintf(app->notice,sizeof(app->notice),"Launch %s / no run receipt / exit %s. No automatic retry.",fields[2],fields[4]);
                valid=true;
            }
        }
        if (input) fclose(input);
        if (!valid) copy_text(app->notice,sizeof(app->notice),p->launched_run[0] ?
            "Launch owner observation unavailable; recorded run retained. Inspect C for execution evidence." :
            "Launch observation unavailable; execution outcome unknown. No automatic retry.");
    }
    /* The workflow snapshot can arrive after the immutable finished receipt. */
    if (p->follow_launched_run && app->workflows) {
        size_t i;
        for (i=0;i<app->workflows->run_count;i++)
            if (!strcmp(app->workflows->runs[i].id,p->launched_run)) {
                app->workflow_run=i; app->workflow_node=0; p->follow_launched_run=false; break;
            }
    }
    if (watch && !p->launch_job.pid && p->launched_digest[0] && strcmp(p->launch_state,"finished")) {
        char *argv[]={(char *)app->hydra,"workflow","plan","--workspace-status",p->launched_digest,NULL};
        (void)native_capture_start(&p->launch_job,argv,3000);
    }
}
