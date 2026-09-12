/* Private observer session lifecycle; included after the measurement protocol. */
struct measure_loop {
    char line[2048];
    size_t used;
    bool ready;
};

static bool measure_read_command(struct measure_live *m, struct measure_loop *loop) {
    char c;
    if (read(STDIN_FILENO,&c,1)<=0) return false;
    if (c=='\n') {
        loop->line[loop->used]='\0'; loop->used=0;
        return measure_command(m,loop->line);
    }
    if (loop->used+1>=sizeof(loop->line)) return false;
    loop->line[loop->used++]=c;
    return true;
}

static void measure_check_deadlines(struct measure_live *m) {
    for (size_t i=0;i<m->expectation_count;i++) {
        struct measure_expectation *e=&m->expectations[i];
        if (e->armed && measure_now_ns()>=e->deadline_ns)
            measure_finish_expectation(m,e,false,"deadline");
    }
}

static bool measure_poll(struct measure_live *m, struct measure_loop *loop) {
    fd_set readers; struct timeval timeout={0,20000}; struct termios current;
    FD_ZERO(&readers); FD_SET(STDIN_FILENO,&readers); FD_SET(m->session.master,&readers);
    int selected=select(m->session.master+1,&readers,NULL,NULL,&timeout);
    if (selected<0 && errno!=EINTR) return false;
    if (selected>0) {
        if (FD_ISSET(m->session.master,&readers) && !measure_receive(m)) return false;
        if (FD_ISSET(STDIN_FILENO,&readers) && !measure_read_command(m,loop)) return false;
    }
    if (!loop->ready && tcgetattr(m->session.slave,&current)==0 && (current.c_lflag&(ICANON|ECHO))==0) {
        loop->ready=true;
        printf("{\"event\":\"raw-ready\",\"observed_ns\":%llu}\n",(unsigned long long)measure_now_ns());
    }
    measure_check_deadlines(m);
    return true;
}

static bool measure_wait_child(struct measure_live *m, int *status, int attempts) {
    for (int i=0;i<attempts;i++) {
        if (waitpid(m->session.pid,status,WNOHANG)==m->session.pid) return true;
        (void)measure_receive(m); sleep_ms(10);
    }
    return false;
}

static bool measure_stop_child(struct measure_live *m, int *status) {
    (void)kill(-m->session.pid,SIGTERM); (void)kill(m->session.pid,SIGTERM);
    if (measure_wait_child(m,status,50)) return true;
    (void)kill(-m->session.pid,SIGKILL); (void)kill(m->session.pid,SIGKILL);
    return measure_wait_child(m,status,100);
}

static int measure_exit_code(bool reaped, int status) {
    if (!reaped) return 255;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128+WTERMSIG(status);
    return 255;
}

static int measure_report_exit(struct measure_live *m, bool reaped, int status, bool forced) {
    struct termios current;
    bool restored=(tcgetattr(m->session.slave,&current)==0 || tcgetattr(m->session.master,&current)==0) &&
        same_terminal(&m->session.original,&current);
    int exit_code=measure_exit_code(reaped,status);
    printf("{\"event\":\"exit\",\"exit_code\":%d,\"reaped\":%s,\"forced\":%s,\"terminal_restored\":%s,"
           "\"pty_bytes\":%zu,\"raw_written\":%zu,\"raw_overflow\":%s}\n",exit_code,reaped ? "true" : "false",
           forced ? "true" : "false",restored ? "true" : "false",m->bytes,m->raw_written,m->overflow ? "true" : "false");
    int result_code=!reaped || exit_code || forced || !restored || m->overflow;
    close_session(&m->session);
    return result_code;
}

static int measure_close(struct measure_live *m, bool reaped, int status) {
    bool forced=false;
    int result_code=1;
    for (size_t i=0;i<m->expectation_count;i++)
        if (m->expectations[i].armed) measure_finish_expectation(m,&m->expectations[i],false,"observer-ended");
    if (m->session.pid>0 && !reaped) {
        forced=true; reaped=measure_stop_child(m,&status);
    }
    if (m->session.master>=0) result_code=measure_report_exit(m,reaped,status,forced);

    if (m->raw && fclose(m->raw)) result_code=1;
    free(m->screen);
    return result_code;
}

static bool measure_open(struct measure_live *m, const char *tui, const char *hydra,
                         unsigned columns, unsigned rows) {
    char path[4096];
    m->screen=measure_screen_create((int)columns,(int)rows);
    if (!m->screen || snprintf(path,sizeof(path),"%s/terminal.raw",m->directory)>=(int)sizeof(path)) return false;
    int raw_fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (raw_fd<0) return false;
    if (fcntl(raw_fd,F_SETFD,FD_CLOEXEC)<0) { close(raw_fd); return false; }
    m->raw=fdopen(raw_fd,"wb");
    if (!m->raw) { close(raw_fd); return false; }
    return !open_session(&m->session,tui,hydra,"/usr/bin",(unsigned short)columns,(unsigned short)rows) &&
        m->session.master<FD_SETSIZE;
}

static int measure_session(const char *tui,const char *hydra,const char *directory,
                           const char *columns_text,const char *rows_text) {
    struct measure_live m={0}; struct measure_loop loop={0}; unsigned columns,rows;
    int status=0; bool reaped=false; uint64_t started=measure_now_ns();
    if (!measure_unsigned(columns_text,&columns) || !measure_unsigned(rows_text,&rows) ||
        columns<20 || columns>MEASURE_COLUMNS || rows<6 || rows>MEASURE_ROWS) return 2;
    m.directory=directory; m.session.master=m.session.slave=-1; m.session.pid=-1;
    if (!measure_open(&m,tui,hydra,columns,rows)) return measure_close(&m,false,status);
    printf("{\"event\":\"spawn\",\"observer_pid\":%ld,\"tui_pid\":%ld,\"started_ns\":%llu}\n",
           (long)getpid(),(long)m.session.pid,(unsigned long long)started);
    while (measure_now_ns()-started<UINT64_C(180000000000)) {
        if (!measure_poll(&m,&loop)) break;
        pid_t waited=waitpid(m.session.pid,&status,WNOHANG);
        if (waited==m.session.pid) { reaped=true; break; }
        if (waited<0 && errno!=EINTR) break;
        if (m.quitting && measure_now_ns()>=m.quit_ns) break;
    }
    return measure_close(&m,reaped,status);
}
