#include "example_research.h"
#include <limits.h>

/* Checker has its own arrival-ordered pending queue and ready queue. It does
 * not call the producer's selection or metric implementation. */
json_object *research_recompute(const struct research_data *data, bool shortest) {
    size_t pending[RESEARCH_JOBS], ready[RESEARCH_JOBS], nready = 0, next = 0;
    for (size_t i = 0; i < RESEARCH_JOBS; i++) {
        size_t at = i;
        while (at && data->jobs[pending[at - 1]].arrival > data->jobs[i].arrival) {
            pending[at] = pending[at - 1]; at--;
        }
        pending[at] = i;
    }
    json_object *rows = json_object_new_array();
    int64_t clock = 0, wait_sum = 0, turn_sum = 0, worst_wait = -1;
    int64_t turnaround[RESEARCH_JOBS]; const char *worst_id = "";
    for (size_t completed = 0; completed < RESEARCH_JOBS; completed++) {
        if (!nready && next < RESEARCH_JOBS && clock < data->jobs[pending[next]].arrival)
            clock = data->jobs[pending[next]].arrival;
        while (next < RESEARCH_JOBS && data->jobs[pending[next]].arrival <= clock)
            ready[nready++] = pending[next++];
        if (!nready) goto invalid;
        size_t selected = 0;
        if (shortest) for (size_t i = 1; i < nready; i++)
            if (data->jobs[ready[i]].duration < data->jobs[ready[selected]].duration) selected = i;
        const struct research_job *job = &data->jobs[ready[selected]];
        for (size_t i = selected + 1; i < nready; i++) ready[i - 1] = ready[i];
        nready--;
        if (clock > INT64_MAX - job->duration) goto invalid;
        int64_t begin = clock; clock += job->duration;
        int64_t wait = begin - job->arrival, turn = clock - job->arrival;
        if (wait_sum > INT64_MAX - wait || turn_sum > INT64_MAX - turn) goto invalid;
        wait_sum += wait; turn_sum += turn;
        if (wait > worst_wait) { worst_wait = wait; worst_id = job->id; }
        size_t insert = completed;
        while (insert && turnaround[insert - 1] > turn) {
            turnaround[insert] = turnaround[insert - 1]; insert--;
        }
        turnaround[insert] = turn;
        json_object *row = json_object_new_object();
        json_object_object_add(row, "id", json_object_new_string(job->id));
        json_object_object_add(row, "start", json_object_new_int64(begin));
        json_object_object_add(row, "finish", json_object_new_int64(clock));
        json_object_object_add(row, "wait", json_object_new_int64(wait));
        json_object_object_add(row, "turnaround", json_object_new_int64(turn));
        json_object_array_add(rows, row);
    }
    json_object *result = json_object_new_object();
    json_object_object_add(result, "schedule", rows);
    json_object_object_add(result, "mean_wait", json_object_new_double((double)wait_sum / RESEARCH_JOBS));
    json_object_object_add(result, "mean_turnaround", json_object_new_double((double)turn_sum / RESEARCH_JOBS));
    size_t rank = (95 * RESEARCH_JOBS + 99) / 100;
    json_object_object_add(result, "p95_turnaround", json_object_new_int64(turnaround[rank - 1]));
    json_object_object_add(result, "max_wait", json_object_new_int64(worst_wait));
    json_object_object_add(result, "worst_wait_job", json_object_new_string(worst_id));
    return result;
invalid:
    json_object_put(rows); return NULL;
}
