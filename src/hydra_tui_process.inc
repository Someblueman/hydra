/* Bounded read-only subprocess capture. The caller owns the stream and process;
 * step never waits for a child, so terminal I/O can continue during observations. */
struct native_capture {
    pid_t pid;
    int fd, status;
    FILE *output;
    struct timespec started;
    long budget_ms;
    size_t bytes;
    bool eof, reaped, failed, timed_out;
};

static void native_capture_destroy(struct native_capture *p) {
    if (p->pid>0 && !p->reaped) {
        (void)kill(-p->pid,SIGKILL); (void)kill(p->pid,SIGKILL);
        while (waitpid(p->pid,&p->status,0)<0 && errno==EINTR) { }
    }
    if (p->fd>=0) close(p->fd);
    if (p->output) fclose(p->output);
    memset(p,0,sizeof(*p)); p->fd=-1;
}

static bool native_capture_start(struct native_capture *p, char *const argv[], long budget_ms) {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int pipes[2], result;
    memset(p,0,sizeof(*p)); p->fd=-1; p->budget_ms=budget_ms;
    p->output=tmpfile();
    if (!p->output) return false;
    if (fcntl(fileno(p->output),F_SETFD,FD_CLOEXEC)<0 || pipe(pipes)<0) goto fail;
    p->fd=pipes[0];
    if (fcntl(p->fd,F_SETFD,FD_CLOEXEC)<0 || fcntl(pipes[1],F_SETFD,FD_CLOEXEC)<0 ||
        fcntl(p->fd,F_SETFL,O_NONBLOCK)<0) { close(pipes[1]); goto fail; }
    posix_spawn_file_actions_init(&actions); posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&attributes,0);
    posix_spawn_file_actions_addclose(&actions,p->fd);
    posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    posix_spawn_file_actions_addclose(&actions,pipes[1]);
    result=posix_spawnp(&p->pid,argv[0],&actions,&attributes,argv,environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes); close(pipes[1]);
    if (result) { p->pid=0; goto fail; }
    (void)clock_gettime(CLOCK_MONOTONIC,&p->started);
    return true;
fail:
    native_capture_destroy(p); return false;
}

static bool native_capture_step(struct native_capture *p) {
    struct timespec now;
    size_t chunks;
    long elapsed;
    if (!p->pid) return true;
    (void)clock_gettime(CLOCK_MONOTONIC,&now);
    elapsed=(long)(now.tv_sec-p->started.tv_sec)*1000L+(long)(now.tv_nsec-p->started.tv_nsec)/1000000L;
    if (elapsed>=p->budget_ms || stop_requested) { p->timed_out=true; p->failed=true; }
    for (chunks=0; !p->failed && !p->eof && chunks<32; chunks++) {
        char bytes[8192];
        ssize_t n=read(p->fd,bytes,sizeof(bytes));
        if (!n) { p->eof=true; break; }
        if (n<0) { if (errno!=EAGAIN && errno!=EINTR) p->failed=true; break; }
        if ((size_t)n>MAX_DATA_BYTES-p->bytes || fwrite(bytes,1,(size_t)n,p->output)!=(size_t)n) p->failed=true;
        else p->bytes+=(size_t)n;
    }
    if (p->failed) {
        /* Group cleanup also stops a descendant that kept stdout open. */
        (void)kill(-p->pid,SIGKILL);
        if (!p->reaped) (void)kill(p->pid,SIGKILL);
        p->eof=true;
    }
    if (!p->reaped) {
        pid_t result=waitpid(p->pid,&p->status,WNOHANG);
        if (result==p->pid) p->reaped=true;
        else if (result<0 && errno!=EINTR) { p->failed=true; p->reaped=true; }
    }
    return p->reaped && p->eof;
}

/* Transfer complete stdout even on a normal nonzero exit, for compiler
 * diagnostics. Timeout/transport failures never become complete documents. */
static FILE *native_capture_result(struct native_capture *p, bool *success) {
    FILE *out=NULL;
    *success=p->reaped && WIFEXITED(p->status) && WEXITSTATUS(p->status)==0 && !p->failed;
    if (p->reaped && p->eof && WIFEXITED(p->status) && !p->failed && fflush(p->output)==0 && fseek(p->output,0,SEEK_SET)==0) {
        out=p->output; p->output=NULL;
    }
    native_capture_destroy(p);
    return out;
}
static FILE *native_capture_take(struct native_capture *p) {
    bool success;
    FILE *out=native_capture_result(p,&success);
    if (!success && out) { fclose(out); out=NULL; }
    return out;
}

/* Mutation owners are deliberately separate from captures: no pipe, terminal,
 * timeout or UI cleanup can terminate them. Callers reap without signalling. */
static bool native_detached_start(pid_t *pid, char *const argv[], int input_fd) {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int result;
    posix_spawn_file_actions_init(&actions); posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&attributes,0);
    if (input_fd>=0) {
        posix_spawn_file_actions_adddup2(&actions,input_fd,STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions,input_fd);
    } else posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    result=posix_spawnp(pid,argv[0],&actions,&attributes,argv,environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
    if (result) *pid=0;
    return result==0;
}
