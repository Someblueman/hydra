#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include "example_performance.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static json_object *load_observation(void) {
    double values[3];
    if (getloadavg(values, 3) != 3)
        return NULL;
    json_object *result = json_object_new_array();
    for (size_t i = 0; i < 3; i++)
        json_object_array_add(result, json_object_new_double(values[i]));
    return result;
}
static int64_t elapsed(const struct timespec *start, const struct timespec *end) {
    return (int64_t)(end->tv_sec - start->tv_sec) * INT64_C(1000000000) + end->tv_nsec -
           start->tv_nsec;
}
static json_object *sample(json_object *command, const char *name, int trial, int position,
                           bool warmup, json_object *failures) {
    struct timespec start, end;
    struct f_capture cap = {0};
    char *args[4] = {0};
    for (size_t i = 0; i < 3; i++)
        args[i] = (char *)f_text(json_object_array_get_idx(command, i));
    if (clock_gettime(CLOCK_MONOTONIC, &start))
        return NULL;
    int execution = f_run(args, NULL, 0, 10, &cap);
    if (clock_gettime(CLOCK_MONOTONIC, &end)) {
        f_capture_free(&cap);
        return NULL;
    }
    char count[129] = "", error[1025] = "";
    int code = cap.timeout ? 124 : execution ? 125 : cap.status;
    const char *text = cap.out ? cap.out : "";
    size_t n = cap.out ? cap.out_bytes : 0;
    while (n && isspace((unsigned char)*text)) { text++; n--; }
    while (n && isspace((unsigned char)text[n - 1]))
        n--;
    bool ok = code == 0 && n == 4 && !memcmp(text, "4003", 4);
    if (n > 128)
        n = 128;
    memcpy(count, text, n);
    count[n] = 0;
    snprintf(error, sizeof error, "%s", cap.timeout ? "sample timeout" : cap.err ? cap.err : "");
    json_object *row = json_object_new_object();
    if (!warmup) {
        char id[64];
        snprintf(id, sizeof id, "%02d-%s", trial, name);
        f_string_add(row, "sample_id", id);
    }
    f_string_add(row, "implementation", name);
    json_object_object_add(row, warmup ? "warmup" : "trial", json_object_new_int(trial));
    if (!warmup)
        json_object_object_add(row, "order", json_object_new_int(position));
    json_object_object_add(row, "elapsed_ns", json_object_new_int64(elapsed(&start, &end)));
    f_string_add(row, "status", ok ? "ok" : "fail");
    json_object_object_add(row, "count", json_object_new_string_len(count, (int)n));
    json_object_object_add(row, "returncode", json_object_new_int(code));
    if (!ok) {
        json_object *failure = plan_canonical(row);
        f_string_add(failure, "phase", warmup ? "warmup" : "trial");
        f_string_add(failure, "stderr", error);
        json_object_array_add(failures, failure);
    }
    f_capture_free(&cap);
    return row;
}
static int csv(const char *path, json_object *rows) {
    FILE *stream = fopen(path, "w");
    if (!stream)
        return -1;
    fputs("sample_id,implementation,trial,order,elapsed_ns,status,count,returncode\n", stream);
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        fprintf(stream, "%s,%s,%d,%d,%lld,%s,", f_string(row, "sample_id"),
                f_string(row, "implementation"), json_object_get_int(f_field(row, "trial")),
                json_object_get_int(f_field(row, "order")),
                (long long)json_object_get_int64(f_field(row, "elapsed_ns")),
                f_string(row, "status"));
        const char *count = json_object_get_string(f_field(row, "count"));
        size_t count_size = (size_t)json_object_get_string_len(f_field(row, "count"));
        bool quoted = memchr(count, ',', count_size) || memchr(count, '"', count_size) ||
                      memchr(count, '\r', count_size) || memchr(count, '\n', count_size);
        if (quoted)
            fputc('"', stream);
        for (size_t j = 0; j < count_size; j++) {
            if (count[j] == '"')
                fputc('"', stream);
            fputc((unsigned char)count[j], stream);
        }
        if (quoted)
            fputc('"', stream);
        fprintf(stream, ",%d\n", json_object_get_int(f_field(row, "returncode")));
    }
    int failed = ferror(stream);
    if (fclose(stream))
        failed = 1;
    return failed ? -1 : 0;
}
int ex_performance_measure(void) {
    const char *exclusive = getenv("HYDRA_PERFORMANCE_EXCLUSIVE");
    if (!exclusive || strcmp(exclusive, "1")) {
        fprintf(stderr,
                "A coordinated measurement window requires HYDRA_PERFORMANCE_EXCLUSIVE=1\n");
        return 1;
    }
    char root[F_PATH], records[F_PATH], path[F_PATH];
    json_object *environment = NULL, *manifest = NULL, *commands = NULL, *sources = NULL,
                *warmups = NULL, *rows = NULL, *failures = NULL;
    int status = 1;
    if (!perf_root(root) || ex_path(records, "HYDRA_WORKFLOW_INPUTS_DIR", "records") ||
        !(environment = perf_environment(root)))
        goto cleanup;
    json_object *before = load_observation();
    if (!before)
        goto cleanup;
    json_object_object_add(environment, "load_before", before);
    commands = json_object_new_object();
    sources = json_object_new_object();
    const char *names[] = {"baseline", "candidate"};
    const char *shell = f_string(f_field(f_field(environment, "tools"), "shell"), "path");
    for (size_t i = 0; i < 2; i++) {
        char label[32];
        snprintf(label, sizeof label, "%s.sh", names[i]);
        if (f_path(path, sizeof path, root, label))
            goto cleanup;
        json_object *source = perf_source(path, label);
        if (!source)
            goto cleanup;
        json_object_object_add(sources, names[i], source);
        const char *args[] = {shell, path, records};
        json_object_object_add(commands, names[i], ex_strings(args, 3));
    }
    warmups = json_object_new_array();
    rows = json_object_new_array();
    failures = json_object_new_array();
    for (int trial = 1; trial <= 2; trial++)
        for (int name = 0; name < 2; name++) {
            json_object *row =
                sample(f_field(commands, names[name]), names[name], trial, 0, true, failures);
            if (!row)
                goto cleanup;
            json_object_array_add(warmups, row);
        }
    for (int trial = 1; trial <= 10; trial++)
        for (int position = 0; position < 2; position++) {
            int name = (position + (trial % 2 == 0)) % 2;
            json_object *row = sample(f_field(commands, names[name]), names[name], trial, position,
                                      false, failures);
            if (!row)
                goto cleanup;
            json_object_array_add(rows, row);
        }
    json_object *after = load_observation();
    if (!after)
        goto cleanup;
    json_object_object_add(environment, "load_after", after);
    manifest = json_object_new_object();
    json_object_object_add(manifest, "schema_version", json_object_new_int(2));
    json_object *workload = perf_source(records, "records.txt");
    if (!workload)
        goto cleanup;
    json_object_object_add(manifest, "workload", workload);
    json_object_object_add(manifest, "commands", json_object_get(commands));
    json_object_object_add(manifest, "sources", json_object_get(sources));
    json_object_object_add(manifest, "environment", json_object_get(environment));
    json_object_object_add(manifest, "warmups", json_object_get(warmups));
    json_object_object_add(manifest, "failures", json_object_get(failures));
    json_object_object_add(manifest, "protocol", perf_protocol());
    if (ex_path(path, "HYDRA_WORKFLOW_OUTPUTS_DIR", "raw.csv") || csv(path, rows) ||
        ex_write("manifest.json", manifest) || ex_write("environment.txt", environment))
        goto cleanup;
    status = 0;
cleanup:
    json_object_put(environment);
    json_object_put(manifest);
    json_object_put(commands);
    json_object_put(sources);
    json_object_put(warmups);
    json_object_put(rows);
    json_object_put(failures);
    if (status)
        fprintf(stderr, "performance measurement could not preserve its complete record\n");
    return status;
}
