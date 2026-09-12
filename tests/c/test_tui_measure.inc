/* Item10 observer for test_tui_pty.c. No product rendering changes.
 * This observes applied ASCII semantic regions, never whole frames or pixels. */
#include "termviz/terminal.h"

#define MEASURE_COLUMNS 160
#define MEASURE_ROWS 48
#define MEASURE_CELLS ((size_t)MEASURE_COLUMNS * MEASURE_ROWS)

struct measure_region {
    int x, y, width;
    const char *text;
};
struct measure_screen {
    struct tv_terminal_model terminal;
    struct tv_cell primary[MEASURE_CELLS], alternate[MEASURE_CELLS];
};

/* Caller owns model storage; regions borrow their ASCII expected strings. */
static bool measure_region_matches(const struct measure_screen *screen,
                                   const struct measure_region *region) {
    const struct tv_terminal_model *t = &screen->terminal;
    const struct tv_canvas *c = t->alternate_active ? &t->alternate.canvas : &t->primary.canvas;
    size_t length;
    if (!region->text || region->x < 0 || region->y < 0 || region->width < 1 ||
        region->y >= c->height || region->x >= c->width || region->width > c->width-region->x)
        return false;
    length = strlen(region->text);
    if (length > (size_t)region->width) return false;
    for (int i = 0; i < region->width; i++) {
        const struct tv_cell *cell = &c->cells[(size_t)region->y*(size_t)c->stride+(size_t)(region->x+i)];
        uint32_t expected = (size_t)i < length ? (unsigned char)region->text[i] : ' ';
        if (expected < 32U || expected > 126U || cell->glyph != expected || cell->width != 1 || cell->combining_count)
            return false;
    }
    return true;
}

static bool measure_semantic_matches(const struct measure_screen *screen,
                                     const struct measure_region *identity,
                                     const struct measure_region *response) {
    /* GROUND=0 is the existing terminal parser's completed-sequence state.
     * Bytes after this boundary may still paint unrelated regions. */
    return screen->terminal.parser_state == 0 && screen->terminal.utf8_length == 0 &&
           measure_region_matches(screen, identity) && measure_region_matches(screen, response);
}

/* Caller frees the returned model. No history is needed for visible regions. */
static struct measure_screen *measure_screen_create(int columns, int rows) {
    struct measure_screen *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (!tv_term_init(&s->terminal, s->primary, s->alternate, MEASURE_COLUMNS, MEASURE_ROWS,
                      NULL, 0, columns, rows)) {
        free(s); return NULL;
    }
    return s;
}

/* Encode the actual termviz presenter into caller-owned bounded output. */
static size_t measure_present_bytes(struct tv_presenter *p, const struct tv_canvas *c,
                                    char *out, size_t capacity) {
    FILE *stream = tmpfile();
    size_t length = 0;
    long size;
    if (!stream) return 0;
    if (!tv_present(p, c, stream, NULL, NULL) || (size = ftell(stream)) <= 0 ||
        (unsigned long)size >= capacity || fseek(stream, 0, SEEK_SET)) goto done;
    length = fread(out, 1, (size_t)size, stream);
    if (length != (size_t)size) length = 0;
done:
    fclose(stream);
    return length;
}

static void measure_control_check(bool condition, bool *valid, int *checks, int line) {
    if (!condition) {
        fprintf(stderr,"semantic control failed at line %d\n",line);
        *valid=false;
    }
    (*checks)++;
}

static int measure_semantic_controls(void) {
    struct measure_screen *s = NULL;
    struct tv_cell *cells = NULL, *previous = NULL;
    struct tv_canvas canvas;
    struct tv_presenter presenter;
    const struct measure_region identity = {2,1,28,"HEAD head_104729_001"};
    const struct measure_region response = {3,5,34,"TAG-head_104729_001-v2"};
    struct measure_region wrong_identity = identity, wrong_response = response;
    char full[16384], delta[16384];
    const char title[] = "\033]0;HEAD head_104729_001 TAG-head_104729_001-v2\007";
    size_t length, delta_length, splits = 0;
    int status = 1, checks = 0;
    bool valid=true;
#define REGION_CHECK(condition) measure_control_check((condition), &valid, &checks, __LINE__)
    s = measure_screen_create(80,16);
    cells = calloc(80U*16U, sizeof(*cells));
    previous = calloc(80U*16U, sizeof(*previous));
    if (!s || !cells || !previous) goto cleanup;
    if (!tv_init(&canvas,cells,80U*16U,80,16,true)) goto cleanup;
    if (!tv_present_init(&presenter,previous,80U*16U)) goto cleanup;
    checks+=3;
    tv_text(&canvas,(struct tv_rect){identity.x,identity.y,identity.width,1},identity.text,TV_BASE);
    tv_text(&canvas,(struct tv_rect){response.x,response.y,response.width,1},response.text,TV_BASE);
    length = measure_present_bytes(&presenter,&canvas,full,sizeof(full));
    REGION_CHECK(length > 0);
    for (size_t split = 0; split <= length; split++) {
        tv_term_reset(&s->terminal);
        tv_term_feed(&s->terminal,full,split);
        tv_term_feed(&s->terminal,full+split,length-split);
        REGION_CHECK(measure_semantic_matches(s,&identity,&response));
        splits++;
    }
    wrong_identity.text = "HEAD head_104729_002";
    wrong_response.text = "TAG-head_104729_002-v2";
    REGION_CHECK(!measure_semantic_matches(s,&wrong_identity,&response));
    REGION_CHECK(!measure_semantic_matches(s,&identity,&wrong_response));
    wrong_response = response; wrong_response.y++;
    REGION_CHECK(!measure_semantic_matches(s,&identity,&wrong_response));
    wrong_response = response; wrong_response.width--;
    wrong_response.x = 159;
    REGION_CHECK(!measure_semantic_matches(s,&identity,&wrong_response));
    tv_term_feed(&s->terminal,"\033[",2);
    REGION_CHECK(!measure_semantic_matches(s,&identity,&response));
    tv_term_feed(&s->terminal,"0m",2);
    REGION_CHECK(measure_semantic_matches(s,&identity,&response));
    tv_term_feed(&s->terminal,"\xf0\x9f",2);
    REGION_CHECK(!measure_semantic_matches(s,&identity,&response));
    tv_term_reset(&s->terminal);
    tv_term_feed(&s->terminal,title,sizeof(title)-1);
    REGION_CHECK(!measure_semantic_matches(s,&identity,&response));
    tv_term_reset(&s->terminal);
    tv_term_feed(&s->terminal,full,length);
    tv_text(&canvas,(struct tv_rect){response.x,response.y,response.width,1},"TAG-head_104729_001-v3",TV_BASE);
    delta_length = measure_present_bytes(&presenter,&canvas,delta,sizeof(delta));
    REGION_CHECK(delta_length > 0 && delta_length < length);
    tv_term_feed(&s->terminal,delta,delta_length);
    REGION_CHECK(!measure_semantic_matches(s,&identity,&response));
    wrong_response = response; wrong_response.text = "TAG-head_104729_001-v3";
    REGION_CHECK(measure_semantic_matches(s,&identity,&wrong_response));
    REGION_CHECK(tv_term_resize(&s->terminal,120,24));
    REGION_CHECK(measure_semantic_matches(s,&identity,&wrong_response));
    REGION_CHECK(tv_term_resize(&s->terminal,15,4));
    REGION_CHECK(!measure_semantic_matches(s,&identity,&wrong_response));
    printf("{\"status\":\"%s\",\"checks\":%d,\"fragment_splits\":%zu,\"presenter_bytes\":%zu,"
           "\"incremental_presenter_bytes\":%zu,\"boundary\":\"applied-semantic-region-at-complete-ANSI-sequence\","
           "\"whole_frame_completion\":null,\"visible_pixels\":null}\n",valid ? "passed" : "failed",checks,splits,length,delta_length);
    status = valid ? 0 : 1;
cleanup:
    free(previous); free(cells); free(s);
#undef REGION_CHECK
    return status;
}

#define MEASURE_EXPECTATIONS 32
struct measure_expectation {
    uint64_t input_ns, deadline_ns;
    bool armed, pending_late;
    char action[64], identity_text[256], response_text[256];
    struct measure_region identity, response;
};

struct measure_live {
    struct session session;
    struct measure_screen *screen;
    FILE *raw;
    const char *directory;
    size_t bytes, raw_written, expectation_count;
    unsigned long sequence;
    uint64_t last_read_ns, applied_ns, quit_ns;
    bool quitting, overflow;
    struct measure_expectation expectations[MEASURE_EXPECTATIONS];
};

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

static uint64_t measure_now_ns(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t scale;
    uint64_t ticks=mach_absolute_time();
    if (!scale.denom && mach_timebase_info(&scale)!=KERN_SUCCESS) return 0;
    return (ticks/scale.denom)*scale.numer+(ticks%scale.denom)*scale.numer/scale.denom;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC,&value)) return 0;
    return (uint64_t)value.tv_sec*UINT64_C(1000000000)+(uint64_t)value.tv_nsec;
#endif
}

static bool measure_name(const char *text) {
    size_t n = text ? strlen(text) : 0;
    if (!n || n >= 64) return false;
    for (size_t i=0;i<n;i++) if (!(text[i]>='a' && text[i]<='z') &&
        !(text[i]>='A' && text[i]<='Z') && !(text[i]>='0' && text[i]<='9') && text[i]!='-') return false;
    return true;
}

static int measure_hex_digit(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    return -1;
}

/* Caller supplies output storage. A trailing NUL is for expected ASCII text. */
static bool measure_decode(const char *hex, char *out, size_t capacity, size_t *length) {
    size_t n = strlen(hex);
    if (!n || n%2 || n/2 >= capacity) return false;
    for (size_t i=0;i<n/2;i++) {
        int a=measure_hex_digit(hex[i*2]),b=measure_hex_digit(hex[i*2+1]);
        if (a<0 || b<0) return false;
        out[i]=(char)(a*16+b);
    }
    *length=n/2; out[*length]='\0'; return true;
}

static size_t measure_write(int fd, const char *bytes, size_t length) {
    size_t sent=0;
    uint64_t stop=measure_now_ns()+UINT64_C(1000000000);
    while (sent<length && measure_now_ns()<stop) {
        ssize_t n=write(fd,bytes+sent,length-sent);
        if (n>0) sent+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) sleep_ms(1);
        else break;
    }
    return sent;
}

static void measure_finish_expectation(struct measure_live *m, struct measure_expectation *e, bool found, const char *reason) {
    printf("{\"event\":\"%s\",\"id\":\"%s\",\"input_ns\":%llu,\"observed_ns\":%llu,"
           "\"checked_ns\":%llu,\"deadline_ns\":%llu,\"pty_sequence\":%lu,\"pty_bytes\":%zu,"
           "\"whole_frame_completion\":null,\"pixels\":null,\"reason\":\"%s\"}\n",found ? "observed" : "miss",e->action,
           (unsigned long long)e->input_ns,(unsigned long long)(found ? m->applied_ns : 0),
           (unsigned long long)measure_now_ns(),(unsigned long long)e->deadline_ns,m->sequence,m->bytes,reason);
    e->armed=false;
    e->pending_late=!found && e->input_ns!=0;
}

static int measure_ascii(const struct tv_cell *cell) {
    if (cell->width==0) return ' ';
    return cell->glyph>=32 && cell->glyph<=126 ? (int)cell->glyph : '?';
}

static bool measure_write_canvas(const struct tv_canvas *c, FILE *file) {
    bool ok=true;
    /* One ASCII character per cell preserves coordinates; styles are excluded. */
    for (int y=0;y<c->height;y++) {
        for (int x=0;x<c->width;x++) {
            const struct tv_cell *cell=&c->cells[(size_t)y*(size_t)c->stride+(size_t)x];
            int value=measure_ascii(cell);
            if (fputc(value,file)==EOF) ok=false;
        }
        if (fputc('\n',file)==EOF) ok=false;
    }
    return ok;
}

static bool measure_snapshot(struct measure_live *m, const char *id) {
    const struct tv_canvas *c=m->screen->terminal.alternate_active ?
        &m->screen->terminal.alternate.canvas : &m->screen->terminal.primary.canvas;
    char path[4096]; FILE *file; int fd; bool ok=true;
    if (snprintf(path,sizeof(path),"%s/snapshot-%s.txt",m->directory,id)>=(int)sizeof(path)) return false;
    fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd<0) return false;
    file=fdopen(fd,"w");
    if (!file) { close(fd); return false; }
    ok=measure_write_canvas(c,file);
    if (fclose(file)) ok=false;
    printf("{\"event\":\"snapshot\",\"id\":\"%s\",\"observed_ns\":%llu,\"checked_ns\":%llu,"
           "\"sequence\":%lu,\"parser_complete\":%s,\"unsupported\":%llu,\"ok\":%s}\n",
           id,(unsigned long long)m->last_read_ns,(unsigned long long)measure_now_ns(),m->sequence,
           !m->screen->terminal.parser_state && !m->screen->terminal.utf8_length ? "true" : "false",
           (unsigned long long)m->screen->terminal.unsupported,ok ? "true" : "false");
    return ok;
}

static bool measure_arm(struct measure_live *m, char **part, size_t count) {
    unsigned values[7]; size_t length; struct measure_expectation *e;
    const size_t indices[]={2,3,4,6,7,8,10};
    if (count!=11 || m->expectation_count>=MEASURE_EXPECTATIONS) return false;
    for (size_t i=0;i<m->expectation_count;i++)
        if (!strcmp(m->expectations[i].action,part[1])) return false;
    e=&m->expectations[m->expectation_count++];
    for (size_t i=0;i<7;i++) if (!measure_unsigned(part[indices[i]],&values[i])) return false;
    if (!values[6] || values[6]>15000U || !values[2] || !values[5] ||
        !measure_decode(part[5],e->identity_text,sizeof(e->identity_text),&length) ||
        strlen(e->identity_text)!=length ||
        !measure_decode(part[9],e->response_text,sizeof(e->response_text),&length) ||
        strlen(e->response_text)!=length) return false;
    e->identity=(struct measure_region){(int)values[0],(int)values[1],(int)values[2],e->identity_text};
    e->response=(struct measure_region){(int)values[3],(int)values[4],(int)values[5],e->response_text};
    (void)snprintf(e->action,sizeof(e->action),"%s",part[1]);
    e->input_ns=0; e->deadline_ns=measure_now_ns()+(uint64_t)values[6]*UINT64_C(1000000);
    e->pending_late=false;
    e->armed=!measure_semantic_matches(m->screen,&e->identity,&e->response);
    printf("{\"event\":\"%s\",\"id\":\"%s\",\"predicate_before_arm\":%s,\"deadline_ns\":%llu}\n",
           e->armed ? "armed" : "invalid-expectation",e->action,e->armed ? "false" : "true",
           (unsigned long long)e->deadline_ns);
    return true;
}

static bool measure_input(struct measure_live *m, struct measure_expectation *e, char **part) {
    char bytes[1024]; size_t length,sent; uint64_t started;
    if (!measure_decode(part[2],bytes,sizeof(bytes),&length)) return false;
    if (e && e->armed && measure_semantic_matches(m->screen,&e->identity,&e->response)) {
        e->armed=false;
        printf("{\"event\":\"invalid-expectation\",\"id\":\"%s\",\"predicate_before_input\":true}\n",e->action);
    }
    started=measure_now_ns(); sent=measure_write(m->session.master,bytes,length);
    if (e && e->armed) e->input_ns=started;
    printf("{\"event\":\"input\",\"id\":\"%s\",\"sent_ns\":%llu,\"send_end_ns\":%llu,"
           "\"requested\":%zu,\"sent\":%zu}\n",part[1],(unsigned long long)started,
           (unsigned long long)measure_now_ns(),length,sent);
    if (!strcmp(part[0],"Q")) { m->quitting=true; m->quit_ns=measure_now_ns()+UINT64_C(3000000000); }
    return sent==length;
}

static bool measure_resize(struct measure_live *m, struct measure_expectation *e, char **part) {
    unsigned columns,rows; struct winsize size; uint64_t started=measure_now_ns();
    if (!measure_unsigned(part[2],&columns) || !measure_unsigned(part[3],&rows) ||
        columns<20 || columns>MEASURE_COLUMNS || rows<6 || rows>MEASURE_ROWS) return false;
    memset(&size,0,sizeof(size));size.ws_col=(unsigned short)columns;size.ws_row=(unsigned short)rows;
    if (e && e->armed && measure_semantic_matches(m->screen,&e->identity,&e->response)) {
        e->armed=false;
        printf("{\"event\":\"invalid-expectation\",\"id\":\"%s\",\"predicate_before_input\":true}\n",e->action);
    }
    if (ioctl(m->session.master,TIOCSWINSZ,&size) || !tv_term_resize(&m->screen->terminal,(int)columns,(int)rows)) return false;
    if (e && e->armed) e->input_ns=started;
    printf("{\"event\":\"resize\",\"id\":\"%s\",\"sent_ns\":%llu,\"columns\":%u,\"rows\":%u}\n",
           part[1],(unsigned long long)started,columns,rows);
    return true;
}

static struct measure_expectation *measure_find_expectation(struct measure_live *m, const char *id) {
    for (size_t i=0;i<m->expectation_count;i++)
        if (!strcmp(m->expectations[i].action,id)) return &m->expectations[i];
    return NULL;
}

/* Private measurement protocol; strings and input bytes are hex encoded. */
static bool measure_command(struct measure_live *m, char *line) {
    char *part[12],*save=NULL,*word; size_t count=0; struct measure_expectation *e=NULL;
    for (word=strtok_r(line," \r",&save);word && count<12;word=strtok_r(NULL," \r",&save)) part[count++]=word;
    if (word || count<2 || !measure_name(part[1])) return false;
    e=measure_find_expectation(m,part[1]);
    if (!strcmp(part[0],"E")) return measure_arm(m,part,count);
    if (!strcmp(part[0],"C") && count==2) {
        printf("{\"event\":\"clock-sync\",\"id\":\"%s\",\"observer_ns\":%llu}\n",part[1],
               (unsigned long long)measure_now_ns());return true;
    }
    if (!strcmp(part[0],"S") && count==2) return measure_snapshot(m,part[1]);
    if ((!strcmp(part[0],"I") || !strcmp(part[0],"Q")) && count==3) return measure_input(m,e,part);
    if (!strcmp(part[0],"R") && count==4) return measure_resize(m,e,part);
    return false;
}

static void measure_observe(struct measure_live *m, struct measure_expectation *e) {
    if ((e->armed || e->pending_late) && e->input_ns && m->applied_ns>=e->input_ns &&
        measure_semantic_matches(m->screen,&e->identity,&e->response)) {
        m->applied_ns=measure_now_ns();
        if (e->armed) measure_finish_expectation(m,e,m->applied_ns<e->deadline_ns,
            m->applied_ns<e->deadline_ns ? "predicate" : "deadline");
        if (e->pending_late) {
            printf("{\"event\":\"late-observed\",\"id\":\"%s\",\"observed_ns\":%llu,\"deadline_ns\":%llu}\n",
                   e->action,(unsigned long long)m->applied_ns,(unsigned long long)e->deadline_ns);
            e->pending_late=false;
        }
    }
}

static bool measure_receive(struct measure_live *m) {
    char bytes[8192]; ssize_t length=read(m->session.master,bytes,sizeof(bytes));
    const size_t cap=32U*1024U*1024U;
    if (length<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK || errno==EIO)) return true;
    if (length<=0) return length==0;
    m->last_read_ns=measure_now_ns();m->bytes+=(size_t)length;m->sequence++;
    if ((size_t)length<=cap-m->raw_written) {
        if (fwrite(bytes,1,(size_t)length,m->raw)!=(size_t)length) return false;
        m->raw_written+=(size_t)length;
    } else m->overflow=true;
    tv_term_feed(&m->screen->terminal,bytes,(size_t)length);
    m->applied_ns=measure_now_ns();
    for (size_t i=0;i<m->expectation_count;i++) measure_observe(m,&m->expectations[i]);
    return true;
}

#include "test_tui_measure_session.inc"
