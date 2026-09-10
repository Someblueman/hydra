#include "example_research.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

const char research_question[] = "For this supplied 12-job trace, compare non-preemptive FCFS and SJF under p95 turnaround <=22 and max wait <=16.";
const char *const research_limits[5] = {"12 synthetic jobs", "one server", "exact known durations", "non-preemptive", "no production evidence"};
const char *const research_explanations[2] = {"mean and tail metrics can disagree because a few long waits dominate tails", "observed long wait is not proof of starvation"};
const char *const research_locations[3] = {"/claims/recommendation", "/policies/FCFS", "/policies/SJF"};

void research_free(struct research_data *data) {
    for (size_t i = 0; i < RESEARCH_JOBS; i++) { free(data->jobs[i].id); data->jobs[i].id = NULL; }
}

/* Decode one CSV field in place. Preserve quoted commas, newlines and doubled
 * quotes; return the delimiter separately because decoding overwrites it. */
static char *field(char **cursor, int *delimiter) {
    char *start = *cursor, *read = start, *write = start;
    bool quoted = *read == '"';
    if (quoted) read++;
    for (;;) {
        if (quoted && *read == '"') {
            read++;
            if (*read == '"') { *write++ = *read++; continue; }
            if (*read && *read != ',' && *read != '\r' && *read != '\n') return NULL;
            break;
        }
        if (!*read) { if (quoted) return NULL; break; }
        if (!quoted && (*read == ',' || *read == '\r' || *read == '\n')) break;
        *write++ = *read++;
    }
    *delimiter = (unsigned char)*read;
    if (*read == '\r' && read[1] == '\n') read++;
    if (*read) read++;
    *write = '\0'; *cursor = read;
    return start;
}

static bool number(const char *text, int64_t *value, bool positive) {
    char *end = NULL;
    errno = 0;
    long long parsed = strtoll(text, &end, 10);
    if (end == text || errno) return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end || parsed < (positive ? 1 : 0)) return false;
    *value = (int64_t)parsed;
    return true;
}

int research_load(struct research_data *data) {
    char path[F_PATH]; size_t size = 0;
    memset(data, 0, sizeof(*data));
    if (ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "jobs")) return -1;
    char *source = ex_read(path, 1024 * 1024, &size);
    if (!source) return -1;
    int result = -1;
    if (memchr(source, 0, size) || ex_hash(source, size, data->hash)) goto done;
    char *cursor = source;
    int columns[3] = {-1, -1, -1};
    for (int i = 0; i < 3; i++) {
        int delimiter = 0; char *name = field(&cursor, &delimiter);
        if (!name || (i < 2 ? delimiter != ',' : (delimiter != '\n' && delimiter != '\r'))) goto done;
        int key = !strcmp(name, "id") ? 0 : !strcmp(name, "arrival") ? 1 : !strcmp(name, "duration") ? 2 : -1;
        if (key < 0 || columns[key] >= 0) goto done;
        columns[key] = i;
    }
    for (size_t i = 0; i < RESEARCH_JOBS; i++) {
        while (*cursor == '\r' || *cursor == '\n') cursor++;
        char *values[3];
        for (int j = 0; j < 3; j++) {
            int delimiter = 0; values[j] = field(&cursor, &delimiter);
            if (!values[j] || (j < 2 ? delimiter != ',' : delimiter == ',')) goto done;
        }
        struct research_job *job = &data->jobs[i];
        if (!*values[columns[0]] || !number(values[columns[1]], &job->arrival, false) ||
            !number(values[columns[2]], &job->duration, true)) goto done;
        for (size_t j = 0; j < i; j++) if (!strcmp(data->jobs[j].id, values[columns[0]])) goto done;
        job->id = strdup(values[columns[0]]);
        if (!job->id) goto done;
    }
    while (*cursor == '\r' || *cursor == '\n') cursor++;
    if (*cursor) goto done;
    result = 0;
done:
    free(source);
    if (result) research_free(data);
    return result;
}

json_object *research_provenance(const char *hash) {
    json_object *value = json_object_new_object();
    json_object_object_add(value, "data_path", json_object_new_string("jobs.csv"));
    json_object_object_add(value, "data_sha256", json_object_new_string(hash));
    json_object_object_add(value, "method", json_object_new_string("deterministic non-preemptive simulation; tie arrival then input order"));
    return value;
}
