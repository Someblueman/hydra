#include "example_performance.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The checker reconstructs the protocol and estimates independently. It does
 * not call the analyzer or accept its summary as the source of expected data. */
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            error = message;                                                                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static bool count_fields(json_object *object, size_t count) {
    return json_object_is_type(object, json_type_object) &&
           (size_t)json_object_object_length(object) == count;
}
static bool integer_is(json_object *value, int64_t expected) {
    int64_t actual;
    return perf_int(value, &actual) && actual == expected;
}
static bool number_is(json_object *value, double expected) {
    return (json_object_is_type(value, json_type_int) ||
            json_object_is_type(value, json_type_double)) &&
           isfinite(json_object_get_double(value)) && json_object_get_double(value) == expected;
}
static bool list_size(json_object *value, size_t count) {
    return json_object_is_type(value, json_type_array) && json_object_array_length(value) == count;
}
static const char *manifest_check(json_object *manifest, const char *root) {
    const char *error = NULL;
    char path[F_PATH];
    json_object *expected = NULL, *environment = NULL, *protocol = NULL;
    char *records = NULL;
    size_t bytes = 0;
    CHECK(count_fields(manifest, 8) && integer_is(f_field(manifest, "schema_version"), 2),
          "manifest fields/version");
    CHECK(!ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "records"), "workload input");
    expected = perf_source(path, "records.txt");
    CHECK(expected && ex_equal(f_field(manifest, "workload"), expected), "workload binding");
    records = ex_read(path, F_LIMIT, &bytes);
    CHECK(records && bytes && records[bytes - 1] == '\n', "workload bytes");
    size_t lines = 0;
    for (size_t i = 0; i < bytes; i++)
        if (records[i] == '\n')
            lines++;
    CHECK(lines == 4003, "workload line-count contract");
    json_object_put(expected);
    expected = NULL;
    json_object *sources = f_field(manifest, "sources");
    CHECK(count_fields(sources, 2), "source set");
    const char *names[] = {"baseline", "candidate"};
    for (size_t i = 0; i < 2; i++) {
        char label[32];
        snprintf(label, sizeof label, "%s.sh", names[i]);
        CHECK(!f_path(path, sizeof path, root, label), "source path");
        expected = perf_source(path, label);
        CHECK(expected && ex_equal(f_field(sources, names[i]), expected), "source binding");
        json_object_put(expected);
        expected = NULL;
    }
    protocol = perf_protocol();
    CHECK(ex_equal(f_field(manifest, "protocol"), protocol), "fixed protocol binding");
    environment = perf_environment(root);
    CHECK(environment, "current toolchain");
    json_object *observed = f_field(manifest, "environment");
    CHECK(count_fields(observed, 10), "environment fields");
    const char *same[] = {"toolchain", "platform", "machine", "timer", "units", "source_git"};
    for (size_t i = 0; i < sizeof same / sizeof *same; i++)
        CHECK(ex_equal(f_field(environment, same[i]), f_field(observed, same[i])),
              "environment/source identity");
    const char *cwd = f_string(observed, "cwd");
    CHECK(cwd && cwd[0] == '/', "measurement working directory");
    json_object *actual_tools = f_field(observed, "tools"),
                *expected_tools = f_field(environment, "tools");
    CHECK(count_fields(actual_tools, 3), "tool set");
    CHECK(ex_equal(f_field(actual_tools, "shell"), f_field(expected_tools, "shell")) &&
              ex_equal(f_field(actual_tools, "awk"), f_field(expected_tools, "awk")),
          "toolchain binding");
    json_object *native = f_field(actual_tools, "native");
    const char *native_path = f_string(native, "path");
    CHECK(count_fields(native, 2) && native_path && native_path[0] == '/' &&
              ex_equal(f_field(native, "sha256"),
                       f_field(f_field(expected_tools, "native"), "sha256")),
          "native tool binding");
    const char *loads[] = {"load_before", "load_after"};
    for (size_t i = 0; i < 2; i++) {
        json_object *load = f_field(observed, loads[i]);
        CHECK(list_size(load, 3), "load observations");
        for (size_t j = 0; j < 3; j++) {
            json_object *number = json_object_array_get_idx(load, j);
            CHECK((json_object_is_type(number, json_type_int) ||
                   json_object_is_type(number, json_type_double)) &&
                      isfinite(json_object_get_double(number)) &&
                      json_object_get_double(number) >= 0,
                  "load observation value");
        }
    }
    json_object *commands = f_field(manifest, "commands");
    CHECK(count_fields(commands, 2), "command set");
    const char *input_path = NULL, *shell = f_string(f_field(expected_tools, "shell"), "path");
    for (size_t i = 0; i < 2; i++) {
        json_object *command = f_field(commands, names[i]);
        char label[32];
        snprintf(label, sizeof label, "%s.sh", names[i]);
        CHECK(!f_path(path, sizeof path, cwd, label) && list_size(command, 3), "command shape");
        const char *tool = f_text(json_object_array_get_idx(command, 0)),
                   *script = f_text(json_object_array_get_idx(command, 1)),
                   *input = f_text(json_object_array_get_idx(command, 2));
        CHECK(tool && shell && !strcmp(tool, shell) && script && !strcmp(script, path) && input &&
                  input[0] == '/',
              "command binding");
        const char *base = strrchr(input, '/');
        CHECK(base && !strcmp(base + 1, "records"), "workload argument");
        CHECK(!input_path || !strcmp(input_path, input), "matched workload command");
        input_path = input;
    }
    json_object *warmups = f_field(manifest, "warmups");
    CHECK(list_size(warmups, 4), "warmup count");
    for (size_t i = 0; i < 4; i++) {
        json_object *row = json_object_array_get_idx(warmups, i);
        int64_t time;
        CHECK(count_fields(row, 6) && ex_text(row, "implementation", names[i % 2]) &&
                  integer_is(f_field(row, "warmup"), (int64_t)i / 2 + 1) &&
                  perf_int(f_field(row, "elapsed_ns"), &time) && time > 0 &&
                  ex_text(row, "status", "ok") && ex_text(row, "count", "4003") &&
                  integer_is(f_field(row, "returncode"), 0),
              "invalid or failed warmup");
    }
    CHECK(list_size(f_field(manifest, "failures"), 0), "recorded measurement failures");
cleanup:
    json_object_put(expected);
    json_object_put(environment);
    json_object_put(protocol);
    free(records);
    return error;
}
static double independent_median(const double *values) {
    double sorted[10];
    for (size_t i = 0; i < 10; i++) {
        size_t j = i;
        while (j && sorted[j - 1] > values[i]) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = values[i];
    }
    return (sorted[4] + sorted[5]) / 2.0;
}
static int compare_draw(const void *left, const void *right) {
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}
static const char *recompute(json_object *report, json_object *rows) {
    const char *error = NULL;
    double values[2][10], effects[10], draws[10000];
    json_object *expected = NULL;
    CHECK(list_size(rows, 20), "paired trial count");
    for (size_t i = 0; i < 20; i++) {
        size_t trial = i / 2 + 1, position = i % 2, name = (position + (trial % 2 == 0)) % 2;
        json_object *row = json_object_array_get_idx(rows, i);
        char identity[64], trial_text[16], order[4], canonical[32], *end = NULL;
        const char *label = name ? "candidate" : "baseline", *elapsed = f_string(row, "elapsed_ns");
        snprintf(identity, sizeof identity, "%02zu-%s", trial, label);
        snprintf(trial_text, sizeof trial_text, "%zu", trial);
        snprintf(order, sizeof order, "%zu", position);
        CHECK(count_fields(row, 8) && ex_text(row, "sample_id", identity) &&
                  ex_text(row, "implementation", label) && ex_text(row, "trial", trial_text) &&
                  ex_text(row, "order", order),
              "pair identity or order");
        CHECK(elapsed && *elapsed, "elapsed value");
        errno = 0;
        long long number = strtoll(elapsed, &end, 10);
        snprintf(canonical, sizeof canonical, "%lld", number);
        CHECK(!errno && end && !*end && number > 0 && number <= PERF_MAX_SAMPLE_NS && !strcmp(canonical, elapsed) &&
                  ex_text(row, "status", "ok") && ex_text(row, "count", "4003") &&
                  ex_text(row, "returncode", "0"),
              "invalid or failed trial");
        values[name][trial - 1] = (double)number;
    }
    expected = f_parse("{\"rows\":20,\"failures\":[],\"invalid_reason\":null}");
    CHECK(ex_equal(f_field(report, "raw_summary"), expected), "raw summary");
    const char *names[] = {"baseline", "candidate"};
    for (size_t i = 0; i < 2; i++) {
        double maximum = values[i][0];
        for (size_t j = 1; j < 10; j++)
            if (values[i][j] > maximum)
                maximum = values[i][j];
        json_object *summary = f_field(report, names[i]);
        CHECK(count_fields(summary, 3) && integer_is(f_field(summary, "n"), 10) &&
                  number_is(f_field(summary, "median_ns"), independent_median(values[i])) &&
                  number_is(f_field(summary, "p95_ns"), maximum),
              "sample summaries");
    }
    for (size_t i = 0; i < 10; i++)
        effects[i] = values[1][i] / values[0][i] - 1.0;
    struct perf_rng generator;
    perf_seed(&generator);
    for (size_t draw = 0; draw < 10000; draw++) {
        double selected[10];
        for (size_t index = 0; index < 10; index++)
            selected[index] = effects[(int)(perf_random(&generator) * 10.0)];
        draws[draw] = independent_median(selected);
    }
    qsort(draws, 10000, sizeof *draws, compare_draw);
    double estimate = independent_median(effects);
    json_object *uncertainty = f_field(report, "paired_uncertainty");
    CHECK(count_fields(uncertainty, 7) &&
              ex_text(uncertainty, "method", "paired bootstrap median relative change") &&
              ex_text(uncertainty, "interval", "90% percentile; independent pair assumption") &&
              integer_is(f_field(uncertainty, "seed"), 1729) &&
              integer_is(f_field(uncertainty, "resamples"), 10000) &&
              number_is(f_field(uncertainty, "estimate"), estimate) &&
              number_is(f_field(uncertainty, "lower"), draws[499]) &&
              number_is(f_field(uncertainty, "upper"), draws[9499]),
          "paired uncertainty recomputation");
    CHECK(ex_text(report, "outcome",
                  estimate <= -.10 && draws[9499] < 0 ? "target established"
                                                      : "target not established"),
          "outcome classification");
cleanup:
    json_object_put(expected);
    return error;
}
static const char *check_report(json_object **observation) {
    const char *error = NULL;
    char root[F_PATH], path[F_PATH], raw_hash[65], manifest_hash[65];
    json_object *report = ex_input("subject"), *manifest = ex_input("manifest"), *rows = NULL,
                *lines = NULL, *header = NULL, *limits = perf_limits();
    char *raw = NULL;
    size_t raw_size = 0;
    CHECK(report && manifest && perf_root(root), "report inputs");
    CHECK(count_fields(report, 9) && integer_is(f_field(report, "schema_version"), 2) &&
              ex_equal(f_field(report, "manifest"), manifest),
          "report fields/sealed manifest equality");
    error = manifest_check(manifest, root);
    if (error)
        goto cleanup;
    CHECK(!ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "raw") &&
              (raw = ex_read(path, F_LIMIT, &raw_size)) && !memchr(raw, 0, raw_size),
          "raw input");
    rows = perf_csv(raw, &lines, &header);
    CHECK(list_size(header, 8), "raw column count");
    const char *columns[] = {"sample_id", "implementation", "trial", "order",
                             "elapsed_ns", "status", "count", "returncode"};
    for (size_t i = 0; i < 8; i++)
        CHECK(!strcmp(f_text(json_object_array_get_idx(header, i)), columns[i]), "raw column order");
    CHECK(rows && ex_equal(f_field(report, "raw_samples"), lines), "sealed raw equality");
    error = recompute(report, rows);
    if (error)
        goto cleanup;
    CHECK(ex_equal(f_field(report, "limits"), limits), "measurement limitations");
    CHECK(!ex_hash(raw, raw_size, raw_hash) &&
              !ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "manifest") &&
              !f_hash(path, manifest_hash),
          "measurement hashes");
    *observation = json_object_new_object();
    const char *fields[] = {"outcome", "baseline", "candidate", "paired_uncertainty"};
    for (size_t i = 0; i < 4; i++)
        json_object_object_add(*observation, fields[i],
                               json_object_get(f_field(report, fields[i])));
    json_object_object_add(*observation, "measurement", json_object_new_int(10));
    f_string_add(*observation, "unit", "paired_trials");
    f_string_add(*observation, "raw_csv_sha256", raw_hash);
    f_string_add(*observation, "manifest_sha256", manifest_hash);
cleanup:
    json_object_put(report);
    json_object_put(manifest);
    json_object_put(rows);
    json_object_put(lines);
    json_object_put(header);
    json_object_put(limits);
    free(raw);
    return error;
}
int ex_performance_check(void) {
    json_object *raw = NULL;
    const char *error = check_report(&raw);
    bool valid = error == NULL;
    if (!raw) {
        raw = f_parse("{\"measurement\":0,\"unit\":\"validated_paired_trials\"}");
        f_string_add(raw, "error", error ? error : "validation failure");
    }
    f_string_add(raw, "actual", valid ? "pass" : "fail");
    f_string_add(raw, "verdict", valid ? "pass" : "fail");
    json_object *observations = json_object_new_array(), *limits = perf_limits();
    const char *ids[] = {"performance-outcome", "binding-check", "protocol-check", "analysis-check",
                         "limits-check"};
    const char *requirements[] = {"binding", "protocol", "analysis", "outcome", "limits"};
    const char *arguments[] = {"./plan-example", "performance-check"};
    json_object *obligations = ex_strings(ids, 5), *needs = ex_strings(requirements, 5),
                *argv = ex_strings(arguments, 2);
    int status = 2;
    if (!ex_observe(observations, "performance-outcome", raw, false))
        status =
            ex_evidence("assessment", "performance-check", "performance-check-recipe",
                        "independent-performance-checker-v4", argv, needs, obligations,
                        observations, valid ? 0 : 1, valid, false, limits,
                        "Independent source, protocol, raw sample and paired analysis checks.");
    json_object_put(raw);
    json_object_put(observations);
    json_object_put(limits);
    json_object_put(obligations);
    json_object_put(needs);
    json_object_put(argv);
    return status;
}
