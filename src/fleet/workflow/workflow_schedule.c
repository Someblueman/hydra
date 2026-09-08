#include "fleet/workflow/workflow_schedule.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

static bool state_valid(const char *state) {
    const char *states[] = {"queued", "ready", "running", "retrying", "waiting-approval", "waiting-remote", "succeeded", "failed", "cancelled", "recovery-required"};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) if (state && !strcmp(state, states[i])) return true;
    return false;
}
static int record_time(const char *run, json_object *observations) {
    char path[F_PATH], *text, *end; long long deadline = 0; time_t now = time(NULL);
    if (now < 0 || f_path(path, sizeof(path), run, "plan-deadline")) return -1;
    text = f_read(path, 32);
    if (text) {
        deadline = strtoll(text, &end, 10);
        bool valid = deadline > 0 && end != text && !strcmp(end, "\n"); free(text);
        if (!valid) return -1;
    } else if (errno != ENOENT) return -1;
    json_object_object_add(observations, "observed_at", json_object_new_int64((int64_t)now));
    json_object_object_add(observations, "deadline", json_object_new_int64(deadline)); return 0;
}
json_object *ws_observe(const char *run, json_object *graph) {
    json_object *out = json_object_new_object(), *states = json_object_new_object(); char path[F_PATH];
    json_object_object_add(out, "states", states);
    json_object_object_foreach(graph, id, definition) {
        (void)definition; char *state;
        if (snprintf(path, sizeof(path), "%s/steps/%s/state", run, id) >= (int)sizeof(path) || !(state = f_read(path, 64))) goto bad;
        state[strcspn(state, "\r\n")] = '\0'; bool valid = state_valid(state);
        if (valid) f_string_add(states, id, state);
        free(state); if (!valid) goto bad;
    }
    if (record_time(run, out) || f_path(path, sizeof(path), run, "cancel-requested")) goto bad;
    json_object_object_add(out, "cancelled", json_object_new_boolean(access(path, F_OK) == 0)); return out;
bad:
    json_object_put(out); return NULL;
}
static bool dependencies_done(const char *needs, json_object *states) {
    if (!needs) return false;
    if (!strcmp(needs, "-")) return true;
    while (*needs) {
        const char *end = strchr(needs, ','); size_t length = end ? (size_t)(end - needs) : strlen(needs); char id[65];
        if (!length || length >= sizeof(id)) return false;
        memcpy(id, needs, length); id[length] = '\0';
        const char *state = f_string(states, id);
        if (!state || strcmp(state, "succeeded")) return false;
        if (!end) break;
        needs = end + 1;
    }
    return true;
}
static int running_count(json_object *graph, json_object *states) {
    int running = 0;
    if (!json_object_is_type(graph, json_type_object) || !json_object_object_length(graph) ||
        json_object_object_length(graph) > (int)WD_STEPS || !json_object_is_type(states, json_type_object) ||
        json_object_object_length(states) != json_object_object_length(graph)) return -1;
    json_object_object_foreach(graph, id, definition) {
        (void)definition; const char *state = f_string(states, id);
        if (!wd_name(id) || !state_valid(state)) return -1;
        if (!strcmp(state, "running")) running++;
    }
    return running;
}
static bool time_valid(json_object *observed) {
    return json_object_is_type(f_field(observed, "observed_at"), json_type_int) &&
        json_object_get_int64(f_field(observed, "observed_at")) >= 0 &&
        json_object_is_type(f_field(observed, "deadline"), json_type_int) && json_object_get_int64(f_field(observed, "deadline")) >= 0;
}
static bool expired(json_object *observed) {
    int64_t deadline = json_object_get_int64(f_field(observed, "deadline"));
    return deadline && json_object_get_int64(f_field(observed, "observed_at")) >= deadline;
}
int ws_decide(json_object *graph, json_object *observations, int parallelism, char choice[65]) {
    json_object *states = f_field(observations, "states"); int running = running_count(graph, states);
    choice[0] = '\0';
    if (running < 0 || !time_valid(observations) || !json_object_is_type(f_field(observations, "cancelled"), json_type_boolean) || parallelism < 1 || parallelism > 16) return -1;
    json_object_object_foreach(graph, id, definition) {
        const char *state = f_string(states, id);
        if (!*choice && !strcmp(state, "ready") && dependencies_done(f_string(definition, "needs"), states)) strcpy(choice, id);
    }
    if (running >= parallelism || expired(observations) || json_object_get_boolean(f_field(observations, "cancelled"))) choice[0] = '\0';
    return 0;
}
