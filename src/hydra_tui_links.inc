/* Read-only association snapshot; workflow and head identities remain authoritative. */
struct native_links {
    char project[128], root[SOURCE_TEXT];
    struct { char run[80], branch[TEXT]; } refs[512];
    size_t count;
    bool stale;
};

static bool native_links_load(FILE *input, struct native_links *m) {
    char line[SOURCE_TEXT+256];
    bool header=false, project=false, end=false;
    while (fgets(line,sizeof(line),input)) {
        char *fields[4];
        size_t len=strlen(line), n;
        if (end || !len || line[len-1]!='\n') return false;
        line[len-1]='\0'; n=split_fields(line,fields,4);
        if (!header) {
            if (n!=2 || strcmp(fields[0],"HYDRA_WORKSPACE_LINKS") || strcmp(fields[1],"1")) return false;
            header=true;
        } else if (!project) {
            if (n!=3 || strcmp(fields[0],"P") || !workflow_id(fields[1]) ||
                strlen(fields[1])>=sizeof(m->project) || strlen(fields[2])>=sizeof(m->root)) return false;
            copy_text(m->project,sizeof(m->project),fields[1]);
            copy_text(m->root,sizeof(m->root),fields[2]); project=true;
        } else if (n==1 && !strcmp(fields[0],"Z")) end=true;
        else if (n==3 && !strcmp(fields[0],"L") && m->count<512 && workflow_id(fields[1]) &&
                 fields[2][0] && strlen(fields[2])<TEXT) {
            copy_text(m->refs[m->count].run,80,fields[1]);
            copy_text(m->refs[m->count++].branch,TEXT,fields[2]);
        } else return false;
    }
    return header && project && end && !ferror(input);
}

static void native_links_accept(struct app *app, FILE *input) {
    struct native_links *next=input ? calloc(1,sizeof(*next)) : NULL;
    bool ok=next && native_links_load(input,next);
    if (input) fclose(input);
    if (!ok) { free(next); if (app->links) app->links->stale=true; return; }
    free(app->links); app->links=next;
}

static bool native_links_match(struct app *app, size_t run, size_t head) {
    size_t i;
    if (!app->links || !app->workflows || app->fleet) return false;
    for (i=0;i<app->links->count;i++) if (
        !strcmp(app->links->refs[i].run,app->workflows->runs[run].id) &&
        !strcmp(app->links->refs[i].branch,app->model.heads[head].branch)) return true;
    return false;
}
