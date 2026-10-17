#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include <dirent.h>
#include <fcntl.h>
/* Planning presentation only. The shell CLI compiles and admits execution. */

/* A draft's assets (published with propose --asset) sit in assets/ beside it. */
static bool native_plan_assets_dir(const char *draft, char out[4096]) {
    const char *slash=strrchr(draft,'/');
    int n=slash ? snprintf(out,4096,"%.*s/assets",(int)(slash-draft),draft) : snprintf(out,4096,"assets");
    return n>0 && n<4096;
}
static unsigned long long native_plan_entry_stamp(const char *name, const struct stat *info) {
    unsigned long long hash=1469598103934665603ULL;
    for (; *name; name++) hash=(hash^(unsigned char)*name)*1099511628211ULL;
    return hash^((unsigned long long)info->st_ino*31ULL+(unsigned long long)info->st_size*131ULL+(unsigned long long)info->st_mtime);
}
/* Changes whenever propose replaces the asset set or a file in it changes;
 * zero when there is no assets directory. Order independent. */
static unsigned long long native_plan_assets_stamp(const char *draft) {
    char directory[4096], path[4096];
    struct stat info;
    struct dirent *entry;
    DIR *listing;
    unsigned long long stamp;
    if (!native_plan_assets_dir(draft,directory) || lstat(directory,&info) || !S_ISDIR(info.st_mode)) return 0;
    stamp=native_plan_entry_stamp(".",&info)|1ULL;
    if (!(listing=opendir(directory))) return stamp;
    while ((entry=readdir(listing))) {
        if (entry->d_name[0]=='.' || snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name)>=(int)sizeof(path) || lstat(path,&info)) continue;
        stamp+=native_plan_entry_stamp(entry->d_name,&info);
    }
    closedir(listing); return stamp;
}




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
bool native_plan_changed(struct native_plan *p) {
    size_t a=0,b=0;
    char *source=native_plan_read(p->path,&a), *policy=native_plan_read(p->policy,&b);
    bool changed=(source==NULL)!=(p->source_bytes==NULL) || (policy==NULL)!=(p->policy_bytes==NULL) ||
        a!=p->source_length || b!=p->policy_length || (source && memcmp(source,p->source_bytes,a)) ||
        (policy && memcmp(policy,p->policy_bytes,b)) || native_plan_assets_stamp(p->path)!=p->assets_stamp;
    free(source); free(policy); return changed;
}
void native_plan_message(struct native_plan *p, const char *text) {
    free(p->text); p->text=strdup(text); p->text_length=p->text ? strlen(p->text) : 0;
    p->text_scroll=0;
}
void native_plan_destroy(struct app *app) {
    struct native_plan *p=app->plan;
    if (!p) return;
    native_capture_destroy(&p->job);
    native_capture_destroy(&p->launch_job);
    /* The detached workflow owner outlives this UI. Never signal it here. */
    if (p->owner_pid>0) (void)waitpid(p->owner_pid,NULL,WNOHANG);
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
    p->job.fd=-1; p->launch_job.fd=-1; app->plan=p;
    if (snprintf(p->directory,sizeof(p->directory),"%s/hydra-ui-plan.XXXXXX",temp && temp[0] ? temp : "/tmp")>=(int)sizeof(p->directory) || !mkdtemp(p->directory)) {
        p->directory[0]='\0'; native_plan_destroy(app); return false;
    }
    return true;
}
static bool native_plan_write(struct native_plan *p, const char *name, const char *bytes, size_t length, char path[4096]) {
    FILE *out;
    bool ok;
    int fd;
    if (snprintf(path,4096,"%s/%s",p->directory,name)>=4096) return false;
    /* Private whatever the caller's umask, like the rest of Hydra's state. */
    fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);
    out=fd<0 ? NULL : fdopen(fd,"wb");
    if (!out) { if (fd>=0) close(fd); return false; }
    ok=fwrite(bytes,1,length,out)==length;
    return fclose(out)==0 && ok;
}
/* The returned revision has no validation, preview or dependency graph. */
void native_plan_returned(struct native_plan *p) {
    char text[512];
    p->state=PLAN_RETURNED; p->digest[0]='\0'; p->graph.run_count=0; p->graph.node_count=0; p->selected=0;
    snprintf(text,sizeof(text),"Returned for changes at revision %u. This draft cannot be validated or executed. "
        "Hydra shows the agent's next published revision here; A opens the conversation.",p->revision);
    native_plan_message(p,text);
}

/* A returned draft stays returned while its bytes are unchanged. */
static bool native_plan_still_returned(const struct native_plan *p, const char *path, const char *bytes, size_t length) {
    return p->state==PLAN_RETURNED && bytes && p->source_bytes && !strcmp(p->path,path) &&
        length==p->source_length && !memcmp(bytes,p->source_bytes,length);
}

bool native_plan_load(struct app *app, const char *path, const char *policy) {
    struct native_plan *p;
    char *a,*b;
    size_t an=0,bn=0;
    bool returned;
    if (strlen(path)>=4096 || strlen(policy)>=4096 || !native_plan_init(app)) return false;
    p=app->plan;
    a=native_plan_read(path,&an); b=native_plan_read(policy,&bn);
    returned=native_plan_still_returned(p,path,a,an);
    if (strcmp(p->path,path) || strcmp(p->policy,policy)) p->proposal_head[0]='\0';
    native_capture_destroy(&p->job); p->state=PLAN_DRAFT; p->digest[0]='\0';
    copy_text(p->path,sizeof(p->path),path); copy_text(p->policy,sizeof(p->policy),policy);
    free(p->source_bytes); free(p->policy_bytes);
    p->source_bytes=a; p->policy_bytes=b; p->source_length=an; p->policy_length=bn;
    p->assets_stamp=native_plan_assets_stamp(path);
    p->revision++; p->graph.run_count=0; p->graph.node_count=0; p->selected=0; p->objective[0]='\0';
    if (!a || !b) {
        p->state=PLAN_INVALID;
        native_plan_message(p,"Draft or policy unavailable. Each must be a regular file of at most 256 KiB. Press I to choose files again.");
        return false;
    }
    if (returned) { native_plan_returned(p); return true; }
    native_plan_message(p,"Conversational proposal loaded. No validation or execution has occurred. "
        "V validates the current draft and policy; F sends it back to the agent with requested changes.");
    return true;
}
/* A returned draft compiles only after the agent republishes changed bytes. */
static bool native_plan_current(struct app *app, struct native_plan *p) {
    char path[4096], policy_path[4096];
    if (!native_plan_changed(p)) return p->state!=PLAN_RETURNED;
    copy_text(path,sizeof(path),p->path); copy_text(policy_path,sizeof(policy_path),p->policy);
    return native_plan_load(app,path,policy_path) && p->state!=PLAN_RETURNED;
}
/* Private copies of the reviewed draft and policy, and the next artifact path. */
static bool native_plan_snapshot(struct native_plan *p, char draft[4096], char policy[4096]) {
    return native_plan_write(p,"draft.json",p->source_bytes,p->source_length,draft) &&
        native_plan_write(p,"policy.json",p->policy_bytes,p->policy_length,policy) &&
        snprintf(p->compiled,sizeof(p->compiled),"%s/compiled-%u.json",p->directory,p->compilation)<(int)sizeof(p->compiled);
}
bool native_plan_compile(struct app *app) {
    struct native_plan *p=app->plan;
    char draft[4096], policy[4096], assets[4096];
    char *argv[10];
    struct stat info;
    if (!p || !p->path[0] || p->job.pid) return false;
    if (!native_plan_current(app,p)) return false;
    if (!p->source_bytes || !p->policy_bytes || p->compilation>=1000) return false;
    p->compilation++;
    if (!native_plan_snapshot(p,draft,policy)) return false;
    argv[0]=(char *)app->hydra; argv[1]="workflow"; argv[2]="plan"; argv[3]="compile";
    argv[4]=draft; argv[5]=policy; argv[6]=p->compiled; argv[7]=NULL;
    /* The compiled artifact embeds the assets, so approval binds their bytes. */
    if (native_plan_assets_dir(p->path,assets) && !lstat(assets,&info) && S_ISDIR(info.st_mode)) {
        argv[7]="--assets-dir"; argv[8]=assets; argv[9]=NULL;
    }
    p->digest[0]='\0'; p->projecting=false; p->state=PLAN_VALIDATING;
    native_plan_message(p,"Validating the draft and policy, binding source and inputs. Agent terminal input remains available.");
    if (native_capture_start(&p->job,argv,30000)) return true;
    p->state=PLAN_INVALID; native_plan_message(p,"Unable to start plan compiler."); return false;
}
