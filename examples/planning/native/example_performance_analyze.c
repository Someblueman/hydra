#include "example_performance.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int compare(const void *left, const void *right) {
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}
static double median(const double *values, size_t count) {
    double copy[10];
    if (count != 10)
        return NAN;
    memcpy(copy, values, sizeof copy);
    qsort(copy, count, sizeof *copy, compare);
    return (copy[4] + copy[5]) / 2.0;
}
static bool trial_value(json_object *row, double *value) {
    const char *text = f_string(row, "elapsed_ns");
    char *end = NULL;
    if (!text || !*text || strspn(text, "0123456789") != strlen(text))
        return false;
    errno = 0;
    long long number = strtoll(text, &end, 10);
    if (errno || !end || *end || number <= 0 || number > PERF_MAX_SAMPLE_NS)
        return false;
    *value = (double)number;
    return ex_text(row, "status", "ok") && ex_text(row, "count", "4003") &&
           ex_text(row, "returncode", "0");
}
static const char *samples(json_object *rows, json_object *manifest, double values[2][10]) {
    if (!rows || json_object_array_length(rows) != 20)
        return "expected exactly twenty complete raw rows";
    for (size_t i = 0; i < 20; i++) {
        size_t trial = i / 2 + 1, position = i % 2, name = (position + (trial % 2 == 0)) % 2;
        const char *label = name ? "candidate" : "baseline";
        json_object *row = json_object_array_get_idx(rows, i);
        char identity[64], number[16], order[4];
        snprintf(identity, sizeof identity, "%02zu-%s", trial, label);
        snprintf(number, sizeof number, "%zu", trial);
        snprintf(order, sizeof order, "%zu", position);
        if (json_object_object_length(row) != 8 || !ex_text(row, "sample_id", identity) ||
            !ex_text(row, "implementation", label) || !ex_text(row, "trial", number) ||
            !ex_text(row, "order", order))
            return "pair identity or alternating order mismatch";
        if (!trial_value(row, &values[name][trial - 1]))
            return "invalid, failed or wrong-result trial";
    }
    json_object *warmups = f_field(manifest, "warmups"), *failures = f_field(manifest, "failures");
    if (!json_object_is_type(warmups, json_type_array) || json_object_array_length(warmups) != 4 ||
        !json_object_is_type(failures, json_type_array) || json_object_array_length(failures))
        return "missing warmups or recorded failures";
    for (size_t i = 0; i < 4; i++) {
        json_object *row = json_object_array_get_idx(warmups, i);
        int64_t warmup, code;
        if (!ex_text(row, "implementation", i % 2 ? "candidate" : "baseline") ||
            !perf_int(f_field(row, "warmup"), &warmup) || warmup != (int64_t)(i / 2 + 1) ||
            !ex_text(row, "status", "ok") || !ex_text(row, "count", "4003") ||
            !perf_int(f_field(row, "returncode"), &code) || code)
            return "invalid warmup";
    }
    return NULL;
}
static json_object *summary(double values[10]) {
    double max = values[0];
    for (size_t i = 1; i < 10; i++)
        if (values[i] > max)
            max = values[i];
    json_object *result = json_object_new_object();
    json_object_object_add(result, "n", json_object_new_int(10));
    json_object_object_add(result, "median_ns", json_object_new_double(median(values, 10)));
    json_object_object_add(result, "p95_ns", json_object_new_int64((int64_t)max));
    return result;
}
static json_object *uncertainty(double values[2][10], bool *established) {
    double ratios[10], draws[10000];
    struct perf_rng rng;
    perf_seed(&rng);
    for (size_t i = 0; i < 10; i++)
        ratios[i] = values[1][i] / values[0][i] - 1.0;
    for (size_t i = 0; i < 10000; i++) {
        double selected[10];
        for (size_t j = 0; j < 10; j++)
            selected[j] = ratios[(size_t)(perf_random(&rng) * 10)];
        draws[i] = median(selected, 10);
    }
    qsort(draws, 10000, sizeof *draws, compare);
    double estimate = median(ratios, 10);
    *established = estimate <= -.10 && draws[9499] < 0;
    json_object *result = f_parse("{\"method\":\"paired bootstrap median relative "
                                  "change\",\"seed\":1729,\"resamples\":10000,\"interval\":\"90% "
                                  "percentile; independent pair assumption\"}");
    json_object_object_add(result, "estimate", json_object_new_double(estimate));
    json_object_object_add(result, "lower", json_object_new_double(draws[499]));
    json_object_object_add(result, "upper", json_object_new_double(draws[9499]));
    return result;
}
int ex_performance_analyze(void) {
    char raw_path[F_PATH];
    size_t size = 0;
    char *raw = NULL;
    json_object *manifest = ex_input("manifest"), *rows = NULL, *lines = NULL, *result = NULL;
    if (!manifest || ex_path(raw_path, "HYDRA_WORKFLOW_INPUTS_DIR", "raw") ||
        !(raw = ex_read(raw_path, F_LIMIT, &size)))
        goto failure;
    bool binary_raw = memchr(raw, 0, size) != NULL;
    if (binary_raw) lines = perf_raw_lines(raw, size);
    else rows = perf_csv(raw, &lines, NULL);
    bool decoded = rows != NULL;
    if (!rows) rows = json_object_new_array();
    result = f_parse("{\"schema_version\":2,\"raw_summary\":{\"rows\":0,\"failures\":[],\"invalid_"
                     "reason\":null},\"baseline\":null,\"candidate\":null,\"paired_uncertainty\":"
                     "null,\"outcome\":\"invalid/insufficient measurement\"}");
    json_object_object_add(result, "manifest", json_object_get(manifest));
    json_object_object_add(result, "limits", perf_limits());
    json_object_object_add(result, "raw_samples", json_object_get(lines));
    json_object *info = f_field(result, "raw_summary"), *failures = f_field(info, "failures");
    json_object_object_add(info, "rows",
                           json_object_new_int64((int64_t)json_object_array_length(rows)));
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!ex_text(row, "status", "ok"))
            json_object_array_add(failures, json_object_get(row));
    }
    double values[2][10];
    const char *error = binary_raw ? "embedded NUL in raw samples" :
                        decoded ? samples(rows, manifest, values) : "malformed CSV record";
    if (error)
        f_string_add(info, "invalid_reason", error);
    else {
        bool established;
        json_object_object_add(result, "baseline", summary(values[0]));
        json_object_object_add(result, "candidate", summary(values[1]));
        json_object_object_add(result, "paired_uncertainty", uncertainty(values, &established));
        f_string_add(result, "outcome",
                     established ? "target established" : "target not established");
    }
    int status = ex_write("report.json", result) ? 1 : 0;
    json_object_put(result);
    json_object_put(lines);
    json_object_put(rows);
    json_object_put(manifest);
    free(raw);
    return status;
failure:
    json_object_put(manifest);
    free(raw);
    fprintf(stderr, "performance input cannot be read\n");
    return 1;
}
