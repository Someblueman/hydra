/* Planning presentation only. The shell CLI compiles and admits execution. */
#define NATIVE_PLAN_LIMIT (256U*1024U)
#define NATIVE_PLAN_TEXT (1024U*1024U)
enum native_plan_state { PLAN_DRAFT, PLAN_VALIDATING, PLAN_INVALID, PLAN_READY, PLAN_UNAVAILABLE };
struct native_plan {
    char path[4096], policy[4096], directory[4096], compiled[4096];
    char digest[65], objective[4096], notice[TEXT];
    unsigned revision, compilation;
    enum native_plan_state state;
    struct native_capture job;
    bool projecting;
    char *source_bytes, *policy_bytes, *text;
    size_t source_length, policy_length, text_length, text_scroll, selected;
    struct workflow_model graph;
};
static char *native_plan_read(const char *path, size_t *length) {
    struct stat info;
    char *bytes;
    size_t used=0;
    int fd=open(path,O_RDONLY|O_NONBLOCK);
    if (fd<0) return NULL;
    if (fcntl(fd,F_SETFD,FD_CLOEXEC)<0 || fstat(fd,&info) || !S_ISREG(info.st_mode) ||
        info.st_size<0 || (uint64_t)info.st_size>NATIVE_PLAN_LIMIT) { close(fd); return NULL; }
    bytes=malloc(NATIVE_PLAN_LIMIT+1);
    if (!bytes) { close(fd); return NULL; }
    while (used<NATIVE_PLAN_LIMIT+1) {
        ssize_t n=read(fd,bytes+used,NATIVE_PLAN_LIMIT+1-used);
        if (!n) break;
        if (n<0) { if (errno==EINTR) continue; free(bytes); close(fd); return NULL; }
        used+=(size_t)n;
    }
    close(fd);
    if (used>NATIVE_PLAN_LIMIT) { free(bytes); return NULL; }
    bytes[used]='\0'; *length=used; return bytes;
}
static bool native_plan_changed(struct native_plan *p) {
    size_t a=0,b=0;
    char *source=native_plan_read(p->path,&a), *policy=native_plan_read(p->policy,&b);
    bool changed=(source==NULL)!=(p->source_bytes==NULL) || (policy==NULL)!=(p->policy_bytes==NULL) ||
        a!=p->source_length || b!=p->policy_length || (source && memcmp(source,p->source_bytes,a)) ||
        (policy && memcmp(policy,p->policy_bytes,b));
    free(source); free(policy); return changed;
}
static void native_plan_message(struct native_plan *p, const char *text) {
    free(p->text); p->text=strdup(text); p->text_length=p->text ? strlen(p->text) : 0;
    p->text_scroll=0;
}
static void native_plan_destroy(struct app *app) {
    struct native_plan *p=app->plan;
    if (!p) return;
    native_capture_destroy(&p->job);
    if (p->directory[0]) {
        unsigned i;
        char path[4096];
        const char *inputs[]={"draft.json","policy.json"};
        for (i=0;i<2;i++) if (snprintf(path,sizeof(path),"%s/%s",p->directory,inputs[i])<(int)sizeof(path)) (void)unlink(path);
        for (i=1;i<=p->compilation;i++) if (snprintf(path,sizeof(path),"%s/compiled-%u.json",p->directory,i)<(int)sizeof(path)) (void)unlink(path);
        (void)rmdir(p->directory);
    }
    free(p->source_bytes); free(p->policy_bytes); free(p->text); free(p); app->plan=NULL;
}
static bool native_plan_init(struct app *app) {
    struct native_plan *p;
    const char *temp=getenv("TMPDIR");
    if (app->plan) return true;
    p=calloc(1,sizeof(*p));
    if (!p) return false;
    p->job.fd=-1; app->plan=p;
    if (snprintf(p->directory,sizeof(p->directory),"%s/hydra-ui-plan.XXXXXX",temp && temp[0] ? temp : "/tmp")>=(int)sizeof(p->directory) || !mkdtemp(p->directory)) {
        p->directory[0]='\0'; native_plan_destroy(app); return false;
    }
    return true;
}
static bool native_plan_write(struct native_plan *p, const char *name, const char *bytes, size_t length, char path[4096]) {
    FILE *out;
    bool ok;
    if (snprintf(path,4096,"%s/%s",p->directory,name)>=4096) return false;
    out=fopen(path,"wb");
    if (!out) return false;
    ok=fwrite(bytes,1,length,out)==length;
    return fclose(out)==0 && ok;
}
static bool native_plan_load(struct app *app, const char *path, const char *policy) {
    struct native_plan *p;
    char *a,*b;
    size_t an=0,bn=0;
    if (strlen(path)>=4096 || strlen(policy)>=4096 || !native_plan_init(app)) return false;
    p=app->plan;
    native_capture_destroy(&p->job); p->state=PLAN_DRAFT; p->digest[0]='\0';
    a=native_plan_read(path,&an); b=native_plan_read(policy,&bn);
    copy_text(p->path,sizeof(p->path),path); copy_text(p->policy,sizeof(p->policy),policy);
    free(p->source_bytes); free(p->policy_bytes);
    p->source_bytes=a; p->policy_bytes=b; p->source_length=an; p->policy_length=bn;
    p->revision++; p->graph.run_count=0; p->graph.node_count=0; p->selected=0; p->objective[0]='\0';
    if (!a || !b) {
        p->state=PLAN_INVALID;
        native_plan_message(p,"Draft or policy unavailable. Each must be a regular file of at most 256 KiB. Press P to choose files again.");
        return false;
    }
    native_plan_message(p,"Conversational proposal loaded. No validation or execution has occurred. Press V to validate and compile the current draft and policy.");
    return true;
}
static bool native_plan_compile(struct app *app) {
    struct native_plan *p=app->plan;
    char draft[4096], policy[4096];
    char *argv[8];
    if (!p || !p->path[0] || p->job.pid) return false;
    if (native_plan_changed(p)) {
        char path[4096], policy_path[4096];
        copy_text(path,sizeof(path),p->path); copy_text(policy_path,sizeof(policy_path),p->policy);
        if (!native_plan_load(app,path,policy_path)) return false;
    }
    if (!p->source_bytes || !p->policy_bytes || p->compilation>=1000) return false;
    p->compilation++;
    if (!native_plan_write(p,"draft.json",p->source_bytes,p->source_length,draft) ||
        !native_plan_write(p,"policy.json",p->policy_bytes,p->policy_length,policy) ||
        snprintf(p->compiled,sizeof(p->compiled),"%s/compiled-%u.json",p->directory,p->compilation)>=(int)sizeof(p->compiled)) return false;
    argv[0]=(char *)app->hydra; argv[1]="workflow"; argv[2]="plan"; argv[3]="compile";
    argv[4]=draft; argv[5]=policy; argv[6]=p->compiled; argv[7]=NULL;
    p->digest[0]='\0'; p->projecting=false; p->state=PLAN_VALIDATING;
    native_plan_message(p,"Validating the draft and policy, binding source and inputs. Agent terminal input remains available.");
    if (native_capture_start(&p->job,argv,30000)) return true;
    p->state=PLAN_INVALID; native_plan_message(p,"Unable to start plan compiler."); return false;
}
