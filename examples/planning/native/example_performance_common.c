#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include "example_performance.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

/* MT19937 init_by_array for the one-word integer seed1729. This preserves the
 * original experiment's deterministic 53-bit draw stream, not a new estimator. */
void perf_seed(struct perf_rng *rng) {
    rng->state[0] = UINT32_C(19650218);
    for (size_t i = 1; i < 624; i++)
        rng->state[i] =
            UINT32_C(1812433253) * (rng->state[i - 1] ^ (rng->state[i - 1] >> 30)) + (uint32_t)i;
    size_t i = 1;
    for (size_t k = 624; k; k--) {
        rng->state[i] = (rng->state[i] ^
                         ((rng->state[i - 1] ^ (rng->state[i - 1] >> 30)) * UINT32_C(1664525))) +
                        UINT32_C(1729);
        if (++i >= 624) {
            rng->state[0] = rng->state[623];
            i = 1;
        }
    }
    for (size_t k = 623; k; k--) {
        rng->state[i] = (rng->state[i] ^
                         ((rng->state[i - 1] ^ (rng->state[i - 1] >> 30)) * UINT32_C(1566083941))) -
                        (uint32_t)i;
        if (++i >= 624) {
            rng->state[0] = rng->state[623];
            i = 1;
        }
    }
    rng->state[0] = UINT32_C(0x80000000);
    rng->index = 624;
}
static uint32_t random_word(struct perf_rng *rng) {
    if (rng->index == 624) {
        for (size_t i = 0; i < 624; i++) {
            uint32_t y = (rng->state[i] & UINT32_C(0x80000000)) |
                         (rng->state[(i + 1) % 624] & UINT32_C(0x7fffffff));
            rng->state[i] =
                rng->state[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? UINT32_C(0x9908b0df) : 0);
        }
        rng->index = 0;
    }
    uint32_t y = rng->state[rng->index++];
    y ^= y >> 11;
    y ^= (y << 7) & UINT32_C(0x9d2c5680);
    y ^= (y << 15) & UINT32_C(0xefc60000);
    y ^= y >> 18;
    return y;
}
double perf_random(struct perf_rng *rng) {
    uint32_t a = random_word(rng) >> 5, b = random_word(rng) >> 6;
    return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}
bool perf_int(json_object *value, int64_t *out) {
    if (!json_object_is_type(value, json_type_int))
        return false;
    *out = json_object_get_int64(value);
    return true;
}
const char *perf_toolchain(void) { return "C99/" __VERSION__; }
json_object *perf_protocol(void) {
    return f_parse(
        "{\"warmups\":2,\"trials\":10,\"order\":\"alternating_AB_BA\",\"stopping_rule\":\"exactly_"
        "10_pairs_no_exclusions_or_retries\",\"sample_timeout_seconds\":10,\"expected_count\":"
        "\"4003\",\"exclusions\":[],\"exclusive_hydra_measurement\":true,\"exclusivity_scope\":"
        "\"coordinated Hydra jobs; other system activity is observed, not excluded\"}");
}
json_object *perf_limits(void) {
    const char *values[] = {
        "One synthetic line-count workload on one host; no Hydra-wide performance claim.",
        "Fresh-process elapsed time includes process startup and OS effects.",
        "Ten pairs give limited precision; p95 is the sample maximum, not a tail guarantee.",
        ("Paired bootstrap assumes independent trial pairs and may understate drift or correlated "
         "noise.")};
    return ex_strings(values, 4);
}
const char *perf_root(char buffer[F_PATH]) {
    const char *root = getenv("HYDRA_WORKFLOW_REPO_ROOT");
    if (!root)
        return getcwd(buffer, F_PATH);
    return f_copy(buffer, F_PATH, root) ? NULL : buffer;
}
json_object *perf_source(const char *path, const char *label) {
    size_t size;
    char hash[65];
    char *bytes = ex_read(path, F_LIMIT, &size);
    if (!bytes || ex_hash(bytes, size, hash)) {
        free(bytes);
        return NULL;
    }
    free(bytes);
    json_object *source = json_object_new_object();
    f_string_add(source, "path", label);
    f_string_add(source, "sha256", hash);
    json_object_object_add(source, "bytes", json_object_new_int64((int64_t)size));
    return source;
}
static char *tool_path(const char *name) {
    char *args[] = {"sh", "-c", "command -v \"$1\"", "tool", (char *)name, NULL};
    struct f_capture cap = {0};
    char *path = NULL;
    if (!f_run(args, NULL, 0, 5, &cap) && cap.status == 0 && cap.out) {
        cap.out[strcspn(cap.out, "\r\n")] = 0;
        path = realpath(cap.out, NULL);
    }
    f_capture_free(&cap);
    return path;
}
json_object *perf_environment(const char *root) {
    struct utsname host;
    json_object *env = NULL, *tools = NULL, *git = NULL;
    char platform[F_PATH], *paths[3] = {NULL, NULL, NULL};
    const char *keys[] = {"shell", "awk", "native"};
    if (uname(&host))
        return NULL;
    int n = snprintf(platform, sizeof platform, "%s %s", host.sysname, host.release);
    if (n < 0 || (size_t)n >= sizeof platform)
        return NULL;
    env = json_object_new_object();
    tools = json_object_new_object();
    git = json_object_new_object();
    f_string_add(env, "toolchain", perf_toolchain());
    f_string_add(env, "platform", platform);
    f_string_add(env, "machine", host.machine);
    f_string_add(env, "cwd", root);
    f_string_add(env, "timer", "clock_gettime(CLOCK_MONOTONIC)");
    f_string_add(env, "units", "nanoseconds");
    paths[0] = tool_path("sh");
    paths[1] = tool_path("awk");
    paths[2] = realpath(ex_program, NULL);
    for (size_t i = 0; i < 3; i++) {
        char hash[65];
        if (!paths[i] || f_hash(paths[i], hash))
            goto bad;
        json_object *tool = json_object_new_object();
        f_string_add(tool, "path", paths[i]);
        f_string_add(tool, "sha256", hash);
        json_object_object_add(tools, keys[i], tool);
    }
    const char *refs[] = {"HEAD", "HEAD^{tree}"}, *labels[] = {"commit", "tree"};
    for (size_t i = 0; i < 2; i++) {
        char *args[] = {"git", "-C", (char *)root, "rev-parse", (char *)refs[i], NULL};
        struct f_capture cap = {0};
        if (f_run(args, NULL, 0, 5, &cap) || cap.status || !cap.out) {
            f_capture_free(&cap);
            goto bad;
        }
        cap.out[strcspn(cap.out, "\r\n")] = 0;
        f_string_add(git, labels[i], cap.out);
        f_capture_free(&cap);
    }
    json_object_object_add(env, "tools", tools);
    tools = NULL;
    json_object_object_add(env, "source_git", git);
    git = NULL;
    for (size_t i = 0; i < 3; i++)
        free(paths[i]);
    return env;
bad:
    for (size_t i = 0; i < 3; i++)
        free(paths[i]);
    json_object_put(tools);
    json_object_put(git);
    json_object_put(env);
    return NULL;
}
/* CSV is framing only. Numerical and protocol acceptance stay in the two
 * independent callers. Decode quoted fields and retain every raw line even
 * when a malformed record prevents structured decoding. */
static char *csv_field(char **cursor, int *delimiter) {
    char *start = *cursor, *read = start, *write = start;
    bool quoted = *read == '"';
    if (quoted) read++;
    for (;;) {
        if (quoted && *read == '"') {
            read++;
            if (*read == '"') { *write++ = *read++; continue; }
            if (*read && *read != ',' && *read != '\r' && *read != '\n') return NULL;
            break;
        }
        if (!*read) { if (quoted) return NULL; break; }
        if (!quoted && (*read == ',' || *read == '\r' || *read == '\n')) break;
        *write++ = *read++;
    }
    *delimiter = (unsigned char)*read;
    if (*read == '\r' && read[1] == '\n') read++;
    if (*read) read++;
    *write = 0;
    *cursor = read;
    return start;
}
json_object *perf_raw_lines(const char *text, size_t size) {
    json_object *lines = json_object_new_array();
    size_t start = 0;
    while (start < size) {
        size_t end = start;
        while (end < size && text[end] != '\r' && text[end] != '\n') end++;
        json_object_array_add(lines, json_object_new_string_len(text + start, (int)(end-start)));
        if (end < size && text[end] == '\r' && end + 1 < size && text[end+1] == '\n') end++;
        start = end < size ? end+1 : end;
    }
    return lines;
}
json_object *perf_csv(const char *text, json_object **lines, json_object **header) {
    static const char *fields[] = {"sample_id", "implementation", "trial", "order",
                                   "elapsed_ns", "status", "count", "returncode"};
    json_object *rows = json_object_new_array();
    char *copy = text ? strdup(text) : NULL, *cursor = copy;
    const char *columns[8];
    *lines = perf_raw_lines(text ? text : "", text ? strlen(text) : 0);
    if (header) *header = NULL;
    if (!copy || !*copy) goto bad;
    for (size_t i = 0; i < 8; i++) {
        int delimiter; char *name = csv_field(&cursor, &delimiter);
        if (!name || (i < 7 ? delimiter != ',' : delimiter == ',')) goto bad;
        size_t match = 0;
        while (match < 8 && strcmp(name, fields[match])) match++;
        if (match == 8) goto bad;
        columns[i] = fields[match];
        for (size_t j = 0; j < i; j++) if (columns[j] == columns[i]) goto bad;
    }
    if (header) *header = ex_strings(columns, 8);
    while (*cursor) {
        if (*cursor == '\n' || *cursor == '\r') { cursor++; continue; }
        json_object *row = json_object_new_object();
        for (size_t i = 0; i < 8; i++) {
            int delimiter; char *value = csv_field(&cursor, &delimiter);
            if (!value || (i < 7 ? delimiter != ',' : delimiter == ',')) {
                json_object_put(row); goto bad;
            }
            f_string_add(row, columns[i], value);
        }
        json_object_array_add(rows, row);
    }
    free(copy);
    return rows;
bad:
    free(copy);
    json_object_put(rows);
    return NULL;
}
