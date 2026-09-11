#include "fleet/enrollment/enrollment.h"
#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

bool enrollment_path(const char *path) { return path && path[0] == '/' && hd_text(path, F_PATH); }
bool enrollment_digest(const char *value) {
    size_t i;
    if (!value || strlen(value) != 64) return false;
    for (i = 0; i < 64; i++) if (!strchr("0123456789abcdef", value[i])) return false;
    return true;
}
int enrollment_write(const char *path, json_object *value) {
    const char *text = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    char directory[F_PATH], *slash; int fd, status;
    if (f_write(path, text, strlen(text), true) || f_copy(directory, sizeof(directory), path)) return -1;
    slash = strrchr(directory, '/');
    if (!slash) f_copy(directory, sizeof(directory), ".");
    else if (slash == directory) slash[1] = '\0';
    else *slash = '\0';
    fd = open(directory, O_RDONLY); if (fd < 0) return -1;
    status = fsync(fd); close(fd); return status;
}
int enrollment_hash(json_object *value, char hash[65]) {
    char path[] = "/tmp/hydra-enrollment.XXXXXX"; int fd = mkstemp(path), status;
    if (fd < 0) return -1;
    close(fd); status = enrollment_write(path, value) || f_hash(path, hash);
    unlink(path); return status ? -1 : 0;
}
bool enrollment_file_matches(const char *path, const char *hash) {
    char actual[65];
    return enrollment_path(path) && enrollment_digest(hash) && !f_hash(path, actual) && !strcmp(actual, hash);
}
static bool option_value(struct enrollment_options *options, const char *key, const char *value) {
    const struct { const char *key; const char **slot; } fields[] = {
        {"--input", &options->input}, {"--output", &options->output}, {"--project", &options->project},
        {"--package", &options->package}, {"--sha256", &options->sha256}, {"--prefix", &options->prefix},
        {"--alias", &options->alias}, {"--confirm", &options->confirm}
    };
    size_t i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (strcmp(key, fields[i].key)) continue;
        if (*fields[i].slot) return false;
        *fields[i].slot = value; return true;
    }
    if (!strcmp(key, "--candidate") && options->count < ENROLL_HOSTS) {
        options->candidates[options->count++] = value; return true;
    }
    if (!strcmp(key, "--timeout")) {
        char *end; unsigned long seconds = strtoul(value, &end, 10);
        if (!*value || *end || seconds < 1 || seconds > 300) return false;
        options->seconds = (unsigned)seconds; return true;
    }
    return false;
}
json_object *enrollment_cli(int argc, char **argv) {
    struct enrollment_options options = {.seconds = 30}; int i;
    if (!argc) return f_error("fleet-enrollment", "invalid_input", "use enroll review|apply");
    for (i = 1; i < argc; i += 2)
        if (i + 1 >= argc || !option_value(&options, argv[i], argv[i + 1]))
            return f_error("fleet-enrollment", "invalid_input", "invalid, repeated, or missing option; select at most 16 candidates");
    if (!strcmp(argv[0], "review")) return enrollment_review(&options);
    if (!strcmp(argv[0], "apply")) return enrollment_apply(&options);
    return f_error("fleet-enrollment", "invalid_input", "use enroll review|apply");
}
