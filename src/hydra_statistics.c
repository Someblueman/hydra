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
    uint64_t value;
    if (number(f[7], observed, &value)) r->started = value;
    if (number(f[8], observed, &value)) r->completed = value;
    if (number(f[9], observed, &value)) r->verified = value;
    r->recoveries_known = number(f[10], 999999, &value);
    if (r->recoveries_known) r->recoveries = (unsigned)value;
    r->planned = !strcmp(f[11], "1");
}

static void step_metrics(struct hs_step *s, char **f, uint64_t observed) {
    uint64_t value;
    if (number(f[8], observed, &value)) s->ready = value;
    if (number(f[9], observed, &value)) s->first_started = value;
}

bool hs_load(FILE *input, struct hs_model *m) {
    char line[2048];
    size_t bytes = 0;
    bool header = false, ended = false;
    memset(m, 0, sizeof(*m));
    while (fgets(line, sizeof(line), input)) {
        char *f[12], *p;
        size_t n = strlen(line), count = 1, i;
        uint64_t value;
        if (ended || !n || line[n-1] != '\n' || (bytes += n) > 1024U * 1024U) return false;
        line[n-1] = '\0'; f[0] = line;
        for (p = line; *p; p++) if (*p == '\t') {
            if (count == 12) return false;
            *p = '\0'; f[count++] = p + 1;
        }
        if (!header) {
            if (count != 3 || strcmp(f[0], "HYDRA_STATISTICS") || strcmp(f[1], "2") ||
                !number(f[2], 253402300799ULL, &m->observed) || !m->observed) return false;
            header = true; continue;
        }
        if (count == 2 && !strcmp(f[0], "X")) {
            m->warnings++; if (!copy(m->warning, sizeof(m->warning), f[1])) return false;
        } else if (count == 12 && !strcmp(f[0], "R")) {
            struct hs_run *r;
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
        } else if (count == 10 && !strcmp(f[0], "S")) {
            struct hs_step *s;
            size_t run;
            for (run = 0; run < m->run_count; run++) if (!strcmp(m->runs[run].id, f[1])) break;
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
        } else if (count == 3 && !strcmp(f[0], "Z")) {
            if (!number(f[1], HS_RUNS, &value) || value != m->run_count ||
                !number(f[2], HS_STEPS, &value) || value != m->step_count) return false;
            ended = true;
        } else return false;
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
    return contains(r->name, f->query) || contains(r->id, f->query) || contains(r->project, f->query);
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
    }
}
