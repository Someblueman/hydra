#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include "fleet/plan/plan.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define METRIC_ATTEMPTS 4096U
struct metric { unsigned eligible, known; int64_t sum; bool incomplete; };
struct metrics { struct metric receiver, manual, transfer; unsigned attempts; };

/* A missing, changing, over-limit or linked record cannot establish a zero. */
static char *read_record(const char *directory, const char *name, size_t limit) {
    char path[F_PATH]; struct stat st; char *bytes = NULL; size_t used = 0; int fd;
    if (f_path(path, sizeof(path), directory, name)) return NULL;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022) ||
        st.st_size < 0 || (uint64_t)st.st_size > limit || !(bytes = malloc(limit + 1))) goto bad;
    while (used <= limit) {
        ssize_t n = read(fd, bytes + used, limit + 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) goto bad;
        if (!n) break;
        used += (size_t)n;
    }
    if (used > limit || memchr(bytes, '\0', used)) goto bad;
    bytes[used] = '\0'; close(fd); return bytes;
bad:
    free(bytes); close(fd); return NULL;
}
json_object *wt_metric_record(const char *directory, const char *name, size_t limit) {
    char *bytes = read_record(directory, name, limit);
    json_object *object = bytes && plan_json_unique(bytes) ? f_parse(bytes) : NULL;
    free(bytes); return object;
}
static bool decimal(char *text, uint64_t maximum, uint64_t *value) {
    uint64_t total = 0;
    if (!text || !*text) return false;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        unsigned digit = (unsigned)(*p - '0');
        if (digit > maximum || total > (maximum - digit) / 10) return false;
        total = total * 10 + digit;
    }
    *value = total; return true;
}
static bool scalar_number(const char *directory, const char *name, uint64_t maximum, uint64_t *value) {
    char *text = read_record(directory, name, 32); bool valid = false;
    if (text) {
        size_t n = strlen(text);
        if (n && text[n - 1] == '\n') text[n - 1] = '\0';
        valid = decimal(text, maximum, value);
    }
    free(text); return valid;
}
static void add(struct metric *metric, int64_t value) {
    if (value < 0 || metric->sum > INT64_MAX - value) { metric->incomplete = true; return; }
    metric->sum += value; metric->known++;
}
static bool same_string(json_object *a, json_object *b, const char *field) {
    const char *left = f_string(a, field), *right = f_string(b, field);
    return left && right && !strcmp(left, right);
}
static void receiver_metric(const char *remote, struct metric *metric) {
    json_object *observation = wt_metric_record(remote, "observation.json", 131072);
    json_object *receipt = wt_metric_record(remote, "receipt.json", 131072);
    json_object *runtime = f_field(observation, "runtime");
    const char *id = f_string(observation, "task_id"), *state = f_string(runtime, "state");
    bool valid = id && !strncmp(id, "task_", 5) && task_hex(id + 5, 64) &&
        task_hex(f_string(observation, "spec_sha256"), 64) && same_string(observation, receipt, "task_id") &&
        same_string(observation, receipt, "spec_sha256") && same_string(observation, receipt, "submission_key") &&
        task_runtime_valid(runtime);
    if (valid) add(metric, !strcmp(state, "outcome_unknown"));
    json_object_put(receipt); json_object_put(observation);
}
static void attempt_metrics(const char *step, unsigned number, struct metrics *metrics) {
    char path[F_PATH], name[40]; struct wt_transfer transfer = {0}; int parent, attempt, remote;
    metrics->receiver.eligible++; metrics->transfer.eligible++;
    parent = open(step, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0) return;
    snprintf(name, sizeof(name), "attempt-%u", number);
    attempt = task_owned_directory(parent, name, false); close(parent);
    if (attempt < 0) return;
    remote = task_owned_directory(attempt, "remote", false); close(attempt);
    if (remote < 0) return;
    close(remote);
    if (snprintf(path, sizeof(path), "%s/%s/remote", step, name) >= (int)sizeof(path)) return;
    receiver_metric(path, &metrics->receiver);
    if (wt_transfer_read(path, &transfer) && transfer.complete && transfer.requests <= INT64_MAX - transfer.responses)
        add(&metrics->transfer, transfer.requests + transfer.responses);
}
static bool step_metrics(const char *run, const char *id, struct metrics *metrics) {
    char steps[F_PATH], step[F_PATH]; uint64_t count; int root, child;
    if (!wd_name(id) || f_path(steps, sizeof(steps), run, "steps") || f_path(step, sizeof(step), steps, id)) return false;
    root = open(steps, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) return false;
    child = task_owned_directory(root, id, false); close(root);
    if (child < 0) return false;
    close(child);
    if (!scalar_number(step, "attempts", METRIC_ATTEMPTS, &count) || count > METRIC_ATTEMPTS - metrics->attempts) return false;
    metrics->attempts += (unsigned)count;
    for (unsigned attempt = 1; attempt <= count; attempt++) attempt_metrics(step, attempt, metrics);
    return true;
}
static void task_metrics(const char *run, struct metrics *metrics) {
    json_object *bindings = wt_metric_record(run, "tasks.json", F_LIMIT), *steps = f_field(bindings, "steps");
    bool valid = f_number_is(bindings, "schema_version", 1) && json_object_is_type(steps, json_type_object) &&
        (unsigned)json_object_object_length(steps) <= WD_STEPS;
    if (valid) {
        json_object_object_foreach(steps, id, binding) {
            (void)binding;
            if (!step_metrics(run, id, metrics)) { valid = false; break; }
        }
    }
    if (!valid) { metrics->receiver.incomplete = true; metrics->transfer.incomplete = true; }
    json_object_put(bindings);
}
static bool valid_event(json_object *event, const char *run, unsigned sequence) {
    return f_number_is(event, "schema_version", 1) && f_number_is(event, "sequence", sequence) &&
        f_string(event, "run_id") && !strcmp(f_string(event, "run_id"), run) &&
        f_string(event, "occurred_at") && f_string(event, "type") && f_string(event, "detail") &&
        (sequence != 1 || !strcmp(f_string(event, "type"), "run.created"));
}
static bool event_count(char *text, const char *run, int64_t *count) {
    unsigned sequence = 0; char *line = text;
    while (*line) {
        char *next = strchr(line, '\n');
        if (!next || ++sequence > 16384) return false;
        *next++ = '\0'; json_object *event = plan_json_unique(line) ? f_parse(line) : NULL;
        bool valid = valid_event(event, run, sequence); const char *type = f_string(event, "type");
        if (valid && (!strcmp(type, "run.cancel_requested") || !strcmp(type, "approval.decided") ||
                      !strcmp(type, "run.resume_requested"))) (*count)++;
        json_object_put(event); if (!valid) return false;
        line = next;
    }
    return sequence > 0;
}
static void manual_metrics(const char *run, struct metric *manual) {
    char *id = read_record(run, "run-id", 128), *events = read_record(run, "events.jsonl", 1024U * 1024U);
    int64_t count = 0; manual->eligible = 1;
    if (!id || !events) goto done;
    id[strcspn(id, "\n")] = '\0';
    if (f_name(id) && event_count(events, id, &count)) add(manual, count);
done:
    free(id); free(events);
}
static const char *metric_state(const struct metric *metric) {
    if (metric->incomplete) return "unknown";
    if (!metric->eligible) return "unavailable";
    if (metric->known != metric->eligible) return metric->known ? "partial" : "unknown";
    return "known";
}
static json_object *metric_view(const struct metric *metric) {
    json_object *out = json_object_new_object();
    f_string_add(out, "state", metric_state(metric));
    json_object_object_add(out, "eligible", json_object_new_int64(metric->eligible));
    json_object_object_add(out, "known", json_object_new_int64(metric->known));
    json_object_object_add(out, "sum", !strcmp(metric_state(metric), "known") ? json_object_new_int64(metric->sum) : NULL);
    return out;
}
static struct metrics collect(const char *run) {
    struct metrics metrics = {0}; struct stat st; char path[F_PATH];
    if (lstat(run, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022) ||
        f_path(path, sizeof(path), run, "retention.json") || !lstat(path, &st)) {
        metrics.receiver.incomplete = metrics.manual.incomplete = metrics.transfer.incomplete = true;
        return metrics;
    }
    task_metrics(run, &metrics); manual_metrics(run, &metrics.manual); return metrics;
}
json_object *wt_metrics(const char *run) {
    struct metrics metrics = collect(run); json_object *out = json_object_new_object();
    json_object_object_add(out, "unknown_receiver_outcomes", metric_view(&metrics.receiver));
    json_object_object_add(out, "recorded_operator_actions", metric_view(&metrics.manual));
    json_object_object_add(out, "transport_stdio_bytes", metric_view(&metrics.transfer));
    return f_success("workflow-task-metrics", out);
}
static void metric_tsv(const char *name, const struct metric *metric) {
    printf("%s\t%s\t%u\t%u\t", name, metric_state(metric), metric->eligible, metric->known);
    if (!strcmp(metric_state(metric), "known")) printf("%lld\n", (long long)metric->sum);
    else puts("-");
}
int wt_metrics_tsv(const char *run) {
    struct metrics metrics = collect(run);
    metric_tsv("unknown_receiver_outcomes", &metrics.receiver);
    metric_tsv("recorded_operator_actions", &metrics.manual);
    metric_tsv("transport_stdio_bytes", &metrics.transfer); return ferror(stdout) ? 1 : 0;
}
