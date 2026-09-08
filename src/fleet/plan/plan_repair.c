#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int scalar(const char *directory, const char *name, const char *value) {
    char path[F_PATH];
    return f_path(path, sizeof(path), directory, name) || f_write(path, value, strlen(value), true) || task_sync_dir(directory) ? -1 : 0;
}
static int round_number(const char *run) {
    char path[F_PATH], *text, *end; int round = -1;
    if (f_path(path, sizeof(path), run, "repair-round")) return -1;
    text = f_read(path, 16);
    if (!text) return errno == ENOENT ? 1 : -1;
    long value = strtol(text, &end, 10);
    if (value >= 1 && value <= 11 && end != text && !strcmp(end, "\n")) round = (int)value;
    free(text); return round;
}
static json_object *read_record(const char *run, int round) {
    char path[F_PATH], digest[65]; json_object *record = NULL;
    if (snprintf(path, sizeof(path), "%s/repair-%d.json", run, round) >= (int)sizeof(path)) return NULL;
    record = plan_read(path);
    if (!record || !f_number_is(record, "attempt", round) || !f_string(record, "sha256") ||
        plan_digest(f_field(record, "evidence"), digest) || strcmp(digest, f_string(record, "sha256"))) {
        json_object_put(record); return NULL;
    }
    return record;
}
static bool accepted(json_object *compiled, json_object *record) {
    char digest[65]; const char *bound = f_string(f_field(record, "evidence"), "plan_sha256");
    return bound && !plan_digest(compiled, digest) && !strcmp(bound, digest);
}
static json_object *current_record(const char *run, json_object *compiled, int round) {
    json_object *record = read_record(run, round);
    if (!record || !accepted(compiled, record)) { json_object_put(record); return NULL; }
    return record;
}
static int reset_steps(const char *run, json_object *compiled, int round) {
    json_object *steps = f_field(f_field(compiled, "plan"), "steps"); char directory[F_PATH], number[16];
    snprintf(number, sizeof(number), "%d\n", round);
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        const char *id = f_string(json_object_array_get_idx(steps, i), "id");
        if (snprintf(directory, sizeof(directory), "%s/steps/%s", run, id) >= (int)sizeof(directory) ||
            scalar(directory, "attempts", number) || scalar(directory, "state", "queued\n")) return -1;
    }
    return scalar(run, "repair-round", number);
}
static int apply(const char *run, json_object *compiled, json_object *pending) {
    int round = json_object_get_int(f_field(pending, "attempt")), current = round_number(run), status = -1;
    json_object *record = current_record(run, compiled, round); char path[F_PATH];
    int budget = json_object_get_int(f_field(f_field(f_field(compiled, "plan"), "envelope"), "repair_budget"));
    if (round < 2 || round > budget + 1 || (current != round - 1 && current != round) ||
        !record || !json_object_equal(record, pending) || reset_steps(run, compiled, round) ||
        f_path(path, sizeof(path), run, "repair-pending.json") || unlink(path) || task_sync_dir(run)) goto done;
    status = 0;
done:
    json_object_put(record); return status;
}
static int prepare(const char *run, json_object *compiled, int round) {
    json_object *evidence = plan_repair_evidence(run, compiled), *record = NULL, *existing = NULL;
    char digest[65], name[64], path[F_PATH]; int status = -1;
    if (!evidence) return 0;
    if (plan_digest(evidence, digest)) goto done;
    record = json_object_new_object(); json_object_object_add(record, "attempt", json_object_new_int(round + 1));
    json_object_object_add(record, "evidence", json_object_get(evidence)); f_string_add(record, "sha256", digest);
    const char *text = json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN);
    if (strlen(text) > PLAN_LIMIT) goto done;
    snprintf(name, sizeof(name), "repair-%d.json", round + 1);
    if (f_path(path, sizeof(path), run, name)) goto done;
    if (!access(path, F_OK)) {
        existing = read_record(run, round + 1);
        if (!existing || !json_object_equal(record, existing)) goto done;
    } else if (errno != ENOENT || task_write_json(run, name, record, false)) goto done;
    if (task_write_json(run, "repair-pending.json", record, false) || apply(run, compiled, record)) goto done;
    status = 1;
done:
    json_object_put(existing); json_object_put(record); json_object_put(evidence); return status;
}
static int next_round(const char *run, json_object *compiled) {
    char path[F_PATH];
    if (f_path(path, sizeof(path), run, "cancel-requested")) return -1;
    if (!access(path, F_OK)) return 0;
    if (errno != ENOENT) return -1;
    int round = round_number(run), budget = json_object_get_int(f_field(f_field(f_field(compiled, "plan"), "envelope"), "repair_budget"));
    if (round < 1) return -1;
    return round > budget ? 0 : prepare(run, compiled, round);
}
/* 1 means a new round was prepared, 0 means no eligible repair, -1 is corrupt
 * repair state. Pending resets are completed before any worker can start. */
int plan_repair(const char *run, bool resume) {
    json_object *compiled = plan_run_definition(run), *pending = NULL; char path[F_PATH]; int status = -1;
    if (!compiled) goto done;
    if (!f_number_is(f_field(compiled, "plan"), "schema_version", 2)) { status = 0; goto done; }
    if (f_path(path, sizeof(path), run, "repair-pending.json")) goto done;
    if (!access(path, F_OK)) {
        pending = plan_read(path); if (pending && !apply(run, compiled, pending)) status = 1;
        goto done;
    }
    if (errno != ENOENT) goto done;
    if (resume) { status = 0; goto done; }
    status = next_round(run, compiled);
done:
    json_object_put(pending); json_object_put(compiled); return status;
}
int plan_task_attempt(const char *run) {
    char path[F_PATH]; int round = round_number(run), status = -1;
    json_object *compiled = NULL, *record = NULL;
    if (round < 1 || f_path(path, sizeof(path), run, "compiled.json")) return -1;
    if (access(path, F_OK)) return errno == ENOENT && round == 1 ? 1 : -1;
    compiled = plan_run_definition(run);
    if (!compiled || !f_number_is(f_field(compiled, "plan"), "schema_version", 2)) goto done;
    int budget = json_object_get_int(f_field(f_field(f_field(compiled, "plan"), "envelope"), "repair_budget"));
    if (round > budget + 1 || f_path(path, sizeof(path), run, "repair-pending.json") || !access(path, F_OK) || errno != ENOENT) goto done;
    if (round > 1 && !(record = current_record(run, compiled, round))) goto done;
    status = round;
done:
    json_object_put(record); json_object_put(compiled); return status;
}
int plan_repair_write(const char *run, const char *directory, const char *name) {
    json_object *compiled = plan_run_definition(run), *context = NULL, *record = NULL; int status = -1, round = round_number(run);
    if (!compiled || !f_number_is(f_field(compiled, "plan"), "schema_version", 2) || round < 1) goto done;
    if (round > 1 && !(record = current_record(run, compiled, round))) goto done;
    context = json_object_new_object(); json_object_object_add(context, "schema_version", json_object_new_int(1));
    json_object_object_add(context, "attempt", json_object_new_int(round));
    json_object_object_add(context, "previous", json_object_get(f_field(record, "evidence")));
    status = task_write_json(directory, name, context, false);
done:
    json_object_put(record); json_object_put(context); json_object_put(compiled); return status;
}
bool plan_repair_fresh(const char *run, json_object *check, const char *subject) {
    int round = round_number(run); json_object *compiled = NULL, *record = NULL; bool fresh = false;
    if (round == 1) return true;
    if (round < 1 || !(compiled = plan_run_definition(run)) || !(record = current_record(run, compiled, round))) goto done;
    json_object *failures = f_field(f_field(record, "evidence"), "failures");
    fresh = json_object_is_type(failures, json_type_object);
    json_object_object_foreach(failures, id, failure) {
        (void)id;
        const char *delivery = f_string(failure, "deliverable"), *previous = f_string(failure, "subject_sha256");
        if (!delivery || !previous || (!strcmp(delivery, f_string(check, "deliverable")) && !strcmp(previous, subject))) fresh = false;
    }
done:
    json_object_put(record); json_object_put(compiled); return fresh;
}
