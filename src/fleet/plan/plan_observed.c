#include "fleet/plan/plan_inspect.h"
#include "fleet/support/files.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Recorded step durations of one run, emitted as an estimates document bound
 * to the run's compiled digest. Timestamps have one-second resolution, so each
 * duration is the range [d-1, d+1] seconds in milliseconds; a step without a
 * completed attempt is omitted and keeps every aggregate that needs it unknown.
 * Provider cost is never recorded, so no cost rows are produced. */
static long scalar(const char *directory, const char *name) {
    char path[F_PATH], *text, *end; long value = -1;
    if (f_path(path, sizeof(path), directory, name)) return -1;
    text = f_read(path, 32);
    if (!text) return -1;
    errno = 0; value = strtol(text, &end, 10);
    if (errno || end == text || strcmp(end, "\n") || value < 0) value = -1;
    free(text); return value;
}
static json_object *duration(const char *run, const char *id, long *attempts) {
    char step[F_PATH], attempt[F_PATH]; long started, completed, count;
    if (snprintf(step, sizeof(step), "%s/steps/%s", run, id) >= (int)sizeof(step)) return NULL;
    count = scalar(step, "attempts"); started = scalar(step, "started-at");
    if (count < 1 || count > 11 || started < 0 || snprintf(attempt, sizeof(attempt), "%s/attempt-%ld", step, count) >= (int)sizeof(attempt)) return NULL;
    completed = scalar(attempt, "completed-at");
    if (completed < started) return NULL;
    *attempts = count;
    json_object *range = json_object_new_array(); long seconds = completed - started;
    json_object_array_add(range, json_object_new_int64((seconds > 0 ? seconds - 1 : 0) * 1000));
    json_object_array_add(range, json_object_new_int64((seconds + 1) * 1000));
    return range;
}
json_object *plan_observed(const char *run) {
    char path[F_PATH], digest[65], source[512], *id = NULL; json_object *compiled, *steps, *rows, *entry, *plans, *out = NULL;
    size_t recorded = 0; long repeated = 0;
    if (f_path(path, sizeof(path), run, "compiled.json") || !(compiled = pi_read_compiled(path, digest))) return NULL;
    if (!f_path(path, sizeof(path), run, "run-id")) id = f_read(path, 128);
    if (id) id[strcspn(id, "\r\n")] = '\0';
    if (!id || !plan_id(id)) goto done;
    steps = f_field(f_field(compiled, "plan"), "steps"); rows = json_object_new_object();
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        const char *step = f_string(json_object_array_get_idx(steps, i), "id"); long attempts = 0;
        json_object *range = duration(run, step, &attempts), *row;
        if (!range) continue;
        row = json_object_new_object(); json_object_object_add(row, "milliseconds", range);
        json_object_object_add(rows, step, row); recorded++;
        if (attempts > 1) repeated++;
    }
    if (!recorded) { json_object_put(rows); goto done; }
    snprintf(source, sizeof(source), "recorded step durations of run %s: one observation per step at one-second resolution for %zu of %zu steps, %ld with repeated attempts; provider cost not recorded",
             id, recorded, json_object_array_length(steps), repeated);
    entry = json_object_new_object(); f_string_add(entry, "source", source); f_string_add(entry, "cost_unit", "unrecorded");
    json_object_object_add(entry, "steps", rows);
    plans = json_object_new_object(); json_object_object_add(plans, digest, entry);
    out = json_object_new_object(); json_object_object_add(out, "schema_version", json_object_new_int(1));
    json_object_object_add(out, "plans", plans);
done:
    free(id); json_object_put(compiled); return out;
}
