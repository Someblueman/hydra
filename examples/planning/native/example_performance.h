#ifndef HYDRA_EXAMPLE_PERFORMANCE_H
#define HYDRA_EXAMPLE_PERFORMANCE_H
#include "example.h"

/* Positive nanoseconds must remain exact when converted for ratio arithmetic. */
#define PERF_MAX_SAMPLE_NS INT64_C(9007199254740991)

struct perf_rng {
    uint32_t state[624];
    size_t index;
};
void perf_seed(struct perf_rng *rng);
double perf_random(struct perf_rng *rng);
json_object *perf_protocol(void);
json_object *perf_limits(void);
json_object *perf_environment(const char *root);
json_object *perf_source(const char *path, const char *label);
/* Returned arrays are caller-owned. Input lengths are bounded by F_LIMIT. */
json_object *perf_raw_lines(const char *text, size_t size);
/* Returned rows, raw lines and optional header are caller-owned. */
json_object *perf_csv(const char *text, json_object **lines, json_object **header);
const char *perf_root(char buffer[F_PATH]);
bool perf_int(json_object *value, int64_t *out);
const char *perf_toolchain(void);
#endif
