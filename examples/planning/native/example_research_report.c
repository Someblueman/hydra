#include "example_research.h"
#include <limits.h>

/* Producer selects directly from the unscheduled input on each dispatch. */
static json_object *schedule(const struct research_data *data, bool shortest) {
    bool used[RESEARCH_JOBS] = {false};
    int64_t now = 0, waits = 0, turns = 0, max_wait = -1, tail = 0;
    const char *worst = "";
    json_object *rows = json_object_new_array();
    for (size_t count = 0; count < RESEARCH_JOBS; count++) {
        int chosen = -1; int64_t next = INT64_MAX;
        for (size_t i = 0; i < RESEARCH_JOBS; i++)
            if (!used[i] && data->jobs[i].arrival < next) next = data->jobs[i].arrival;
        if (now < next) now = next;
        for (size_t i = 0; i < RESEARCH_JOBS; i++) {
            const struct research_job *job = &data->jobs[i];
            if (used[i] || job->arrival > now) continue;
            if (chosen < 0) { chosen = (int)i; continue; }
            const struct research_job *best = &data->jobs[chosen];
            if ((shortest && job->duration < best->duration) ||
                ((!shortest || job->duration == best->duration) && job->arrival < best->arrival)) chosen = (int)i;
        }
        if (chosen < 0) goto invalid;
        const struct research_job *job = &data->jobs[chosen];
        if (job->duration > INT64_MAX - now) goto invalid;
        int64_t finish = now + job->duration, wait = now - job->arrival, turn = finish - job->arrival;
        if (wait > INT64_MAX - waits || turn > INT64_MAX - turns) goto invalid;
        waits += wait; turns += turn;
        if (wait > max_wait) { max_wait = wait; worst = job->id; }
        if (turn > tail) tail = turn;
        json_object *row = json_object_new_object();
        json_object_object_add(row, "id", json_object_new_string(job->id));
        json_object_object_add(row, "start", json_object_new_int64(now));
        json_object_object_add(row, "finish", json_object_new_int64(finish));
        json_object_object_add(row, "wait", json_object_new_int64(wait));
        json_object_object_add(row, "turnaround", json_object_new_int64(turn));
        json_object_array_add(rows, row);
        now = finish; used[chosen] = true;
    }
    json_object *result = json_object_new_object();
    json_object_object_add(result, "schedule", rows);
    json_object_object_add(result, "mean_wait", json_object_new_double((double)waits / RESEARCH_JOBS));
    json_object_object_add(result, "mean_turnaround", json_object_new_double((double)turns / RESEARCH_JOBS));
    /* Nearest-rank p95 of exactly twelve observations is the maximum. */
    json_object_object_add(result, "p95_turnaround", json_object_new_int64(tail));
    json_object_object_add(result, "max_wait", json_object_new_int64(max_wait));
    json_object_object_add(result, "worst_wait_job", json_object_new_string(worst));
    return result;
invalid:
    json_object_put(rows); return NULL;
}

int ex_research_report(void) {
    struct research_data data;
    if (research_load(&data)) return 2;
    json_object *fcfs = schedule(&data, false), *sjf = schedule(&data, true);
    research_free(&data);
    if (!fcfs || !sjf) { json_object_put(fcfs); json_object_put(sjf); return 2; }
    bool fc_pass = json_object_get_int64(json_object_object_get(fcfs, "p95_turnaround")) <= 22 &&
                   json_object_get_int64(json_object_object_get(fcfs, "max_wait")) <= 16;
    bool sj_pass = json_object_get_int64(json_object_object_get(sjf, "p95_turnaround")) <= 22 &&
                   json_object_get_int64(json_object_object_get(sjf, "max_wait")) <= 16;
    json_object *report = json_object_new_object(), *policies = json_object_new_object();
    json_object *claims = json_object_new_object(), *checks = json_object_new_object();
    json_object_object_add(checks, "FCFS", json_object_new_boolean(fc_pass));
    json_object_object_add(checks, "SJF", json_object_new_boolean(sj_pass));
    json_object_object_add(claims, "question", json_object_new_string(research_question));
    json_object_object_add(claims, "recommendation", json_object_new_string(fc_pass ? "FCFS" : sj_pass ? "SJF" : "neither qualifies"));
    json_object_object_add(claims, "constraint_checks", checks);
    json_object_object_add(claims, "limits", ex_strings(research_limits, 5));
    json_object_object_add(claims, "competing_explanations", ex_strings(research_explanations, 2));
    json_object_object_add(policies, "FCFS", fcfs); json_object_object_add(policies, "SJF", sjf);
    json_object_object_add(report, "schema_version", json_object_new_int(3));
    json_object_object_add(report, "question", json_object_new_string(research_question));
    json_object_object_add(report, "provenance", research_provenance(data.hash));
    json_object_object_add(report, "policies", policies); json_object_object_add(report, "claims", claims);
    json_object_object_add(report, "claim_locations", ex_strings(research_locations, 3));
    json_object_object_add(report, "limitations", ex_strings(research_limits, 5));
    int result = ex_write("report.json", report); json_object_put(report); return result ? 2 : 0;
}
