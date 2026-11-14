#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "statistics.h"
/* Agent evidence in the statistics view: tokens, agent, version, model and
 * effort come from exec receipts. Unknown is named, never shown as zero. */

void statistics_count(uint64_t value, bool known, char *out, size_t size) {
    int written;
    if (!known) written = snprintf(out, size, "unknown");
    else if (value >= 1000000) written = snprintf(out, size, "%.2fM", (double)value / 1e6);
    else if (value >= 1000) written = snprintf(out, size, "%.1fk", (double)value / 1e3);
    else written = snprintf(out, size, "%llu", (unsigned long long)value);
    if (written < 0 || (size_t)written >= size) copy_text(out, size, "?");
}

static void cost_text(uint64_t micro, bool known, char *out, size_t size) {
    if (!known) copy_text(out, size, "cost not reported");
    else if (snprintf(out, size, "cost $%.4f", (double)micro / 1e6) >= (int)size) copy_text(out, size, "cost ?");
}

void statistics_tokens(const struct hs_usage *u, char *out, size_t size) {
    char in[24], cached[24], output[24];
    if (!u->recorded) { copy_text(out, size, "no agent receipt"); return; }
    statistics_count(u->counts[HS_TOKENS_IN], u->known[HS_TOKENS_IN], in, sizeof(in));
    statistics_count(u->counts[HS_TOKENS_CACHED], u->known[HS_TOKENS_CACHED], cached, sizeof(cached));
    statistics_count(u->counts[HS_TOKENS_OUT], u->known[HS_TOKENS_OUT], output, sizeof(output));
    if (snprintf(out, size, "in %s / cached %s / out %s", in, cached, output) >= (int)size) out[size - 1] = '\0';
}

/* Scope totals add only the counts that were reported; the known/total pair
 * says how many agent steps contributed, so a partial sum is never passed
 * off as complete. */
void statistics_scope_tokens(const struct hs_summary *s, char *out, size_t size) {
    char in[24], cached[24], output[24];
    if (!s->agent_steps) { copy_text(out, size, "no agent steps in scope"); return; }
    statistics_count(s->usage[HS_TOKENS_IN], s->usage_known[HS_TOKENS_IN] > 0, in, sizeof(in));
    statistics_count(s->usage[HS_TOKENS_CACHED], s->usage_known[HS_TOKENS_CACHED] > 0, cached, sizeof(cached));
    statistics_count(s->usage[HS_TOKENS_OUT], s->usage_known[HS_TOKENS_OUT] > 0, output, sizeof(output));
    if (snprintf(out, size, "tokens in %s / cached %s / out %s from %zu/%zu agent steps", in, cached, output,
                 s->usage_known[HS_TOKENS_IN], s->agent_steps) >= (int)size) out[size - 1] = '\0';
}

/* Word-wraps text into at most rows lines of width cells; returns rows used. */
int statistics_wrap(struct kt_canvas *c, int x, int y, int width, int rows, struct kt_style tone, const char *text) {
    int used = 0;
    while (*text && used < rows && width > 0) {
        size_t length = strlen(text), take = length <= (size_t)width ? length : (size_t)width;
        if (take < length) {
            size_t space = take;
            while (space > 0 && text[space] != ' ') space--;
            if (space > 0) take = space;
        }
        dashboard_text(c, x, y + used++, width, tone, "%.*s", (int)take, text);
        text += take;
        while (*text == ' ') text++;
    }
    return used;
}

/* Per-run token total for the run table; empty when the run has no agent step. */
void statistics_run_tokens(const struct hs_model *m, size_t run, char *out, size_t size) {
    struct hs_summary totals;
    char in[24], output[24];
    size_t i;
    memset(&totals, 0, sizeof(totals));
    for (i = 0; i < m->step_count; i++) if (m->steps[i].run == run) hs_usage_add(&totals, &m->steps[i]);
    out[0] = '\0';
    if (!totals.agent_steps) return;
    statistics_count(totals.usage[HS_TOKENS_IN], totals.usage_known[HS_TOKENS_IN] > 0, in, sizeof(in));
    statistics_count(totals.usage[HS_TOKENS_OUT], totals.usage_known[HS_TOKENS_OUT] > 0, output, sizeof(output));
    if (snprintf(out, size, "%s in / %s out", in, output) >= (int)size) out[size - 1] = '\0';
}

/* One line per step in the run detail: agent and tokens, or why there are
 * none. Narrow tables use the compact in/cached/out form. */
void statistics_step_agent(const struct hs_step *s, bool compact, char *out, size_t size) {
    char tokens[96], in[24], cached[24], output[24];
    const struct hs_usage *u = &s->usage;
    if (!u->recorded) {
        copy_text(out, size, strcmp(s->kind, "exec") ? "" : "no agent receipt");
        return;
    }
    statistics_tokens(u, tokens, sizeof(tokens));
    if (compact) {
        statistics_count(u->counts[HS_TOKENS_IN], u->known[HS_TOKENS_IN], in, sizeof(in));
        statistics_count(u->counts[HS_TOKENS_CACHED], u->known[HS_TOKENS_CACHED], cached, sizeof(cached));
        statistics_count(u->counts[HS_TOKENS_OUT], u->known[HS_TOKENS_OUT], output, sizeof(output));
        if (snprintf(tokens, sizeof(tokens), "%s/%s/%s", in, cached, output) >= (int)sizeof(tokens)) tokens[sizeof(tokens) - 1] = '\0';
    }
    if (snprintf(out, size, "%s  %s", u->profile[0] ? u->profile : "agent unknown", tokens) >= (int)size)
        out[size - 1] = '\0';
}

/* The expanded block for the first visible step of the selected run. */
void statistics_step_detail(struct kt_canvas *c, struct kt_rect r, const struct hs_model *m, const struct hs_step *s) {
    char text[512], tokens[96], cost[48], seconds[32] = "time unknown";
    const struct hs_usage *u = &s->usage;
    uint64_t duration;
    int row = r.y;
    if (hs_duration(m, s, &duration)) (void)snprintf(seconds, sizeof(seconds), "%llus", (unsigned long long)duration);
    dashboard_text(c, r.x, row++, r.width, KT_STRONG, "%s: %s, %s, %u attempt%s", s->id, s->state, seconds,
        s->attempts, s->attempts == 1 ? "" : "s");
    if (!u->recorded) {
        statistics_wrap(c, r.x, row, r.width, r.height - 1, KT_MUTED,
            strcmp(s->kind, "exec") ? "Not an agent step: no tokens apply." : "No agent receipt was recorded for this step.");
        return;
    }
    statistics_tokens(u, tokens, sizeof(tokens));
    cost_text(u->counts[HS_COST_MICROUSD], u->known[HS_COST_MICROUSD], cost, sizeof(cost));
    if (snprintf(text, sizeof(text), "Agent %s, version %s, model %s, effort %s.",
                 u->profile[0] ? u->profile : "unknown", u->version[0] ? u->version : "not recorded",
                 u->model[0] ? u->model : "not recorded", u->effort[0] ? u->effort : "not recorded") >= (int)sizeof(text))
        text[sizeof(text) - 1] = '\0';
    row += statistics_wrap(c, r.x, row, r.width, r.height - 2, KT_BASE, text);
    dashboard_text(c, r.x, row, r.width, KT_BASE, "Tokens %s; %s.", tokens, cost);
}

/* ---- run evidence -------------------------------------------------------- */

static void step_row(struct kt_canvas *c, struct kt_rect r, int row, const struct hs_model *m, const struct hs_step *s) {
    char seconds[24] = "--", attempts[24] = "--", agent[160];
    uint64_t duration;
    bool compact = r.width < 100;
    if (hs_duration(m, s, &duration)) (void)snprintf(seconds, sizeof(seconds), "%llu", (unsigned long long)duration);
    if (s->attempts_known) (void)snprintf(attempts, sizeof(attempts), "%u", s->attempts);
    statistics_step_agent(s, compact, agent, sizeof(agent));
    dashboard_text(c, r.x, row, r.width, hs_state(s->state) == HS_FAILED ? KT_WARNING : KT_BASE,
        compact ? "%-16.16s %-10.10s %3s %5s  %s" : "%-20.20s %-15.15s %5s %8s  %s", s->id, s->state, attempts, seconds, agent);
}

/* The run's totals over its recorded steps. */
static void run_totals(struct kt_canvas *c, struct kt_rect r, int row, const struct hs_model *m, size_t run_index) {
    struct hs_summary totals;
    char tokens[192];
    size_t i;
    memset(&totals, 0, sizeof(totals));
    for (i = 0; i < m->step_count; i++) if (m->steps[i].run == run_index) hs_usage_add(&totals, &m->steps[i]);
    statistics_scope_tokens(&totals, tokens, sizeof(tokens));
    dashboard_text(c, r.x, row, r.width, KT_BASE, "Run %s", tokens);
}

/* The steps from the scroll position; the first one shown is detailed below
 * them when there is room. */
static void run_steps(const struct statistics_view *v, struct kt_canvas *c, struct kt_rect r, size_t run_index) {
    const struct hs_step *first = NULL;
    size_t i, ordinal = 0;
    int row = r.y, bottom = r.y + r.height - (r.height > 8 ? 5 : 0);
    for (i = 0; i < v->model->step_count && row < bottom; i++) {
        const struct hs_step *s = &v->model->steps[i];
        if (s->run != run_index || ordinal++ < v->step_scroll) continue;
        if (!first) first = s;
        step_row(c, r, row++, v->model, s);
    }
    if (row == r.y) dashboard_text(c, r.x, row, r.width, KT_WARNING, "No step evidence at this scroll position");
    if (first && r.height > 8) statistics_step_detail(c, (struct kt_rect){r.x, bottom + 1, r.width, 4}, v->model, first);
}

void stats_run_evidence(struct app *app, struct kt_canvas *c, struct kt_rect r, bool focused) {
    const struct statistics_view *v = app->statistics;
    const struct hs_run *run;
    struct kt_rect inner = {r.x + 2, r.y, r.width - 3, r.height};
    size_t run_index;
    stats_rule(c, (struct kt_rect){r.x, r.y, r.width, 1}, "Run evidence", "latest attempts", focused);
    if (!v->count) { dashboard_text(c, inner.x, r.y + 1, inner.width, KT_WARNING, "Selected run no longer matches"); return; }
    run_index = v->visible[v->selected]; run = &v->model->runs[run_index];
    dashboard_text(c, inner.x, r.y + 1, inner.width, KT_STRONG, "%s / %s", run->name, run->state);
    dashboard_text(c, inner.x, r.y + 2, inner.width, KT_MUTED, "%s", run->id);
    run_totals(c, inner, r.y + 3, v->model, run_index);
    if (r.height < 7) return;
    dashboard_text(c, inner.x, r.y + 4, inner.width, KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0),
                   inner.width < 100 ? "STEP             STATE      TRY  SECS  AGENT TOKENS IN/CACHED/OUT"
                                     : "STEP                 STATE           TRIES  SECONDS  AGENT / TOKENS");
    run_steps(v, c, (struct kt_rect){inner.x, r.y + 5, inner.width, r.height - 5}, run_index);
}
