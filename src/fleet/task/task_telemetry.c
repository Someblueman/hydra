#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include <string.h>
#include <sys/resource.h>
#include <time.h>

/* Telemetry block 1 (see docs/FLEET.md "Task telemetry"): what the task owner
 * measured about the execution phase, which is the `hydra workflow run` or
 * `hydra exec` child and every descendant it waited for. Preparation, head
 * spawn and sealing are outside the scope. A field the owner could not
 * measure is absent and named under `unmeasured` with its reason; nothing is
 * ever recorded as zero in its place. Measurements of a resumed task add up
 * over the owner invocations that recorded them. */
#define TELEMETRY_SCHEMA 1

static int64_t monotonic_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec;
}
static int64_t rss_bytes(long maxrss) {
#ifdef __APPLE__
    return (int64_t)maxrss;
#else
    return (int64_t)maxrss * 1024;
#endif
}
void task_telemetry_begin(struct task_telemetry *probe) {
    struct rusage usage;
    memset(probe, 0, sizeof(*probe));
    probe->monotonic = monotonic_now();
    if (getrusage(RUSAGE_CHILDREN, &usage)) return;
    probe->user_us = (int64_t)usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
    probe->system_us = (int64_t)usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
    probe->maxrss = rss_bytes(usage.ru_maxrss);
    probe->valid = probe->monotonic >= 0;
}
/* One field: this invocation's measurement combined with the prior record.
 * A prior invocation that lacked the field makes the total unknown. */
static void field(json_object *out, json_object *unmeasured, json_object *previous, const char *key,
                  bool known, int64_t value, const char *reason, bool maximum) {
    json_object *prior = f_field(previous, key);
    if (previous && !json_object_is_type(prior, json_type_int)) {
        const char *why = f_string(f_field(previous, "unmeasured"), key);
        f_string_add(unmeasured, key, why ? why : "prior_invocation_unmeasured"); return;
    }
    if (!known || value < 0) { f_string_add(unmeasured, key, reason); return; }
    if (prior) {
        int64_t before = json_object_get_int64(prior);
        if (maximum) value = before > value ? before : value;
        else if (before < 0 || value > INT64_MAX - before) { f_string_add(unmeasured, key, "overflow"); return; }
        else value += before;
    }
    json_object_object_add(out, key, json_object_new_int64(value));
}
void task_telemetry_record(json_object *state, const struct task_telemetry *before, bool started, bool reaped) {
    struct task_telemetry after;
    json_object *previous = f_field(state, "telemetry"), *out = json_object_new_object(), *unmeasured = json_object_new_object();
    bool usable = before->valid, measured;
    const char *cpu_reason = !usable ? "owner_measurement_unavailable" : !started ? "execution_not_started" : "child_not_reaped";
    int64_t invocations = 1;
    if (previous && !f_number_is(previous, "schema_version", TELEMETRY_SCHEMA)) previous = NULL;
    if (previous && json_object_is_type(f_field(previous, "invocations"), json_type_int)) invocations += json_object_get_int64(f_field(previous, "invocations"));
    task_telemetry_begin(&after);
    usable = usable && after.valid;
    measured = usable && started && reaped;
    json_object_object_add(out, "schema_version", json_object_new_int(TELEMETRY_SCHEMA));
    f_string_add(out, "measured_by", "task_owner"); f_string_add(out, "scope", "execution_phase");
    f_string_add(out, "clock", "monotonic"); f_string_add(out, "cpu_source", "getrusage_children");
    f_string_add(out, "rss_source", "getrusage_children_maxrss");
    json_object_object_add(out, "invocations", json_object_new_int64(invocations));
    field(out, unmeasured, previous, "wall_seconds", usable && started, after.monotonic - before->monotonic,
          usable ? "execution_not_started" : "owner_measurement_unavailable", false);
    field(out, unmeasured, previous, "cpu_user_microseconds", measured, after.user_us - before->user_us, cpu_reason, false);
    field(out, unmeasured, previous, "cpu_system_microseconds", measured, after.system_us - before->system_us, cpu_reason, false);
    /* ru_maxrss is the largest waited-for descendant so far: the execution
     * child's peak is known only when it raised that maximum. */
    field(out, unmeasured, previous, "peak_rss_bytes", measured && after.maxrss > before->maxrss, after.maxrss,
          measured ? "rss_not_distinguishable" : cpu_reason, true);
    json_object_object_add(out, "unmeasured", unmeasured);
    json_object_object_add(state, "telemetry", out);
}
