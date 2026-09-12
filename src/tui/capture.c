#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include <limits.h>
#ifdef __APPLE__
#include <libproc.h>
#elif defined(__linux__)
#include <dirent.h>
#endif
/* Bounded read-only subprocess capture. The caller owns the stream and process;
 * step never waits for a child, so terminal I/O can continue during observations. */
#define CAPTURE_STOP_GRACE_MS 750L

/* Captures own a private session, including helpers with separate process
 * groups. Keep its leader unreaped until actual pipe EOF to reserve the ID.
 * Enumerate only during escalation; ordinary refresh does no process scan. */
static void kill_capture_session(pid_t session) {
#ifdef __APPLE__
    int size = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    pid_t *pids;
    if (size <= 0 || size > INT_MAX - 4096) return;
    size += 4096;
    pids = malloc((size_t)size);
    if (!pids) return;
    int used = proc_listpids(PROC_ALL_PIDS, 0, pids, size);
    for (size_t i = 0; used > 0 && i < (size_t)used / sizeof(*pids); i++) {
        if (pids[i] > 0 && getsid(pids[i]) == session) (void)kill(pids[i], SIGKILL);
    }
    free(pids);
#elif defined(__linux__)
    DIR *directory = opendir("/proc");
    struct dirent *entry;
    if (!directory) return;
    while ((entry = readdir(directory)) != NULL) {
        char *end; long pid;
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') continue;
        errno = 0; pid = strtol(entry->d_name, &end, 10);
        if (!errno && !*end && pid <= INT_MAX && getsid((pid_t)pid) == session)
            (void)kill((pid_t)pid, SIGKILL);
    }
    closedir(directory);
#endif
}

static long elapsed_ms(const struct timespec *started) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC,&now);
    return (long)(now.tv_sec-started->tv_sec)*1000L+(long)(now.tv_nsec-started->tv_nsec)/1000000L;
}

static void stop_capture(struct native_capture *p) {
    if (!p->stopping) {
        p->stopping=true; p->failed=true;
        (void)clock_gettime(CLOCK_MONOTONIC,&p->stop_started);
        /* The producer owns any nested process groups and temporary files. */
        (void)kill(-p->pid,SIGTERM); (void)kill(p->pid,SIGTERM);
    }
    if (elapsed_ms(&p->stop_started)>=CAPTURE_STOP_GRACE_MS) {
        kill_capture_session(p->pid);
        (void)kill(-p->pid,SIGKILL); (void)kill(p->pid,SIGKILL);
    }
}

void native_capture_destroy(struct native_capture *p) {
    if (p->pid>0 && !p->reaped) {
        const struct timespec pause={0,10000000L};
        stop_capture(p);
        while (!native_capture_step(p)) (void)nanosleep(&pause,NULL);
    }
    if (p->fd>=0) close(p->fd);
    if (p->output) fclose(p->output);
    memset(p,0,sizeof(*p)); p->fd=-1;
}

bool native_capture_start(struct native_capture *p, char *const argv[], long budget_ms) {
    int pipes[2];
    memset(p,0,sizeof(*p)); p->fd=-1; p->budget_ms=budget_ms;
    p->output=tmpfile();
    if (!p->output) return false;
    if (fcntl(fileno(p->output),F_SETFD,FD_CLOEXEC)<0 || pipe(pipes)<0) goto fail;
    p->fd=pipes[0];
    if (fcntl(p->fd,F_SETFD,FD_CLOEXEC)<0 || fcntl(pipes[1],F_SETFD,FD_CLOEXEC)<0 ||
        fcntl(p->fd,F_SETFL,O_NONBLOCK)<0) { close(pipes[1]); goto fail; }
    p->pid=fork();
    if (p->pid==0) {
        int null_fd;
        if (setsid()<0 || (null_fd=open("/dev/null",O_RDWR))<0) _exit(126);
        if (dup2(pipes[1],STDOUT_FILENO)<0 || dup2(null_fd,STDIN_FILENO)<0 ||
            dup2(null_fd,STDERR_FILENO)<0) _exit(126);
        close(null_fd); close(pipes[0]); close(pipes[1]);
        execvp(argv[0],argv); _exit(127);
    }
    close(pipes[1]);
    if (p->pid<0) { p->pid=0; goto fail; }
    (void)clock_gettime(CLOCK_MONOTONIC,&p->started);
    return true;
fail:
    native_capture_destroy(p); return false;
}

static void drain_capture(struct native_capture *p) {
    for (size_t chunks=0; chunks<32; chunks++) {
        char bytes[8192];
        ssize_t n=read(p->fd,bytes,sizeof(bytes));
        if (n<=0) {
            if (!n) p->eof=true;
            else if (errno!=EAGAIN && errno!=EINTR) p->failed=true;
            return;
        }
        /* Drain discarded output while a cancelled producer unwinds. */
        if (p->failed) continue;
        if ((size_t)n>MAX_DATA_BYTES-p->bytes || fwrite(bytes,1,(size_t)n,p->output)!=(size_t)n) p->failed=true;
        else p->bytes+=(size_t)n;
    }
}

bool native_capture_step(struct native_capture *p) {
    if (!p->pid) return true;
    if (elapsed_ms(&p->started)>=p->budget_ms || terminal_stopped()) { p->timed_out=true; p->failed=true; }
    if (!p->eof) drain_capture(p);
    if (p->failed && !p->reaped) stop_capture(p);
    /* Retain the leader until EOF so its process-group ID cannot be reused. */
    if (!p->reaped && p->eof) {
        pid_t result=waitpid(p->pid,&p->status,WNOHANG);
        if (result==p->pid) p->reaped=true;
        else if (result<0 && errno!=EINTR) { p->failed=true; p->reaped=true; }
    }
    return p->reaped && p->eof;
}

/* Transfer complete stdout even on a normal nonzero exit, for compiler
 * diagnostics. Timeout/transport failures never become complete documents. */
FILE *native_capture_result(struct native_capture *p, bool *success) {
    FILE *out=NULL;
    *success=p->reaped && WIFEXITED(p->status) && WEXITSTATUS(p->status)==0 && !p->failed;
    if (p->reaped && p->eof && WIFEXITED(p->status) && !p->failed && fflush(p->output)==0 && fseek(p->output,0,SEEK_SET)==0) {
        out=p->output; p->output=NULL;
    }
    native_capture_destroy(p);
    return out;
}
FILE *native_capture_take(struct native_capture *p) {
    bool success;
    FILE *out=native_capture_result(p,&success);
    if (!success && out) { fclose(out); out=NULL; }
    return out;
}

/* Mutation owners are deliberately separate from captures: no pipe, terminal,
 * timeout or UI cleanup can terminate them. Callers reap without signalling. */
bool native_detached_start(pid_t *pid, char *const argv[], int input_fd) {
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
