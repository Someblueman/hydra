#include "libhydra.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define HYDRA_SCALAR_MAX 4096
#define HYDRA_EVENT_MAX 32770

struct hydra_head_record {
    char project_id[64];
    char head_id[64];
    char branch[HYDRA_SCALAR_MAX];
    char desired_state[64];
    char current_instance[64];
};

static int hydra_path(char *out, size_t size, const char *a, const char *b) {
    int written = snprintf(out, size, "%s/%s", a, b);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int hydra_copy(char *out, size_t size, const char *value) {
    int written = snprintf(out, size, "%s", value);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int hydra_instance_path(char *out, size_t size, const char *head_path,
                               const char *instance_id) {
    int written = snprintf(out, size, "%s/instances/%s", head_path, instance_id);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int hydra_is_dir(const char *path) {
    struct stat status;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

int hydra_json_write_string(FILE *out, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    if (fputc('"', out) == EOF) {
        return -1;
    }
    while (*cursor != '\0') {
        unsigned char byte = *cursor++;
        switch (byte) {
            case '"':
                if (fputs("\\\"", out) == EOF) return -1;
                break;
            case '\\':
                if (fputs("\\\\", out) == EOF) return -1;
                break;
            case '\b':
                if (fputs("\\b", out) == EOF) return -1;
                break;
            case '\t':
                if (fputs("\\t", out) == EOF) return -1;
                break;
            case '\n':
                if (fputs("\\n", out) == EOF) return -1;
                break;
            case '\f':
                if (fputs("\\f", out) == EOF) return -1;
                break;
            case '\r':
                if (fputs("\\r", out) == EOF) return -1;
                break;
            default:
                if (byte < 0x20U) {
                    if (fprintf(out, "\\u%04x", (unsigned int)byte) < 0) return -1;
                } else if (fputc((int)byte, out) == EOF) {
                    return -1;
                }
        }
    }
    return fputc('"', out) == EOF ? -1 : 0;
}

int hydra_valid_id(const char *value) {
    static const char *prefixes[] = {
        "project_", "head_", "instance_", "run_", "step_", "evt_", "claim_", "pack_"
    };
    size_t index;
    const char *suffix = NULL;
    if (value == NULL) return 0;
    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++) {
        size_t length = strlen(prefixes[index]);
        if (strncmp(value, prefixes[index], length) == 0) {
            suffix = value + length;
            break;
        }
    }
    if (suffix == NULL || *suffix == '\0') return 0;
    while (*suffix != '\0') {
        if (!(*suffix >= '0' && *suffix <= '9') && !(*suffix >= 'a' && *suffix <= 'f')) return 0;
        suffix++;
    }
    return 1;
}

/* Reads a scalar record: a regular file that holds exactly one line with no
 * carriage return and no NUL, the rule of state_v2_scalar_valid in
 * lib/state_v2.sh. The line may be empty and may lack its newline. */
int hydra_read_scalar(const char *path, char *out, size_t out_size) {
    struct stat status;
    FILE *file;
    size_t length;
    int failed;
    if (path == NULL || out == NULL || out_size < 2U) return -1;
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode)) return -1;
    file = fopen(path, "rb");
    if (file == NULL) return -1;
    length = fread(out, 1U, out_size, file);
    failed = ferror(file);
    fclose(file);
    if (failed || length == 0U || length == out_size) return -1;
    if (out[length - 1U] == '\n') length--;
    out[length] = '\0';
    if (memchr(out, '\n', length) != NULL || memchr(out, '\r', length) != NULL ||
        memchr(out, '\0', length) != NULL) return -1;
    return 0;
}

int hydra_validate_events(const char *path, FILE *err) {
    FILE *file = fopen(path, "rb");
    char line[HYDRA_EVENT_MAX];
    unsigned long expected = 0UL;
    if (file == NULL) {
        fprintf(err, "cannot open events: %s\n", strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *sequence;
        char *end;
        unsigned long actual;
        size_t length = strlen(line);
        if (length == 0U || (line[length - 1U] != '\n' && !feof(file)) ||
            (line[length - 1U] == '\n' && length - 1U > 32768U)) {
            fputs("event line exceeds limit or is partial\n", err);
            fclose(file);
            return -1;
        }
        if (line[0] != '{' || line[length - (line[length - 1U] == '\n' ? 2U : 1U)] != '}' ||
            strstr(line, "\"schema_version\":1") == NULL ||
            strstr(line, "\"event_id\":\"evt_") == NULL ||
            strstr(line, "\"project_id\":\"project_") == NULL ||
            strstr(line, "\"head_id\":\"head_") == NULL ||
            strstr(line, "\"instance_id\":\"instance_") == NULL) {
            fputs("invalid event envelope\n", err);
            fclose(file);
            return -1;
        }
        sequence = strstr(line, "\"sequence\":");
        if (sequence == NULL) {
            fputs("missing event sequence\n", err);
            fclose(file);
            return -1;
        }
        sequence += strlen("\"sequence\":");
        errno = 0;
        actual = strtoul(sequence, &end, 10);
        if (expected == 0UL) expected = actual;
        if (errno != 0 || end == sequence || actual != expected) {
            fprintf(err, "invalid event sequence: expected %lu\n", expected);
            fclose(file);
            return -1;
        }
        expected++;
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

/* ---- state v2 ------------------------------------------------------------
 * A snapshot is written only for a store that state_v2_verify in
 * lib/state_v2.sh accepts: the CLI falls back to the shell implementation when
 * this one refuses, so every rule here is that verifier's rule or a stricter
 * one, never a looser one. Event logs are not read: `hydra snapshot` does not
 * verify them either. */

static int hydra_scalar_at(const char *directory, const char *name, char *out, size_t out_size) {
    char path[PATH_MAX];
    return hydra_path(path, sizeof(path), directory, name) == 0 ? hydra_read_scalar(path, out, out_size) : -1;
}

static int hydra_scalars_valid(const char *directory, const char *const *names, size_t count) {
    char value[HYDRA_SCALAR_MAX];
    size_t index;
    for (index = 0U; index < count; index++) {
        if (hydra_scalar_at(directory, names[index], value, sizeof(value)) != 0) return 0;
    }
    return 1;
}

static int hydra_is_file_at(const char *directory, const char *name) {
    char path[PATH_MAX];
    struct stat status;
    return hydra_path(path, sizeof(path), directory, name) == 0 && stat(path, &status) == 0 &&
           S_ISREG(status.st_mode);
}

/* The optional terminal-mode record: 0 for interactive (also when the record
 * is absent), 1 for headless, -1 when it holds anything else. */
static int hydra_terminal_mode(const char *directory) {
    char path[PATH_MAX];
    char value[32];
    struct stat status;
    if (hydra_path(path, sizeof(path), directory, "terminal-mode") != 0) return -1;
    if (stat(path, &status) != 0) return errno == ENOENT ? 0 : -1;
    if (hydra_read_scalar(path, value, sizeof(value)) != 0) return -1;
    if (strcmp(value, "interactive") == 0) return 0;
    return strcmp(value, "headless") == 0 ? 1 : -1;
}

/* A headless head or instance has the session "-"; an interactive one has not. */
static int hydra_session_fits(const char *session, int headless) {
    return (strcmp(session, "-") == 0) == (headless != 0);
}

/* Head identity records: id, branch, desired state and completion policy.
 * Returns what is wrong, or NULL. */
static const char *hydra_load_head_identity(const char *head_path, struct hydra_head_record *record) {
    char value[HYDRA_SCALAR_MAX];
    if (!hydra_valid_id(record->head_id)) return "head id";
    if (hydra_scalar_at(head_path, "head-id", value, sizeof(value)) != 0 ||
        strcmp(value, record->head_id) != 0) return "head-id";
    if (hydra_scalar_at(head_path, "branch", record->branch, sizeof(record->branch)) != 0 ||
        record->branch[0] == '\0') return "branch";
    if (hydra_scalar_at(head_path, "desired-state", value, sizeof(value)) != 0 ||
        (strcmp(value, "running") != 0 && strcmp(value, "headless") != 0 &&
         strcmp(value, "stopping") != 0 && strcmp(value, "stopped") != 0) ||
        hydra_copy(record->desired_state, sizeof(record->desired_state), value) != 0) return "desired-state";
    if (hydra_scalar_at(head_path, "completion-policy", value, sizeof(value)) != 0 ||
        (strcmp(value, "declared-done") != 0 && strcmp(value, "observed-exit-zero") != 0 &&
         strcmp(value, "either") != 0)) return "completion-policy";
    return NULL;
}

/* The head's own records, its terminal mode (*headless) and its session and
 * current instance. Returns what is wrong, or NULL. */
static const char *hydra_load_head_records(const char *head_path, struct hydra_head_record *record, int *headless) {
    static const char *const head_scalars[] = {
        "profile", "group", "created-at", "dependencies", "pr", "worktree", "base-ref"
    };
    char value[HYDRA_SCALAR_MAX];
    if (!hydra_scalars_valid(head_path, head_scalars, sizeof(head_scalars) / sizeof(head_scalars[0])) ||
        !hydra_is_file_at(head_path, "task") || !hydra_is_file_at(head_path, "scopes")) return "required records";
    *headless = hydra_terminal_mode(head_path);
    if (*headless < 0) return "terminal-mode";
    if (hydra_scalar_at(head_path, "session", value, sizeof(value)) != 0 || value[0] == '\0' ||
        !hydra_session_fits(value, *headless)) return "session";
    if (hydra_scalar_at(head_path, "current-instance", value, sizeof(value)) != 0 ||
        !hydra_valid_id(value) || strncmp(value, "instance_", 9U) != 0 ||
        hydra_copy(record->current_instance, sizeof(record->current_instance), value) != 0) return "current-instance";
    return NULL;
}

/* The current instance's directory and records. Returns what is wrong, or NULL. */
static const char *hydra_load_instance(const char *head_path, const struct hydra_head_record *record, int headless) {
    static const char *const instance_scalars[] = {
        "started-at", "observed-status", "observed-source", "observed-confidence",
        "observed-at", "observed-exit-code", "provider-session-id"
    };
    char value[HYDRA_SCALAR_MAX];
    char instance_path[PATH_MAX];
    if (hydra_instance_path(instance_path, sizeof(instance_path), head_path, record->current_instance) != 0 ||
        !hydra_is_dir(instance_path)) return "current instance directory";
    if (hydra_scalar_at(instance_path, "instance-id", value, sizeof(value)) != 0 ||
        strcmp(value, record->current_instance) != 0) return "instance-id";
    if (hydra_terminal_mode(instance_path) != headless) return "instance terminal-mode";
    if (hydra_scalar_at(instance_path, "session", value, sizeof(value)) != 0 ||
        !hydra_session_fits(value, headless)) return "instance session";
    if (!hydra_scalars_valid(instance_path, instance_scalars,
                             sizeof(instance_scalars) / sizeof(instance_scalars[0]))) return "instance records";
    return NULL;
}

/* Verifies one head and its current instance and fills the fields a snapshot
 * prints; record->head_id is already set. Returns what is wrong, or NULL. */
static const char *hydra_load_head(const char *head_path, struct hydra_head_record *record) {
    const char *problem;
    int headless;
    problem = hydra_load_head_identity(head_path, record);
    if (problem == NULL) problem = hydra_load_head_records(head_path, record, &headless);
    if (problem == NULL) problem = hydra_load_instance(head_path, record, headless);
    return problem;
}

struct hydra_heads {
    struct hydra_head_record *records;
    size_t count;
    size_t capacity;
};

static struct hydra_head_record *hydra_heads_next(struct hydra_heads *heads) {
    if (heads->count == heads->capacity) {
        size_t next = heads->capacity == 0U ? 16U : heads->capacity * 2U;
        void *grown;
        if (heads->capacity > (size_t)-1 / 2U / sizeof(*heads->records)) return NULL;
        grown = realloc(heads->records, next * sizeof(*heads->records));
        if (grown == NULL) return NULL;
        heads->records = (struct hydra_head_record *)grown;
        heads->capacity = next;
    }
    memset(&heads->records[heads->count], 0, sizeof(*heads->records));
    return &heads->records[heads->count];
}

/* Reads one entry of a project's heads directory into the next record.
 * Returns 1 for an entry that is not a head, 0 once it is verified and
 * -1 when it is refused (the reason is on err when there is one). */
static int hydra_load_head_entry(const char *heads_path, const char *name, const char *project_id,
                                 struct hydra_heads *heads, FILE *err) {
    char head_path[PATH_MAX];
    struct hydra_head_record *record;
    const char *problem;
    if (strncmp(name, "head_", 5U) != 0) return 1;
    if (hydra_path(head_path, sizeof(head_path), heads_path, name) != 0) return -1;
    if (!hydra_is_dir(head_path)) return 1;
    record = hydra_heads_next(heads);
    if (record == NULL) return -1;
    if (hydra_copy(record->project_id, sizeof(record->project_id), project_id) != 0 ||
        hydra_copy(record->head_id, sizeof(record->head_id), name) != 0) {
        problem = "head id";
    } else {
        problem = hydra_load_head(head_path, record);
    }
    if (problem != NULL) {
        fprintf(err, "invalid head record: %s (%s)\n", name, problem);
        return -1;
    }
    heads->count++;
    return 0;
}

/* Refuses two heads on one branch among the records from first on. */
static int hydra_check_branches(const struct hydra_heads *heads, size_t first, FILE *err) {
    size_t left, right;
    for (left = first; left < heads->count; left++) {
        for (right = left + 1U; right < heads->count; right++) {
            if (strcmp(heads->records[left].branch, heads->records[right].branch) == 0) {
                fprintf(err, "duplicate branch identity: %s\n", heads->records[left].branch);
                return -1;
            }
        }
    }
    return 0;
}

/* Reads every head of one project in a single walk of its heads directory,
 * then refuses two heads on one branch among the records it collected. */
static int hydra_load_project_heads(const char *project_path, const char *project_id,
                                    struct hydra_heads *heads, FILE *err) {
    char heads_path[PATH_MAX];
    DIR *directory;
    struct dirent *entry;
    size_t first = heads->count;
    if (hydra_path(heads_path, sizeof(heads_path), project_path, "heads") != 0 ||
        (directory = opendir(heads_path)) == NULL) {
        fprintf(err, "invalid project record: %s\n", project_id);
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (hydra_load_head_entry(heads_path, entry->d_name, project_id, heads, err) < 0) {
            closedir(directory);
            return -1;
        }
    }
    closedir(directory);
    return hydra_check_branches(heads, first, err);
}

static int hydra_compare_heads(const void *left, const void *right) {
    const struct hydra_head_record *a = (const struct hydra_head_record *)left;
    const struct hydra_head_record *b = (const struct hydra_head_record *)right;
    int project = strcmp(a->project_id, b->project_id);
    return project != 0 ? project : strcmp(a->head_id, b->head_id);
}

/* One entry of the projects directory. Returns 1 for an entry that is not a
 * project, 0 once it and its heads are verified and -1 when it is refused
 * (the reason is on err when there is one). */
static int hydra_load_project(const char *projects_path, const char *name, struct hydra_heads *heads, FILE *err) {
    char project_path[PATH_MAX];
    char value[HYDRA_SCALAR_MAX];
    if (strncmp(name, "project_", 8U) != 0) return 1;
    if (hydra_path(project_path, sizeof(project_path), projects_path, name) != 0) return -1;
    if (!hydra_is_dir(project_path)) return 1;
    if (!hydra_valid_id(name)) {
        fprintf(err, "invalid project id: %s\n", name);
        return -1;
    }
    if (hydra_scalar_at(project_path, "project-id", value, sizeof(value)) != 0 ||
        strcmp(value, name) != 0 ||
        hydra_scalar_at(project_path, "repo-root", value, sizeof(value)) != 0 || value[0] == '\0') {
        fprintf(err, "invalid project record: %s\n", name);
        return -1;
    }
    return hydra_load_project_heads(project_path, name, heads, err);
}

static void hydra_heads_discard(struct hydra_heads *heads) {
    free(heads->records);
    heads->records = NULL;
    heads->count = heads->capacity = 0U;
}

/* Verifies the store and collects its heads in project, then head order. On
 * failure nothing is returned and the reason is on err. */
static int hydra_load_state(const char *root, struct hydra_heads *heads, size_t *projects_out, FILE *err) {
    char projects_path[PATH_MAX];
    char value[HYDRA_SCALAR_MAX];
    DIR *projects;
    struct dirent *entry;
    size_t project_count = 0U;
    int loaded;
    if (hydra_scalar_at(root, "schema-version", value, sizeof(value)) != 0 || strcmp(value, "2") != 0) {
        fputs("unsupported or missing state schema\n", err);
        return -1;
    }
    if (hydra_path(projects_path, sizeof(projects_path), root, "projects") != 0 ||
        (projects = opendir(projects_path)) == NULL) {
        fputs("missing projects directory\n", err);
        return -1;
    }
    while ((entry = readdir(projects)) != NULL) {
        loaded = hydra_load_project(projects_path, entry->d_name, heads, err);
        if (loaded < 0) {
            closedir(projects);
            hydra_heads_discard(heads);
            return -1;
        }
        if (loaded == 0) project_count++;
    }
    closedir(projects);
    if (heads->count > 1U) qsort(heads->records, heads->count, sizeof(*heads->records), hydra_compare_heads);
    *projects_out = project_count;
    return 0;
}

int hydra_write_snapshot(const char *root, FILE *out, FILE *err) {
    struct hydra_heads heads = {NULL, 0U, 0U};
    size_t projects = 0U;
    size_t index;
    if (hydra_load_state(root, &heads, &projects, err) != 0) return -1;
    fprintf(out, "{\"schema_version\":1,\"ok\":true,\"command\":\"snapshot\",\"data\":{\"state_schema\":2,\"projects\":%lu,\"heads\":[",
            (unsigned long)projects);
    for (index = 0U; index < heads.count; index++) {
        const struct hydra_head_record *record = &heads.records[index];
        if (index > 0U) fputc(',', out);
        fputs("{\"project_id\":", out); hydra_json_write_string(out, record->project_id);
        fputs(",\"head_id\":", out); hydra_json_write_string(out, record->head_id);
        fputs(",\"branch\":", out); hydra_json_write_string(out, record->branch);
        fputs(",\"desired_state\":", out); hydra_json_write_string(out, record->desired_state);
        fputs(",\"current_instance\":", out); hydra_json_write_string(out, record->current_instance);
        fputc('}', out);
    }
    free(heads.records);
    if (fputs("]}}\n", out) == EOF || fflush(out) != 0 || ferror(out)) return -1;
    return 0;
}
