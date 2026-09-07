/* Detached launch transport; all admission and execution remain in the CLI. */
static bool native_plan_launch(struct app *app, const char *digest) {
    struct native_plan *p=app->plan;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int fd,result;
    char *argv[]={"nohup",(char *)app->hydra,"workflow","plan","--workspace-owner",(char *)digest,NULL};
    if (!p || p->state!=PLAN_READY || p->owner_pid>0 || strcmp(p->digest,digest) || native_plan_changed(p)) return false;
    fd=open(p->compiled,O_RDONLY);
    if (fd<0) return false;
    posix_spawn_file_actions_init(&actions); posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&attributes,0);
    posix_spawn_file_actions_adddup2(&actions,fd,STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions,fd);
    posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    result=posix_spawnp(&p->owner_pid,argv[0],&actions,&attributes,argv,environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes); close(fd);
    if (result) { p->owner_pid=0; return false; }
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
                copy_text(p->launch_state,sizeof(p->launch_state),fields[2]);
                if (strcmp(fields[3],"-")) {
                    size_t i;
                    if (strcmp(p->launched_run,fields[3])) p->follow_launched_run=true;
                    copy_text(p->launched_run,sizeof(p->launched_run),fields[3]);
                    if (p->follow_launched_run && app->workflows) for (i=0;i<app->workflows->run_count;i++)
                        if (!strcmp(app->workflows->runs[i].id,fields[3])) {
                            app->workflow_run=i; app->workflow_node=0; p->follow_launched_run=false; break;
                        }
                    snprintf(app->notice,sizeof(app->notice),"Run %s / launch owner %s / exit %s",fields[3],fields[2],fields[4]);
                } else snprintf(app->notice,sizeof(app->notice),"Launch %s / no run receipt / exit %s. No automatic retry.",fields[2],fields[4]);
                valid=true;
            }
        }
        if (input) fclose(input);
        if (!valid) copy_text(app->notice,sizeof(app->notice),"Launch observation unavailable; execution outcome unknown. No automatic retry.");
    }
    if (watch && !p->launch_job.pid && p->launched_digest[0]) {
        char *argv[]={(char *)app->hydra,"workflow","plan","--workspace-status",p->launched_digest,NULL};
        (void)native_capture_start(&p->launch_job,argv,3000);
    }
}
