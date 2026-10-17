#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Remote evidence of a workflow run's task steps for statistics schema 5:
 * one `remote` row per task step whose latest attempt was dispatched, read
 * only from the records the fleet protocol wrote (dispatch binding, receipt
 * and observation, telemetry block 1 in the receipt runtime, transport
 * metrics, placement record), and one `usage` row when the collected result
 * carries the remote exec receipt. Every value is "-" unless the record
 * supplies it; a missing or unverifiable record never establishes a zero. */
#define COHORT_FIELD 79U
#define COHORT_ATTEMPTS 4096U

static void text(const char *value) {
    size_t i;
    if (!value || !*value) { fputs("\t-", stdout); return; }
    putchar('\t');
    for (i = 0; value[i] && i < COHORT_FIELD; i++)
        putchar((unsigned char)value[i] < 32U || value[i] == 127 || value[i] == '\t' ? '?' : value[i]);
}
static void number(bool known, int64_t value) {
    if (known && value >= 0) printf("\t%lld", (long long)value); else fputs("\t-", stdout);
}
static bool integer(json_object *object, const char *key, int64_t *out) {
    json_object *value = f_field(object, key);
    if (!json_object_is_type(value, json_type_int) || json_object_get_int64(value) < 0) return false;
    *out = json_object_get_int64(value); return true;
}
/* The reason column names why telemetry fields are unknown: no block, or the
 * owner's own reason per field, joined and bounded. */
static void reasons(json_object *telemetry, char out[COHORT_FIELD + 1]) {
    json_object *unmeasured = f_field(telemetry, "unmeasured"); size_t used = 0;
    out[0] = '\0';
    if (!telemetry) { snprintf(out, COHORT_FIELD + 1, "no_telemetry_block"); return; }
    if (!json_object_is_type(unmeasured, json_type_object)) return;
    json_object_object_foreach(unmeasured, key, value) {
        const char *why = f_text(value); int n;
        if (!why) continue;
        n = snprintf(out + used, COHORT_FIELD + 1 - used, "%s%s=%s", used ? "," : "", key, why);
        if (n < 0 || (size_t)n >= COHORT_FIELD + 1 - used) { out[used] = '\0'; break; }
        used += (size_t)n;
    }
}
static bool same(json_object *a, json_object *b, const char *key) {
    const char *left = f_string(a, key), *right = f_string(b, key);
    return left && right && !strcmp(left, right);
}
static bool verified(json_object *observation, json_object *receipt) {
    const char *id = f_string(observation, "task_id");
    return id && !strncmp(id, "task_", 5) && task_hex(id + 5, 64) && task_hex(f_string(observation, "spec_sha256"), 64) &&
        same(observation, receipt, "task_id") && same(observation, receipt, "spec_sha256") &&
        same(observation, receipt, "submission_key") && task_runtime_valid(f_field(observation, "runtime"));
}
static void placement(json_object *observation, bool valid, const char *host, char mode[8], int64_t *generation, bool *generation_known) {
    json_object *record = valid ? placement_record_read(f_string(observation, "task_id")) : NULL;
    const char *recorded = f_string(record, "mode");
    /* A workflow binding pins its destination; only a placement record can say auto. */
    snprintf(mode, 8, "%s", recorded && !strcmp(recorded, "auto") ? "auto" : "pinned");
    *generation_known = valid && integer(observation, "assignment_generation", generation) && *generation > 0;
    if (!*generation_known && record && integer(record, "generation", generation) && *generation > 0) *generation_known = true;
    if (!*generation_known && record && recorded && host && !strcmp(recorded, host)) { *generation = 1; *generation_known = true; }
    json_object_put(record);
}
static bool hex_decode(const char *hex, size_t bytes, char *out) {
    size_t i;
    for (i = 0; i < bytes; i++) {
        unsigned value = 0, j;
        for (j = 0; j < 2; j++) {
            char c = hex[i * 2 + j];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') value |= (unsigned)(c - 'a' + 10);
            else return false;
        }
        out[i] = (char)value;
    }
    out[bytes] = '\0'; return true;
}
static void count(json_object *usage, const char *key) {
    int64_t value = 0;
    bool known = integer(usage, key, &value);
    number(known, value);
}
/* The remote exec receipt (agent.json) collected with the result. */
static void usage_row(const char *remote, const char *step) {
    json_object *envelope = wt_metric_record(remote, "result.json", TASK_PACKAGE_LIMIT + 1024), *files = f_field(f_field(envelope, "result"), "evidence");
    json_object *agent = NULL, *usage; size_t i; char *decoded = NULL;
    for (i = 0; !agent && json_object_is_type(files, json_type_array) && i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); const char *path = f_string(file, "path"), *hex = f_string(file, "hex"); int64_t bytes;
        if (!path || strncmp(path, "exec/", 5) || strlen(path) < 11 || strcmp(path + strlen(path) - 11, "/agent.json") ||
            !hex || !integer(file, "bytes", &bytes) || bytes > (int64_t)TASK_FILE_LIMIT || strlen(hex) != (size_t)bytes * 2 ||
            !(decoded = malloc((size_t)bytes + 1))) continue;
        if (hex_decode(hex, (size_t)bytes, decoded)) agent = f_parse(decoded);
        free(decoded); decoded = NULL;
    }
    if (agent) {
        json_object *cost = f_field(f_field(agent, "usage"), "cost_usd"); double usd;
        usage = f_field(agent, "usage");
        fputs("usage", stdout); text(step); text(f_string(agent, "profile")); text(f_string(f_field(agent, "probe"), "executable_version"));
        text(f_string(agent, "model")); text(f_string(agent, "effort"));
        count(usage, "input_tokens"); count(usage, "cached_input_tokens"); count(usage, "output_tokens");
        usd = cost ? json_object_get_double(cost) : -1.0;
        if (cost && isfinite(usd) && usd >= 0 && usd < 1e9) printf("\t%lld", (long long)llround(usd * 1e6)); else fputs("\t-", stdout);
        putchar('\n');
    }
    json_object_put(agent); json_object_put(envelope);
}
static void remote_row(const char *remote, const char *step, json_object *destination) {
    json_object *observation = wt_metric_record(remote, "observation.json", 131072), *receipt = wt_metric_record(remote, "receipt.json", 131072);
    json_object *runtime = f_field(observation, "runtime"), *telemetry = f_field(runtime, "telemetry");
    const char *kind = f_string(destination, "kind"), *alias = f_string(destination, "alias");
    bool valid = verified(observation, receipt), generation_known, wall_known, cpu_known, rss_known; int64_t generation = 0, wall = 0, user = 0, sys = 0, rss = 0; char mode[8], why[COHORT_FIELD + 1];
    struct wt_transfer transfer = {0};
    const char *host = kind && !strcmp(kind, "local") ? "local" : alias;
    if (telemetry && !f_number_is(telemetry, "schema_version", 1)) telemetry = NULL;
    if (!valid) telemetry = NULL;
    placement(observation, valid, host, mode, &generation, &generation_known);
    fputs("remote", stdout); text(step); text(host); text(kind); text(mode); number(generation_known, generation);
    text(valid ? f_string(runtime, "state") : NULL);
    wall_known = integer(telemetry, "wall_seconds", &wall);
    cpu_known = integer(telemetry, "cpu_user_microseconds", &user) && integer(telemetry, "cpu_system_microseconds", &sys) && user <= INT64_MAX - sys;
    rss_known = integer(telemetry, "peak_rss_bytes", &rss);
    number(wall_known, wall); number(cpu_known, user + sys); number(rss_known, rss);
    if (wt_transfer_read(remote, &transfer) && transfer.complete && transfer.requests <= INT64_MAX - transfer.responses) {
        number(true, transfer.calls); number(true, transfer.requests + transfer.responses);
    } else { fputs("\t-\t-", stdout); }
    if (valid) reasons(telemetry, why); else snprintf(why, sizeof(why), "receipt_unverified");
    text(why);
    putchar('\n');
    if (valid) usage_row(remote, step);
    json_object_put(receipt); json_object_put(observation);
}
static bool decimal(const char *text_value, uint64_t maximum, uint64_t *value) {
    uint64_t total = 0;
    if (!text_value || !*text_value) return false;
    for (const char *p = text_value; *p; p++) {
        unsigned digit = (unsigned)(*p - '0');
        if (*p < '0' || *p > '9' || digit > maximum || total > (maximum - digit) / 10) return false;
        total = total * 10 + digit;
    }
    *value = total; return true;
}
static bool latest_attempt(const char *step, unsigned *attempts) {
    char path[F_PATH], *text_value; uint64_t value; bool ok = false;
    if (f_path(path, sizeof(path), step, "attempts") || !(text_value = f_read(path, 32))) return false;
    text_value[strcspn(text_value, "\n")] = '\0';
    if (decimal(text_value, COHORT_ATTEMPTS, &value)) { *attempts = (unsigned)value; ok = true; }
    free(text_value); return ok;
}
static void step_row(const char *run, const char *id, json_object *binding) {
    char steps[F_PATH], step[F_PATH], name[40], remote[F_PATH]; unsigned attempts; int root, child, attempt, directory;
    if (!wd_name(id) || f_path(steps, sizeof(steps), run, "steps") || f_path(step, sizeof(step), steps, id)) return;
    root = open(steps, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) return;
    child = task_owned_directory(root, id, false); close(root);
    if (child < 0) return;
    if (!latest_attempt(step, &attempts) || !attempts) { close(child); return; }
    snprintf(name, sizeof(name), "attempt-%u", attempts);
    attempt = task_owned_directory(child, name, false); close(child);
    if (attempt < 0) return;
    directory = task_owned_directory(attempt, "remote", false); close(attempt);
    if (directory < 0) return;
    close(directory);
    if (snprintf(remote, sizeof(remote), "%s/%s/remote", step, name) >= (int)sizeof(remote)) return;
    remote_row(remote, id, f_field(binding, "destination"));
}
int wt_remote_tsv(const char *run) {
    json_object *bindings = wt_metric_record(run, "tasks.json", F_LIMIT), *steps = f_field(bindings, "steps");
    if (f_number_is(bindings, "schema_version", 1) && json_object_is_type(steps, json_type_object) &&
        (unsigned)json_object_object_length(steps) <= WD_STEPS) {
        json_object_object_foreach(steps, id, binding) step_row(run, id, binding);
    }
    json_object_put(bindings);
    return ferror(stdout) ? 1 : 0;
}
