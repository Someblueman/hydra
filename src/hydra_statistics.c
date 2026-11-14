#include "hydra_statistics.h"
#include <ctype.h>
#include <string.h>

static bool number(const char *s, uint64_t limit, uint64_t *out) {
    uint64_t n = 0;
    if (!*s) return false;
    for (; *s; s++) {
        unsigned digit = (unsigned char)*s - (unsigned)'0';
        if (digit > 9 || n > limit / 10 || (n == limit / 10 && digit > limit % 10)) return false;
        n = n * 10 + digit;
    }
    *out = n; return true;
}

static bool copy(char *out, size_t capacity, const char *s) {
    size_t n = strlen(s);
    if (n >= capacity || !n) return false;
    memcpy(out, s, n + 1); return true;
}

static bool id(const char *s) {
    size_t i, n = strlen(s);
    if (!n || n >= 80) return false;
    for (i = 0; i < n; i++) if (!((s[i] >= 'a' && s[i] <= 'z') ||
        (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-')) return false;
    return true;
}

static uint64_t iso_time(const char *s) {
    static const unsigned months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned values[6] = {0}, widths[] = {4,2,2,2,2,2}, i, j, pos = 0, year, days;
    const char separators[] = "--T::Z";
    uint64_t total;
    bool leap;
    if (strlen(s) != 20) return 0;
    for (i = 0; i < 6; i++) {
        for (j = 0; j < widths[i]; j++, pos++) {
            if (s[pos] < '0' || s[pos] > '9') return 0;
            values[i] = values[i] * 10 + (unsigned)(s[pos] - '0');
        }
        if (s[pos++] != separators[i]) return 0;
    }
    year = values[0]; leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < 1970 || values[1] < 1 || values[1] > 12 || values[2] < 1 ||
        values[2] > months[values[1]-1] + (unsigned)(leap && values[1] == 2) ||
        values[3] > 23 || values[4] > 59 || values[5] > 59) return 0;
    days = (year - 1970) * 365 + (year-1)/4 - 1969/4 - (year-1)/100 + 1969/100 + (year-1)/400 - 1969/400;
    for (i = 1; i < values[1]; i++) days += months[i-1] + (unsigned)(leap && i == 2);
    total = (uint64_t)(days + values[2] - 1) * 86400;
    return total + values[3] * 3600 + values[4] * 60 + values[5];
}

enum hs_state hs_state(const char *s) {
    if (!strcmp(s, "succeeded")) return HS_SUCCEEDED;
    if (!strcmp(s, "failed")) return HS_FAILED;
    if (!strcmp(s, "running")) return HS_RUNNING;
    if (!strcmp(s, "queued") || !strcmp(s, "ready") || !strcmp(s, "retrying")) return HS_QUEUED;
    if (!strcmp(s, "recovery-required") || !strcmp(s, "blocked") || !strcmp(s, "waiting-approval")) return HS_BLOCKED;
    if (!strcmp(s, "cancelled")) return HS_CANCELLED;
    return HS_UNKNOWN;
}

static void run_metrics(struct hs_run *r, char **f, uint64_t observed) {
    uint64_t value = 0;
    if (number(f[7], observed, &value)) r->started = value;
    if (number(f[8], observed, &value)) r->completed = value;
    if (number(f[9], observed, &value)) r->verified = value;
    r->recoveries_known = number(f[10], 999999, &value);
    if (r->recoveries_known) r->recoveries = (unsigned)value;
    r->planned = !strcmp(f[11], "1");
    if (f[12]) r->unknown_outcomes_known = number(f[12], 999999, &value);
    if (r->unknown_outcomes_known) r->unknown_outcomes = value;
    if (f[13]) r->interventions_known = number(f[13], 999999, &value);
    if (r->interventions_known) r->interventions = value;
    if (f[14]) r->transfer_bytes_known = number(f[14], UINT64_MAX, &value);
    if (r->transfer_bytes_known) r->transfer_bytes = value;
}

static void step_metrics(struct hs_step *s, char **f, uint64_t observed) {
    uint64_t value;
    if (number(f[8], observed, &value)) s->ready = value;
    if (number(f[9], observed, &value)) s->first_started = value;
}

static bool run_row(char **fields, size_t count, unsigned version) {
    return count == (version >= 3 ? 15U : 12U) && !strcmp(fields[0], "R");
}

static size_t find_run(const struct hs_model *m, const char *id) {
    size_t run;
    for (run = 0; run < m->run_count; run++) if (!strcmp(m->runs[run].id, id)) break;
    return run;
}

static bool parse_step(struct hs_model *m, char **f) {
    struct hs_step *s;
    size_t i, run = find_run(m, f[1]);
    uint64_t value;
    if (run == m->run_count || m->step_count == HS_STEPS || !id(f[2])) return false;
    for (i = 0; i < m->step_count; i++) if (m->steps[i].run == run && !strcmp(m->steps[i].id, f[2])) return false;
    s = &m->steps[m->step_count++]; s->run = run;
    if (!copy(s->id, sizeof(s->id), f[2]) || !copy(s->kind, sizeof(s->kind), f[3]) ||
        !copy(s->state, sizeof(s->state), f[4])) return false;
    s->attempts_known = number(f[5], 999999, &value);
    if (s->attempts_known) s->attempts = (unsigned)value;
    if (number(f[6], m->observed, &value)) s->started = value;
    if (number(f[7], m->observed, &value)) s->completed = value;
    step_metrics(s, f, m->observed);
    return true;
}

/* Text fields keep "-" as unknown; overlong values are refused, not cut. */
static bool usage_text(char *out, size_t capacity, const char *value) {
    if (!strcmp(value, "-")) { out[0] = '\0'; return true; }
    return copy(out, capacity, value);
}

/* U rows (schema 4) attach one exec receipt's evidence to an existing step. */
static bool parse_usage(struct hs_model *m, char **f) {
    size_t i, run = find_run(m, f[1]);
    struct hs_usage *u = NULL;
    for (i = 0; run < m->run_count && i < m->step_count; i++)
        if (m->steps[i].run == run && !strcmp(m->steps[i].id, f[2])) u = &m->steps[i].usage;
    if (!u || u->recorded || !usage_text(u->profile, sizeof(u->profile), f[3]) ||
        !usage_text(u->version, sizeof(u->version), f[4]) || !usage_text(u->model, sizeof(u->model), f[5]) ||
        !usage_text(u->effort, sizeof(u->effort), f[6])) return false;
    for (i = 0; i < HS_USAGE_COUNTS; i++) {
        u->known[i] = number(f[7 + i], UINT64_MAX / 4, &u->counts[i]);
        if (!u->known[i] && strcmp(f[7 + i], "-")) return false;
    }
    u->recorded = true;
    return true;
}
static bool parse_run(struct hs_model *m, char **f) {
    struct hs_run *r;
    size_t i;
    if (m->run_count == HS_RUNS || !id(f[1])) return false;
    for (i = 0; i < m->run_count; i++) if (!strcmp(m->runs[i].id, f[1])) return false;
    r = &m->runs[m->run_count++];
    if (!copy(r->id, sizeof(r->id), f[1]) || !copy(r->name, sizeof(r->name), f[2]) ||
        !copy(r->state, sizeof(r->state), f[3]) || !copy(r->project, sizeof(r->project), f[4])) return false;
    r->created = iso_time(f[5]);
    if (r->created > m->observed) r->created = 0;
    if (strcmp(f[6], "partial") && strcmp(f[6], "complete")) return false;
    r->partial = !strcmp(f[6], "partial");
    run_metrics(r, f, m->observed);
    return true;
}

/* A time is "-" (unknown) or an epoch; one after the sample is not believed. */
static bool session_time(const struct hs_model *m, const char *text, uint64_t *value, bool *known) {
    *known = number(text, 253402300799ULL, value);
    if (!*known) return !strcmp(text, "-");
    *known = *value && *value <= m->observed;
    return true;
}

static bool session_seconds(const char *text, uint64_t *value, bool *known) {
    *known = number(text, 4294967295ULL, value);
    return *known || !strcmp(text, "-");
}

static bool session_kind(struct hs_session *s, const char *mode, const char *state, const char *outcome) {
    static const char *const outcomes[] = {"-", "done", "failed", "blocked", "abandoned", "canceled"};
    size_t i;
    if (strcmp(mode, "interactive") && strcmp(mode, "headless")) return false;
    s->headless = !strcmp(mode, "headless");
    if (!strcmp(state, "open")) s->state = HS_SESSION_OPEN;
    else if (!strcmp(state, "ended")) s->state = HS_SESSION_ENDED;
    else if (!strcmp(state, "superseded")) s->state = HS_SESSION_SUPERSEDED;
    else return false;
    for (i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); i++)
        if (!strcmp(outcome, outcomes[i])) return usage_text(s->outcome, sizeof(s->outcome), outcome);
    return false;
}

/* H rows (schema 6): one head session. An open session has no end, and an
 * end before the start is not an end. */
static bool parse_session(struct hs_model *m, char **f) {
    struct hs_session *s;
    size_t i;
    if (m->session_count == HS_SESSIONS || !id(f[1]) || !id(f[2])) return false;
    for (i = 0; i < m->session_count; i++) if (!strcmp(m->sessions[i].instance, f[2])) return false;
    s = &m->sessions[m->session_count++];
    if (!copy(s->head, sizeof(s->head), f[1]) || !copy(s->instance, sizeof(s->instance), f[2]) ||
        !copy(s->branch, sizeof(s->branch), f[3]) || !usage_text(s->profile, sizeof(s->profile), f[4]) ||
        !session_kind(s, f[5], f[6], f[9])) return false;
    if (!session_time(m, f[7], &s->started, &s->started_known) || !session_time(m, f[8], &s->ended, &s->ended_known) ||
        !session_seconds(f[10], &s->active, &s->active_known) || !session_seconds(f[11], &s->quiet, &s->quiet_known) ||
        !session_seconds(f[12], &s->unobserved, &s->unobserved_known) ||
        !session_seconds(f[13], &s->reported, &s->reported_known)) return false;
    if (s->state == HS_SESSION_OPEN && strcmp(f[8], "-")) return false;
    if (s->started_known && s->ended_known && s->ended < s->started) s->ended_known = false;
    return true;
}

/* T rows (schema 5) attach one dispatched attempt's remote evidence to an
 * existing step. Text fields keep "-" as unknown; counts keep "-" unknown. */
static bool parse_remote(struct hs_model *m, char **f) {
    size_t i, run = find_run(m, f[1]);
    struct hs_remote *r = NULL;
    uint64_t value;
    for (i = 0; run < m->run_count && i < m->step_count; i++)
        if (m->steps[i].run == run && !strcmp(m->steps[i].id, f[2])) r = &m->steps[i].remote;
    if (!r || r->recorded || !usage_text(r->host, sizeof(r->host), f[3]) ||
        !usage_text(r->transport, sizeof(r->transport), f[4]) || !usage_text(r->placement, sizeof(r->placement), f[5]) ||
        !usage_text(r->state, sizeof(r->state), f[7]) || !usage_text(r->reason, sizeof(r->reason), f[13])) return false;
    if (r->placement[0] && strcmp(r->placement, "auto") && strcmp(r->placement, "pinned")) return false;
    r->generation_known = number(f[6], 999999, &value);
    if (r->generation_known) r->generation = (unsigned)value;
    else if (strcmp(f[6], "-")) return false;
    for (i = 0; i < HS_REMOTE_COUNTS; i++) {
        r->known[i] = number(f[8 + i], UINT64_MAX / 4, &r->counts[i]);
        if (!r->known[i] && strcmp(f[8 + i], "-")) return false;
    }
    r->recorded = true;
    return true;
}

static bool parse_header(struct hs_model *m, char **f, size_t count) {
    if (count != 3 || strcmp(f[0], "HYDRA_STATISTICS") || strlen(f[1]) != 1 || f[1][0] < '2' || f[1][0] > '6' ||
        !number(f[2], 253402300799ULL, &m->observed) || !m->observed) return false;
    m->schema_version = (unsigned)(f[1][0] - '0');
    return true;
}

/* Z closes the feed with the row counts; schema 6 adds the session count. */
static bool parse_end(const struct hs_model *m, char **f, size_t count) {
    uint64_t value;
    if (count != (m->schema_version >= 6 ? 4U : 3U)) return false;
    if (!number(f[1], HS_RUNS, &value) || value != m->run_count ||
        !number(f[2], HS_STEPS, &value) || value != m->step_count) return false;
    return count == 3 || (number(f[3], HS_SESSIONS, &value) && value == m->session_count);
}

static bool parse_row(struct hs_model *m, char **f, size_t count, bool *ended) {
    if (count == 2 && !strcmp(f[0], "X")) {
        m->warnings++;
        return copy(m->warning, sizeof(m->warning), f[1]);
    }
    if (run_row(f, count, m->schema_version)) return parse_run(m, f);
    if (count == 10 && !strcmp(f[0], "S")) return parse_step(m, f);
    if (count == 11 && m->schema_version >= 4 && !strcmp(f[0], "U")) return parse_usage(m, f);
    if (count == 14 && m->schema_version >= 5 && !strcmp(f[0], "T")) return parse_remote(m, f);
    if (count == 14 && m->schema_version >= 6 && !strcmp(f[0], "H")) return parse_session(m, f);
    if (strcmp(f[0], "Z")) return false;
    *ended = parse_end(m, f, count);
    return *ended;
}

/* Splits a row at its tabs in place; 0 when it has more than 15 fields. */
static size_t split_row(char *line, char **f) {
    size_t count = 1;
    char *p;
    f[0] = line;
    for (p = line; *p; p++) if (*p == '\t') {
        if (count == 15) return 0;
        *p = '\0'; f[count++] = p + 1;
    }
    return count;
}

bool hs_load(FILE *input, struct hs_model *m) {
    char line[2048];
    size_t bytes = 0;
    bool header = false, ended = false;
    memset(m, 0, sizeof(*m));
    while (fgets(line, sizeof(line), input)) {
        char *f[15] = {0};
        size_t n = strlen(line), count;
        if (ended || !n || line[n-1] != '\n' || (bytes += n) > 1024U * 1024U) return false;
        line[n-1] = '\0';
        count = split_row(line, f);
        if (!count) return false;
        if (!header) {
            if (!parse_header(m, f, count)) return false;
            header = true; continue;
        }
        if (!parse_row(m, f, count, &ended)) return false;
    }
    return header && ended && !ferror(input);
}

static bool contains(const char *s, const char *query) {
    size_t i;
    if (!*query) return true;
    for (; *s; s++) {
        for (i = 0; query[i] && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)query[i]); i++) { }
        if (!query[i]) return true;
    }
    return false;
}

bool hs_matches(const struct hs_model *m, size_t run, const struct hs_filter *f) {
    const struct hs_run *r = &m->runs[run];
    enum hs_state state = hs_state(r->state);
    if (f->days && (!r->created || m->observed - r->created > (uint64_t)f->days * 86400)) return false;
    if (f->workflow[0] && strcmp(f->workflow, r->name)) return false;
    if (f->attention && state != HS_FAILED && state != HS_BLOCKED && state != HS_UNKNOWN) return false;
    if (f->location[0]) {
        if (!strcmp(f->location, "local")) { if (hs_run_remote(m, run, NULL)) return false; }
        else if (!hs_run_remote(m, run, strcmp(f->location, "remote") ? f->location : NULL)) return false;
    }
    return contains(r->name, f->query) || contains(r->id, f->query) || contains(r->project, f->query);
}

bool hs_session_matches(const struct hs_model *m, size_t session, const struct hs_filter *f) {
    const struct hs_session *s = &m->sessions[session];
    if (f->days && (!s->started_known || m->observed - s->started > (uint64_t)f->days * 86400)) return false;
    if (f->attention && strcmp(s->outcome, "failed") && strcmp(s->outcome, "blocked")) return false;
    /* Head sessions are local work: a remote or host filter leaves none. */
    if (f->location[0] && strcmp(f->location, "local")) return false;
    return contains(s->branch, f->query) || contains(s->profile, f->query) ||
           contains(s->head, f->query) || contains(s->instance, f->query);
}

bool hs_session_length(const struct hs_model *m, const struct hs_session *s, uint64_t *seconds) {
    if (!s->started_known) return false;
    if (s->state == HS_SESSION_OPEN) { *seconds = m->observed - s->started; return true; }
    if (!s->ended_known) return false;
    *seconds = s->ended - s->started;
    return true;
}

/* A run is remote when any recorded step ran as a remote task; with a host,
 * when any such step ran on that alias. */
bool hs_run_remote(const struct hs_model *m, size_t run, const char *host) {
    size_t i;
    for (i = 0; i < m->step_count; i++) {
        const struct hs_remote *r = &m->steps[i].remote;
        if (m->steps[i].run == run && r->recorded && (!host || !strcmp(r->host, host))) return true;
    }
    return false;
}

/* The receipt carried a telemetry block unless the reason says it did not. */
bool hs_remote_telemetry(const struct hs_remote *r) {
    return r->recorded && strcmp(r->reason, "no_telemetry_block") && strcmp(r->reason, "receipt_unverified");
}

void hs_remote_add(struct hs_summary *out, const struct hs_step *s) {
    size_t i;
    const struct hs_remote *r = &s->remote;
    if (!r->recorded) return;
    out->remote_steps++;
    if (hs_remote_telemetry(r)) out->remote_telemetry++;
    for (i = 0; i < HS_REMOTE_COUNTS; i++) if (r->known[i]) {
        out->remote_known[i]++;
        if (i == HS_PEAK_RSS_BYTES) { if (r->counts[i] > out->remote[i]) out->remote[i] = r->counts[i]; }
        else out->remote[i] += r->counts[i];
    }
}

bool hs_duration(const struct hs_model *m, const struct hs_step *s, uint64_t *seconds) {
    if (!s->attempts_known || !s->attempts || !s->started || !s->completed ||
        s->completed < s->started || s->completed > m->observed ||
        (m->runs[s->run].created && s->started < m->runs[s->run].created)) return false;
    *seconds = s->completed - s->started; return true;
}

void hs_summarize(const struct hs_model *m, const struct hs_filter *f, struct hs_summary *out) {
    size_t i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < m->run_count; i++) {
        const struct hs_run *r = &m->runs[i];
        if (!hs_matches(m, i, f)) {
            struct hs_filter undated = *f; undated.days = 0;
            if (f->days && !r->created && hs_matches(m, i, &undated)) out->excluded_undated++;
            continue;
        }
        out->runs++; out->run_states[hs_state(r->state)]++;
        if (!r->created) out->undated++;
        else if (m->observed - r->created < 7 * 86400) out->daily[6 - (size_t)((m->observed-r->created)/86400)]++;
        if (r->partial) out->partial_runs++;
    }
    for (i = 0; i < m->step_count; i++) {
        const struct hs_step *s = &m->steps[i];
        uint64_t duration;
        if (!hs_matches(m, s->run, f)) continue;
        out->steps++; out->step_states[hs_state(s->state)]++;
        if (s->attempts_known) {
            out->attempts_known++; out->attempts += s->attempts;
            if (s->attempts) out->retries += s->attempts - 1;
        }
        if (hs_duration(m, s, &duration)) {
            out->duration_known++; out->duration_sum += duration;
            if (duration > out->duration_max) out->duration_max = duration;
        }
        hs_usage_add(out, s);
        hs_remote_add(out, s);
    }
}

void hs_usage_add(struct hs_summary *out, const struct hs_step *s) {
    size_t i;
    if (!s->usage.recorded) return;
    out->agent_steps++;
    for (i = 0; i < HS_USAGE_COUNTS; i++) if (s->usage.known[i]) {
        out->usage_known[i]++; out->usage[i] += s->usage.counts[i];
    }
}
