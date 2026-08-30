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

int hydra_read_scalar(const char *path, char *out, size_t out_size) {
    FILE *file;
    size_t length;
    int extra;
    if (path == NULL || out == NULL || out_size < 2U) return -1;
    file = fopen(path, "rb");
    if (file == NULL) return -1;
    if (fgets(out, (int)out_size, file) == NULL) {
        fclose(file);
        return -1;
    }
    length = strlen(out);
    if (length > 0U && out[length - 1U] == '\n') out[--length] = '\0';
    if (length > 0U && out[length - 1U] == '\r') out[--length] = '\0';
    extra = fgetc(file);
    fclose(file);
    if (extra != EOF || strchr(out, '\n') != NULL || strchr(out, '\r') != NULL) return -1;
    return 0;
}

static int hydra_validate_head(const char *head_path, const char *head_name, FILE *err) {
    char path[PATH_MAX];
    char value[HYDRA_SCALAR_MAX];
    char instance_path[PATH_MAX];
    if (!hydra_valid_id(head_name) || strncmp(head_name, "head_", 5U) != 0) {
        fprintf(err, "invalid head id: %s\n", head_name);
        return -1;
    }
    if (hydra_path(path, sizeof(path), head_path, "head-id") != 0 ||
        hydra_read_scalar(path, value, sizeof(value)) != 0 || strcmp(value, head_name) != 0) {
        fprintf(err, "invalid head-id record: %s\n", head_name);
        return -1;
    }
    if (hydra_path(path, sizeof(path), head_path, "branch") != 0 ||
        hydra_read_scalar(path, value, sizeof(value)) != 0 || value[0] == '\0') {
        fprintf(err, "invalid branch record: %s\n", head_name);
        return -1;
    }
    if (hydra_path(path, sizeof(path), head_path, "session") != 0 ||
        hydra_read_scalar(path, value, sizeof(value)) != 0 || value[0] == '\0') {
        fprintf(err, "invalid session record: %s\n", head_name);
        return -1;
    }
    if (hydra_path(path, sizeof(path), head_path, "current-instance") != 0 ||
        hydra_read_scalar(path, value, sizeof(value)) != 0 || !hydra_valid_id(value) ||
        strncmp(value, "instance_", 9U) != 0) {
        fprintf(err, "invalid current instance: %s\n", head_name);
        return -1;
    }
    if (hydra_instance_path(instance_path, sizeof(instance_path), head_path, value) != 0 ||
        !hydra_is_dir(instance_path)) {
        fprintf(err, "missing current instance: %s\n", head_name);
        return -1;
    }
    if (hydra_path(path, sizeof(path), instance_path, "instance-id") != 0 ||
        hydra_read_scalar(path, value, sizeof(value)) != 0 ||
        strcmp(value, strrchr(instance_path, '/') + 1) != 0) {
        fprintf(err, "invalid instance record: %s\n", head_name);
        return -1;
    }
    return 0;
}

static int hydra_validate_unique_branches(const char *heads_path, FILE *err) {
    DIR *heads = opendir(heads_path);
    struct dirent *head_entry;
    if (heads == NULL) return -1;
    while ((head_entry = readdir(heads)) != NULL) {
        char head_path[PATH_MAX];
        char branch_path[PATH_MAX];
        char branch[HYDRA_SCALAR_MAX];
        DIR *others;
        struct dirent *other_entry;
        if (head_entry->d_name[0] == '.' || strncmp(head_entry->d_name, "head_", 5U) != 0) continue;
        if (hydra_path(head_path, sizeof(head_path), heads_path, head_entry->d_name) != 0 ||
            !hydra_is_dir(head_path) ||
            hydra_path(branch_path, sizeof(branch_path), head_path, "branch") != 0 ||
            hydra_read_scalar(branch_path, branch, sizeof(branch)) != 0) {
            closedir(heads);
            return -1;
        }
        others = opendir(heads_path);
        if (others == NULL) {
            closedir(heads);
            return -1;
        }
        while ((other_entry = readdir(others)) != NULL) {
            char other_path[PATH_MAX];
            char other_branch_path[PATH_MAX];
            char other_branch[HYDRA_SCALAR_MAX];
            if (other_entry->d_name[0] == '.' ||
                strncmp(other_entry->d_name, "head_", 5U) != 0 ||
                strcmp(other_entry->d_name, head_entry->d_name) <= 0) continue;
            if (hydra_path(other_path, sizeof(other_path), heads_path, other_entry->d_name) != 0 ||
                !hydra_is_dir(other_path) ||
                hydra_path(other_branch_path, sizeof(other_branch_path), other_path, "branch") != 0 ||
                hydra_read_scalar(other_branch_path, other_branch, sizeof(other_branch)) != 0) {
                closedir(others);
                closedir(heads);
                return -1;
            }
            if (strcmp(branch, other_branch) == 0) {
                fprintf(err, "duplicate branch identity: %s\n", branch);
                closedir(others);
                closedir(heads);
                return -1;
            }
        }
        closedir(others);
    }
    closedir(heads);
    return 0;
}

int hydra_validate_state(const char *root, FILE *err) {
    char path[PATH_MAX];
    char value[HYDRA_SCALAR_MAX];
    DIR *projects;
    struct dirent *project_entry;
    if (hydra_path(path, sizeof(path), root, "schema-version") != 0 ||
        hydra_read_scalar(path, value, sizeof(value)) != 0 || strcmp(value, "2") != 0) {
        fputs("unsupported or missing state schema\n", err);
        return -1;
    }
    if (hydra_path(path, sizeof(path), root, "projects") != 0 || (projects = opendir(path)) == NULL) {
        fputs("missing projects directory\n", err);
        return -1;
    }
    while ((project_entry = readdir(projects)) != NULL) {
        char project_path[PATH_MAX];
        char heads_path[PATH_MAX];
        DIR *heads;
        struct dirent *head_entry;
        if (project_entry->d_name[0] == '.') continue;
        if (hydra_path(project_path, sizeof(project_path), path, project_entry->d_name) != 0) {
            closedir(projects);
            return -1;
        }
        if (!hydra_is_dir(project_path) || strncmp(project_entry->d_name, "project_", 8U) != 0) continue;
        if (!hydra_valid_id(project_entry->d_name)) {
            fprintf(err, "invalid project id: %s\n", project_entry->d_name);
            closedir(projects);
            return -1;
        }
        if (hydra_path(heads_path, sizeof(heads_path), project_path, "project-id") != 0 ||
            hydra_read_scalar(heads_path, value, sizeof(value)) != 0 ||
            strcmp(value, project_entry->d_name) != 0 ||
            hydra_path(heads_path, sizeof(heads_path), project_path, "repo-root") != 0 ||
            hydra_read_scalar(heads_path, value, sizeof(value)) != 0 || value[0] == '\0' ||
            hydra_path(heads_path, sizeof(heads_path), project_path, "heads") != 0 ||
            (heads = opendir(heads_path)) == NULL) {
            fprintf(err, "invalid project record: %s\n", project_entry->d_name);
            closedir(projects);
            return -1;
        }
        while ((head_entry = readdir(heads)) != NULL) {
            char head_path[PATH_MAX];
            if (head_entry->d_name[0] == '.') continue;
            if (strncmp(head_entry->d_name, "head_", 5U) != 0) continue;
            if (hydra_path(head_path, sizeof(head_path), heads_path, head_entry->d_name) != 0 ||
                hydra_validate_head(head_path, head_entry->d_name, err) != 0) {
                closedir(heads);
                closedir(projects);
                return -1;
            }
        }
        closedir(heads);
        if (hydra_validate_unique_branches(heads_path, err) != 0) {
            closedir(projects);
            return -1;
        }
    }
    closedir(projects);
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

static int hydra_compare_heads(const void *left, const void *right) {
    const struct hydra_head_record *a = (const struct hydra_head_record *)left;
    const struct hydra_head_record *b = (const struct hydra_head_record *)right;
    int project = strcmp(a->project_id, b->project_id);
    return project != 0 ? project : strcmp(a->head_id, b->head_id);
}

static int hydra_collect_heads(const char *root, struct hydra_head_record **records_out,
                               size_t *count_out, size_t *projects_out, FILE *err) {
    char projects_path[PATH_MAX];
    DIR *projects;
    struct dirent *project_entry;
    struct hydra_head_record *records = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    size_t project_count = 0U;
    if (hydra_path(projects_path, sizeof(projects_path), root, "projects") != 0 ||
        (projects = opendir(projects_path)) == NULL) return -1;
    while ((project_entry = readdir(projects)) != NULL) {
        char project_path[PATH_MAX];
        char heads_path[PATH_MAX];
        DIR *heads;
        struct dirent *head_entry;
        if (project_entry->d_name[0] == '.') continue;
        if (hydra_path(project_path, sizeof(project_path), projects_path, project_entry->d_name) != 0) {
            closedir(projects); free(records); return -1;
        }
        if (!hydra_is_dir(project_path) || strncmp(project_entry->d_name, "project_", 8U) != 0) continue;
        if (!hydra_valid_id(project_entry->d_name)) continue;
        project_count++;
        if (hydra_path(heads_path, sizeof(heads_path), project_path, "heads") != 0 ||
            (heads = opendir(heads_path)) == NULL) {
            closedir(projects);
            free(records);
            return -1;
        }
        while ((head_entry = readdir(heads)) != NULL) {
            struct hydra_head_record *record;
            char head_path[PATH_MAX];
            char scalar_path[PATH_MAX];
            if (head_entry->d_name[0] == '.') continue;
            if (strncmp(head_entry->d_name, "head_", 5U) != 0) continue;
            if (count == capacity) {
                size_t next = capacity == 0U ? 16U : capacity * 2U;
                void *grown = realloc(records, next * sizeof(*records));
                if (grown == NULL) {
                    closedir(heads); closedir(projects); free(records); return -1;
                }
                records = (struct hydra_head_record *)grown;
                capacity = next;
            }
            record = &records[count];
            memset(record, 0, sizeof(*record));
            if (hydra_copy(record->project_id, sizeof(record->project_id), project_entry->d_name) != 0 ||
                hydra_copy(record->head_id, sizeof(record->head_id), head_entry->d_name) != 0 ||
                hydra_path(head_path, sizeof(head_path), heads_path, head_entry->d_name) != 0 ||
                hydra_path(scalar_path, sizeof(scalar_path), head_path, "branch") != 0 ||
                hydra_read_scalar(scalar_path, record->branch, sizeof(record->branch)) != 0 ||
                hydra_path(scalar_path, sizeof(scalar_path), head_path, "desired-state") != 0 ||
                hydra_read_scalar(scalar_path, record->desired_state, sizeof(record->desired_state)) != 0 ||
                hydra_path(scalar_path, sizeof(scalar_path), head_path, "current-instance") != 0 ||
                hydra_read_scalar(scalar_path, record->current_instance, sizeof(record->current_instance)) != 0) {
                fprintf(err, "cannot read snapshot head: %s\n", head_entry->d_name);
                closedir(heads); closedir(projects); free(records); return -1;
            }
            count++;
        }
        closedir(heads);
    }
    closedir(projects);
    qsort(records, count, sizeof(*records), hydra_compare_heads);
    *records_out = records;
    *count_out = count;
    *projects_out = project_count;
    return 0;
}

int hydra_write_snapshot(const char *root, FILE *out, FILE *err) {
    struct hydra_head_record *records = NULL;
    size_t count = 0U;
    size_t projects = 0U;
    size_t index;
    if (hydra_validate_state(root, err) != 0 ||
        hydra_collect_heads(root, &records, &count, &projects, err) != 0) return -1;
    fprintf(out, "{\"schema_version\":1,\"ok\":true,\"command\":\"snapshot\",\"data\":{\"state_schema\":2,\"projects\":%lu,\"heads\":[",
            (unsigned long)projects);
    for (index = 0U; index < count; index++) {
        if (index > 0U) fputc(',', out);
        fputs("{\"project_id\":", out); hydra_json_write_string(out, records[index].project_id);
        fputs(",\"head_id\":", out); hydra_json_write_string(out, records[index].head_id);
        fputs(",\"branch\":", out); hydra_json_write_string(out, records[index].branch);
        fputs(",\"desired_state\":", out); hydra_json_write_string(out, records[index].desired_state);
        fputs(",\"current_instance\":", out); hydra_json_write_string(out, records[index].current_instance);
        fputc('}', out);
    }
    free(records);
    return fputs("]}}\n", out) == EOF ? -1 : 0;
}
