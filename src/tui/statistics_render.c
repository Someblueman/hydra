#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Statistics are a view over recorded evidence, never a telemetry collector. */
static const char *statistics_range(const struct statistics_view *v) {
    return v->filter.days == 1 ? "24 hours" : v->filter.days == 7 ? "7 days" : "all recorded";
}

static void statistics_time(uint64_t epoch, char *out, size_t capacity) {
    time_t value = (time_t)epoch;
    struct tm utc;
    if (!epoch || !gmtime_r(&value, &utc) || !strftime(out, capacity, "%m-%d %H:%M UTC", &utc))
        copy_text(out, capacity, "unknown date");
}

static void statistics_hit(struct app *app, struct tv_rect r, size_t item) {
    size_t n = app->hit_count;
    if (n == MAX_HEADS) return;
    app->hit_rows[n] = r.y + 1; app->hit_bottom[n] = r.y + r.height;
    app->hit_left[n] = r.x + 1; app->hit_right[n] = r.x + r.width;
    app->hit_items[n] = item; app->hit_count++;
}

static void statistics_distribution(struct tv_canvas *c, struct tv_rect r, const struct hs_summary *s) {
    static const char *names[] = {"Succeeded", "Failed", "Running", "Queued", "Blocked", "Cancelled", "Unknown"};
    size_t i;
    tv_panel(c, r, "RUN OUTCOMES / recorded");
    for (i = 0; i < HS_STATES && (int)i < r.height - 3; i++) {
        int row = r.y + 2 + (int)i, bar = r.width - 22;
        enum tv_style tone = i == HS_FAILED || i == HS_BLOCKED || i == HS_UNKNOWN ? TV_WARNING : TV_BASE;
        dashboard_text(c, r.x+2, row, r.width-4, tone, "%-10s %3zu", names[i], s->run_states[i]);
        if (bar > 1) tv_bar(c, (struct tv_rect){r.x+19,row,bar,1}, s->run_states[i], s->runs, tone);
    }
}

static void statistics_trend(struct tv_canvas *c, struct tv_rect r, const struct hs_summary *s) {
    size_t i, maximum = 1;
    tv_panel(c, r, "RUN CREATION / 7 x 24h");
    for (i = 0; i < 7; i++) if (s->daily[i] > maximum) maximum = s->daily[i];
    for (i = 0; i < 7 && (int)i < r.height - 3; i++) {
        int row = r.y + 2 + (int)i;
        dashboard_text(c, r.x+2, row, r.width-4, TV_BORDER, "-%zud %3zu", 6-i, s->daily[i]);
        tv_bar(c, (struct tv_rect){r.x+12,row,r.width-15,1}, s->daily[i], maximum, TV_BORDER);
    }
}

static void statistics_run_table(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    struct statistics_view *v = app->statistics;
    size_t i, start;
    int available = r.height - 4, row = r.y + 2;
    tv_panel(c, r, "RUNS / newest creation first");
    if (!v->count) {
        dashboard_text(c, r.x+2, row, r.width-4, TV_WARNING, "No matching recorded runs");
        dashboard_text(c, r.x+2, row+1, r.width-4, TV_BORDER, "0 reset filters / / search"); return;
    }
    if (available < 1) return;
    start = v->selected >= (size_t)available ? v->selected - (size_t)available + 1 : 0;
    for (i = start; i < v->count && row < r.y+r.height-2; i++, row++) {
        const struct hs_run *run = &v->model->runs[v->visible[i]];
        char date[40];
        int name_width = r.width > 70 ? r.width-47 : r.width-25;
        enum tv_style tone = i == v->selected ? TV_SELECTED : hs_state(run->state) == HS_FAILED ? TV_WARNING : TV_BASE;
        statistics_time(run->created, date, sizeof(date));
        dashboard_text(c, r.x+2, row, r.width-4, tone, "%c %-*.*s %-18.18s%s%s",
            i == v->selected ? '>' : ' ', name_width, name_width, run->name, run->state, r.width > 70 ? "  " : "", r.width > 70 ? date : "");
        statistics_hit(app, (struct tv_rect){r.x+1,row,r.width-2,1}, i);
    }
    dashboard_text(c, r.x+2, r.y+r.height-2, r.width-4, TV_BORDER, "%zu/%zu / Enter evidence / g graph", v->selected+1, v->count);
}

static void statistics_run_detail(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    struct statistics_view *v = app->statistics;
    const struct hs_run *run;
    size_t i, ordinal = 0, run_index;
    int row = r.y + 5;
    tv_panel(c, r, "RUN EVIDENCE / latest attempts");
    if (!v->count) { dashboard_text(c,r.x+2,r.y+2,r.width-4,TV_WARNING,"Selected run no longer matches"); return; }
    run_index = v->visible[v->selected]; run = &v->model->runs[run_index];
    dashboard_text(c,r.x+2,r.y+1,r.width-4,TV_STRONG,"%s / %s",run->name,run->state);
    dashboard_text(c,r.x+2,r.y+2,r.width-4,TV_BORDER,"%s",run->id);
    dashboard_text(c,r.x+2,r.y+3,r.width-4,TV_BORDER,"STEP                 STATE           TRIES  SECONDS");
    for (i = 0; i < v->model->step_count && row < r.y+r.height-2; i++) {
        const struct hs_step *s = &v->model->steps[i];
        char seconds[24] = "--", attempts[24] = "--";
        uint64_t duration;
        if (s->run != run_index || ordinal++ < v->step_scroll) continue;
        if (hs_duration(v->model,s,&duration)) snprintf(seconds,sizeof(seconds),"%llu",(unsigned long long)duration);
        if (s->attempts_known) snprintf(attempts,sizeof(attempts),"%u",s->attempts);
        dashboard_text(c,r.x+2,row++,r.width-4,hs_state(s->state)==HS_FAILED ? TV_WARNING : TV_BASE,
            "%-20.20s %-15.15s %5s %8s",s->id,s->state,attempts,seconds);
    }
    if (row == r.y+5) dashboard_text(c,r.x+2,row,r.width-4,TV_WARNING,"No step evidence at this scroll position");
    dashboard_text(c,r.x+2,r.y+r.height-2,r.width-4,TV_BORDER,"j/k scroll / g graph / Esc statistics");
}

static void statistics_fleet(struct app *app, struct tv_canvas *c, struct tv_rect r) {
    struct statistics_view *v = app->statistics;
    size_t i, responded = 0, failed = 0, heads = 0;
    char reported[24]="--";
    int row = r.y+5;
    size_t start = 0;
    tv_panel(c,r,"HOST STATISTICS / latest fleet response");
    for (i=0;i<v->count;i++) {
        const struct host_observation *h = &app->model.hosts[v->visible[i]];
        if (!strcmp(h->state,"failed")) failed++; else { responded++; heads+=h->heads; }
    }
    if(responded) snprintf(reported,sizeof(reported),"%zu",heads);
    if (r.height < 10 || r.width < 65) {
        dashboard_text(c,r.x+1,r.y,r.width-2,TV_STRONG,"%zu responded / %zu failed",responded,failed);
        dashboard_text(c,r.x+1,r.y+1,r.width-2,TV_BASE,"%s reported heads",reported);
        if (v->count) {
            const struct host_observation *h=&app->model.hosts[v->visible[v->selected]];
            dashboard_text(c,r.x+1,r.y+2,r.width-2,TV_SELECTED,"%zu/%zu %s",v->selected+1,v->count,h->name);
            dashboard_text(c,r.x+1,r.y+3,r.width-2,TV_BASE,"%s / Enter evidence",h->state);
            if (v->detail) {
                size_t ordinal=0;
                bool shown=false;
                for (i=0;i<app->model.head_count;i++) {
                    const struct head *head=&app->model.heads[i];
                    if (strcmp(head->remote_host,h->name) || ordinal++<v->step_scroll) continue;
                    dashboard_text(c,r.x+1,r.y+3,r.width-2,TV_BASE,"%s / %s",head->branch,head->desired);
                    shown=true; break;
                }
                if (!shown) dashboard_text(c,r.x+1,r.y+3,r.width-2,TV_WARNING,"No head evidence at this offset");
            }
        } else dashboard_text(c,r.x+1,r.y+2,r.width-2,TV_WARNING,"No matching hosts / 0 reset");
        return;
    }
    dashboard_text(c,r.x+2,r.y+2,r.width-4,TV_STRONG,"%zu responded   %zu failed   %s reported heads",responded,failed,reported);
    if (r.height > 5) dashboard_text(c,r.x+2,r.y+3,r.width-4,TV_WARNING,"Failed hosts have unknown head counts");
    if (v->detail && v->count) {
        const char *host = app->model.hosts[v->visible[v->selected]].name;
        size_t ordinal = 0;
        for (i=0;i<app->model.head_count && row<r.y+r.height-3;i++) {
            const struct head *h = &app->model.heads[i];
            if (strcmp(h->remote_host,host) || ordinal++<v->step_scroll) continue;
            dashboard_text(c,r.x+2,row++,r.width-4,TV_BASE,"%s / %s / %s",h->remote_project,h->branch,h->desired);
        }
        if (!ordinal) dashboard_text(c,r.x+2,row++,r.width-4,TV_WARNING,"No head rows for %s / inspect host state",host);
    } else {
        size_t available=(size_t)(r.height-8);
        if (v->selected >= available) start=v->selected-available+1;
        for (i=start;i<v->count && row<r.y+r.height-3;i++,row++) {
            const struct host_observation *h = &app->model.hosts[v->visible[i]];
            char count[24]="--";
            if (strcmp(h->state,"failed")) snprintf(count,sizeof(count),"%u",h->heads);
            dashboard_text(c,r.x+2,row,r.width-4,i==v->selected ? TV_SELECTED : TV_BASE,
                "%c %-20.20s %-10s %s heads",i==v->selected ? '>' : ' ',h->name,h->state,count);
            statistics_hit(app,(struct tv_rect){r.x+1,row,r.width-2,1},i);
        }
    }
    dashboard_text(c,r.x+2,r.y+r.height-2,r.width-4,TV_WARNING,"Remote workflow history / CPU / memory / cost: unavailable");
}

bool render_statistics(struct app *app, unsigned frame, bool headless) {
    struct tv_canvas c;
    struct native_workspace *w;
    struct statistics_view *v;
    struct hs_summary summary;
    int width=app->cols>512 ? 511 : app->cols-1, height=app->rows>256 ? 256 : app->rows;
    int y, left, body, cards, chart_height;
    char updated[40]="unavailable", duration[40]="--", maximum[40]="--", retries[32]="--";
    if (width<19 || height<6 || !native_workspace_init(app) || !statistics_init(app)) return false;
    w=app->workspace; v=app->statistics;
    app->paint=!headless && !app->no_color;
    if (w->theme!=app->theme) { tv_present_invalidate(&w->presenter); w->theme=app->theme; }
    (void)tv_init(&c,w->cells,WORKSPACE_CAPACITY,width,height,!app->ascii);
    statistics_visible(app);
    memset(&summary,0,sizeof(summary));
    if (v->model) { hs_summarize(v->model,&v->filter,&summary); statistics_time(v->model->observed,updated,sizeof(updated)); }
    if (summary.attempts_known || !summary.steps) snprintf(retries,sizeof(retries),"%llu",(unsigned long long)summary.retries);
    if (summary.duration_known) snprintf(maximum,sizeof(maximum),"%llus",(unsigned long long)summary.duration_max);
    dashboard_text(&c,1,0,width-2,TV_TITLE,"HYDRA / D STATISTICS%s",(v->stale || app->snapshot_stale) ? " / STALE" : "");
    dashboard_text(&c,1,1,width-2,TV_BORDER,"%s / %s / %s%s%s%s",app->fleet ? "Fleet snapshot" : "Local project",app->fleet ? "latest response" : statistics_range(v),
        v->filter.query[0] ? v->filter.query : "all work", v->filter.attention ? " / attention" : "",
        v->filter.workflow[0] ? " / workflow: " : "",v->filter.workflow);
    if (app->fleet) statistics_fleet(app,&c,(struct tv_rect){0,2,width,height-4});
    else if (!v->model || (!v->model->run_count && v->model->warnings)) {
        dashboard_text(&c,2,4,width-4,TV_WARNING,"Statistics unavailable");
        if (height>10) dashboard_text(&c,2,6,width-4,TV_BASE,"No valid workflow statistics sample received. r retries.");
    } else if (height<18 || width<65) {
        dashboard_text(&c,1,2,width-2,TV_STRONG,"%zu runs / %zu steps / %s retries",summary.runs,summary.steps,retries);
        dashboard_text(&c,1,3,width-2,TV_BASE,"Run OK %zu / fail %zu / unknown %zu",summary.run_states[HS_SUCCEEDED],summary.run_states[HS_FAILED],summary.run_states[HS_UNKNOWN]);
        if (v->count) {
            const struct hs_run *run=&v->model->runs[v->visible[v->selected]];
            dashboard_text(&c,1,4,width-2,TV_SELECTED,"%zu/%zu %s",v->selected+1,v->count,run->name);
            dashboard_text(&c,1,5,width-2,TV_BASE,"%s / Enter evidence",run->state);
            if (v->detail) {
                size_t i, ordinal=0;
                bool shown=false;
                for (i=0;i<v->model->step_count;i++) {
                    const struct hs_step *s=&v->model->steps[i];
                    uint64_t seconds;
                    if (s->run!=v->visible[v->selected] || ordinal++<v->step_scroll) continue;
                    if (hs_duration(v->model,s,&seconds)) snprintf(duration,sizeof(duration),"%llus",(unsigned long long)seconds);
                    dashboard_text(&c,1,5,width-2,TV_BASE,"%s / %s / %s",s->id,s->state,duration); shown=true; break;
                }
                if(!shown) dashboard_text(&c,1,5,width-2,TV_WARNING,"No step at this offset / k back");
            }
        } else dashboard_text(&c,1,4,width-2,TV_WARNING,"No matching runs / 0 reset filters");
        if (height>9) dashboard_text(&c,1,6,width-2,TV_BORDER,"Timing %zu/%zu / missing stays --",summary.duration_known,summary.steps);
    } else {
        left=width>=110 ? 25 : 0; body=width-left; cards=body/4;
        if (left) {
            tv_panel(&c,(struct tv_rect){0,2,left-1,height-4},"SCOPE & COVERAGE");
            dashboard_text(&c,2,4,left-5,TV_STRONG,"%s",statistics_range(v));
            dashboard_text(&c,2,5,left-5,TV_BASE,"T change range");
            dashboard_text(&c,2,7,left-5,TV_BASE,"Workflow: %s",v->filter.workflow[0] ? v->filter.workflow : "all");
            dashboard_text(&c,2,8,left-5,TV_BORDER,"[ / ] choose");
            dashboard_text(&c,2,10,left-5,TV_BASE,"%zu undated",summary.undated);
            dashboard_text(&c,2,11,left-5,TV_BASE,"%zu date-excluded",summary.excluded_undated);
            dashboard_text(&c,2,13,left-5,TV_BASE,"Timing %zu/%zu",summary.duration_known,summary.steps);
            dashboard_text(&c,2,14,left-5,TV_BASE,"Attempts %zu/%zu",summary.attempts_known,summary.steps);
            if (height>23) {
                dashboard_text(&c,2,17,left-5,TV_WARNING,"CPU / memory: --");
                dashboard_text(&c,2,18,left-5,TV_WARNING,"Tokens / cost: --");
                dashboard_text(&c,2,20,left-5,TV_BORDER,"No measurements");
                dashboard_text(&c,2,21,left-5,TV_BORDER,"0 reset filters");
            }
        }
        {
            struct tv_canvas card_view;
            (void)tv_canvas_view(&card_view,&c,(struct tv_rect){left,2,body,5});
            dashboard_card(&card_view,0,cards-1,"RUNS",summary.runs,"matched cohort",TV_STRONG);
            dashboard_card(&card_view,cards,cards-1,"RUNNING",summary.step_states[HS_RUNNING],"recorded steps",TV_STRONG);
            dashboard_card(&card_view,2*cards,cards-1,"ATTENTION",summary.run_states[HS_FAILED]+summary.run_states[HS_BLOCKED]+summary.run_states[HS_UNKNOWN],"run outcomes",TV_WARNING);
            dashboard_card(&card_view,3*cards,body-3*cards,"RETRIES",summary.attempts_known || !summary.steps ? (size_t)summary.retries : SIZE_MAX,"known attempts",TV_STRONG);
        }
        if (summary.duration_known) snprintf(duration,sizeof(duration),"%.1fs",(double)summary.duration_sum/(double)summary.duration_known);
        dashboard_text(&c,left+1,7,body-2,TV_BASE,"Latest attempt mean %s / n=%zu / max %s",duration,summary.duration_known,maximum);
        if (v->detail) statistics_run_detail(app,&c,(struct tv_rect){left,8,body,height-10});
        else {
            chart_height=height>=32 ? 10 : 0;
            if (chart_height) {
                statistics_distribution(&c,(struct tv_rect){left,8,body/2-1,chart_height},&summary);
                statistics_trend(&c,(struct tv_rect){left+body/2,8,body-body/2,chart_height},&summary);
            }
            statistics_run_table(app,&c,(struct tv_rect){left,8+chart_height,body,height-10-chart_height});
        }
    }
    dashboard_text(&c,0,height-2,width,(v->stale || (v->model && v->model->warnings)) ? TV_WARNING : TV_BORDER,
        "%s%s",v->model && v->model->warnings ? "PARTIAL / " : "",v->error[0] ? v->error : app->notice[0] ? app->notice : v->model && v->model->warnings ? v->model->warning : app->fleet ? "Source: latest fleet list / desired state is not process liveness" : "Source: recorded workflow scalars / success is not verified result");
    if (height>=25 && v->model && !app->fleet && width>=110)
        dashboard_text(&c,2,height-5,20,TV_BORDER,"%s",updated);
    tv_text(&c,(struct tv_rect){0,height-1,width,1},width<65 ? "D back T range / find Enter q quit" : width<100 ? "D back T range / find Enter evidence g graph q quit" : "D back / T range / ! attention / [ ] workflow / / find / Enter evidence / g graph / q quit",TV_STRONG);
    if (headless) {
        printf("FRAME %u %dx%d\n",frame,app->cols,app->rows);
        for(y=0;y<height;y++) { (void)tv_write_row(&c,y,stdout,NULL,NULL); putchar('\n'); }
    } else if (!tv_present(&w->presenter,&c,stdout,dashboard_style,app)) app->running=false;
    return true;
}
