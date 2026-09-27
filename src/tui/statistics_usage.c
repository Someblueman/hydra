#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
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
int statistics_wrap(struct tv_canvas *c, int x, int y, int width, int rows, enum tv_style tone, const char *text) {
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

/* The scope side panel: token totals, cost only when reported, and the
 * resource metrics Hydra does not collect, said in full. */
void statistics_scope_notes(struct tv_canvas *c, int x, int y, int width, int rows, const struct hs_summary *s) {
    static const char *const labels[] = {"in", "cached", "out"};
    char value[24], cost[48];
    int row = y, end = y + rows;
    size_t i;
    dashboard_text(c, x, row++, width, TV_STRONG, "Agent tokens");
    if (!s->agent_steps && row < end) dashboard_text(c, x, row++, width, TV_MUTED, "no agent steps");
    for (i = 0; s->agent_steps && i < 3 && row < end; i++) {
        statistics_count(s->usage[i], s->usage_known[i] > 0, value, sizeof(value));
        dashboard_text(c, x, row++, width, TV_BASE, "%-7s %s", labels[i], value);
    }
    if (s->agent_steps && row < end)
        dashboard_text(c, x, row++, width, TV_MUTED, "%zu of %zu reported", s->usage_known[HS_TOKENS_IN], s->agent_steps);
    cost_text(s->usage[HS_COST_MICROUSD], s->usage_known[HS_COST_MICROUSD] > 0, cost, sizeof(cost));
    if (s->agent_steps && row < end) dashboard_text(c, x, row++, width, TV_MUTED, "%s", cost);
    if (row + 1 < end) row++;
    row += statistics_wrap(c, x, row, width, end - row, TV_MUTED,
        "CPU and memory: not measured. Hydra does not sample processes yet (roadmap item 10).");
    if (row + 1 < end) statistics_wrap(c, x, row + 1, width, end - row - 1, TV_MUTED, "0 resets filters");
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
void statistics_step_detail(struct tv_canvas *c, struct tv_rect r, const struct hs_model *m, const struct hs_step *s) {
    char text[512], tokens[96], cost[48], seconds[32] = "time unknown";
    const struct hs_usage *u = &s->usage;
    uint64_t duration;
    int row = r.y;
    if (hs_duration(m, s, &duration)) (void)snprintf(seconds, sizeof(seconds), "%llus", (unsigned long long)duration);
    dashboard_text(c, r.x, row++, r.width, TV_STRONG, "%s: %s, %s, %u attempt%s", s->id, s->state, seconds,
        s->attempts, s->attempts == 1 ? "" : "s");
    if (!u->recorded) {
        statistics_wrap(c, r.x, row, r.width, r.height - 1, TV_MUTED,
            strcmp(s->kind, "exec") ? "Not an agent step: no tokens apply." : "No agent receipt was recorded for this step.");
        return;
    }
    statistics_tokens(u, tokens, sizeof(tokens));
    cost_text(u->counts[HS_COST_MICROUSD], u->known[HS_COST_MICROUSD], cost, sizeof(cost));
    if (snprintf(text, sizeof(text), "Agent %s, version %s, model %s, effort %s.",
                 u->profile[0] ? u->profile : "unknown", u->version[0] ? u->version : "not recorded",
                 u->model[0] ? u->model : "not recorded", u->effort[0] ? u->effort : "not recorded") >= (int)sizeof(text))
        text[sizeof(text) - 1] = '\0';
    row += statistics_wrap(c, r.x, row, r.width, r.height - 2, TV_BASE, text);
    dashboard_text(c, r.x, row, r.width, TV_BASE, "Tokens %s; %s.", tokens, cost);
}
