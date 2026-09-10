#define _XOPEN_SOURCE 700
/* Example-only native boundary. The installed Hydra runtime does not depend on it. */
#include "precompile.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *f_home, *f_hydra;

int precompile_error(const char *message) {
    fprintf(stderr, "precompile error: %s\n", message);
    return -1;
}
bool precompile_text_is(json_object *object, const char *key, const char *expected) {
    const char *value = f_string(object, key);
    return value && !strcmp(value, expected);
}
json_object *precompile_read(const char *path, size_t limit, char **raw, size_t *size) {
    FILE *file = NULL;
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    struct stat metadata;
    char *bytes = malloc(limit + 1);
    json_object *value = NULL;
    size_t length = 0;
    if (fd < 0 || fstat(fd, &metadata) || !S_ISREG(metadata.st_mode) ||
        metadata.st_size < 0 || (size_t)metadata.st_size > limit || !bytes) goto done;
    file = fdopen(fd, "rb");
    if (!file) goto done;
    fd = -1;
    length = fread(bytes, 1, limit + 1, file);
    if (ferror(file) || length > limit || memchr(bytes, '\0', length)) goto done;
    bytes[length] = '\0';
    if (plan_json_unique(bytes)) value = f_parse(bytes);
    if (value && raw) { *raw = bytes; bytes = NULL; }
    if (value && size) *size = length;
done:
    if (file) fclose(file);
    if (fd >= 0) close(fd);
    free(bytes);
    return value;
}
int precompile_hash(const char *bytes, size_t size, char digest[65]) {
    char *argv[] = {"shasum", "-a", "256", NULL};
    struct f_capture capture = {0};
    int status = -1;
    if (f_run(argv, bytes, size, 30, &capture)) goto done;
    if (capture.status == 127) {
        char *gnu[] = {"sha256sum", NULL};
        f_capture_free(&capture);
        if (f_run(gnu, bytes, size, 30, &capture)) goto done;
    }
    if (!capture.status &&
        capture.input_complete && capture.out && strlen(capture.out) >= 64 &&
        strspn(capture.out, "0123456789abcdef") == 64) {
        memcpy(digest, capture.out, 64); digest[64] = '\0'; status = 0;
    }
done:
    f_capture_free(&capture);
    return status;
}
static bool member_id(const char *id) {
    size_t i, length = id ? strlen(id) : 0;
    if (!length || length > 32 || id[0] < 'a' || id[0] > 'z') return false;
    for (i = 1; i < length; i++)
        if (!(id[i] >= 'a' && id[i] <= 'z') && !(id[i] >= '0' && id[i] <= '9') && id[i] != '-') return false;
    return true;
}
static int manifest_load(struct manifest *manifest) {
    json_object *items;
    size_t i, j;
    manifest->value = precompile_read("manifest.json", 4096, &manifest->raw, &manifest->size);
    if (!manifest->value) return precompile_error("invalid, duplicate or oversized manifest (maximum 4096 bytes)");
    if (json_object_object_length(manifest->value) != 2 || !f_number_is(manifest->value, "schema_version", 1) ||
        !(items = f_field(manifest->value, "items"))) return precompile_error("manifest schema");
    if (!plan_list(items, 0, 8)) return precompile_error("manifest cardinality");
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *value = f_field(item, "value");
        const char *id = f_string(item, "id");
        if (!json_object_is_type(item, json_type_object) || json_object_object_length(item) != 3 ||
            !member_id(id) || !json_object_is_type(value, json_type_int) ||
            json_object_get_int64(value) < -10 || json_object_get_int64(value) > 10 ||
            !json_object_is_type(f_field(item, "enabled"), json_type_boolean)) return precompile_error("manifest member");
        for (j = 0; j < i; j++)
            if (precompile_text_is(json_object_array_get_idx(items, j), "id", id)) return precompile_error("manifest member: duplicate ID");
    }
    return 0;
}
static bool run_id_valid(const char *id) {
    size_t i;
    if (!id || strncmp(id, "run_", 4) || !id[4]) return false;
    for (i = 4; id[i]; i++)
        if (!(id[i] >= 'a' && id[i] <= 'z') && !(id[i] >= '0' && id[i] <= '9')) return false;
    return true;
}
/* Preserve the original staged CLI's manifest-parent cwd and caller-relative
 * output path. Manifest mode still requires the current example's input. */
static int prepare_paths(const char *manifest, const char *destination, bool staged, char output[F_PATH]) {
    char input[F_PATH], expected[F_PATH], directory[F_PATH], cwd[F_PATH];
    char *separator;
    if (!getcwd(cwd, sizeof(cwd)) || (destination[0] == '/' ?
        f_copy(output, F_PATH, destination) : f_path(output, F_PATH, cwd, destination)))
        return precompile_error("output path too long or current directory unavailable");
    if (!staged) {
        if (!realpath(manifest, input) || !realpath("manifest.json", expected) || strcmp(input, expected))
            return precompile_error("run in the copied example repository with its manifest.json input");
        return 0;
    }
    if (f_copy(directory, sizeof(directory), manifest)) return precompile_error("manifest path too long");
    separator = strrchr(directory, '/');
    if (strcmp(separator ? separator + 1 : directory, "manifest.json"))
        return precompile_error("manifest path must be manifest.json");
    if (separator) {
        *separator = '\0';
        if (chdir(*directory ? directory : "/")) return precompile_error("manifest directory unavailable");
    }
    return 0;
}
int main(int argc, char **argv) {
    struct manifest manifest = {0};
    json_object *plan = NULL;
    char output[F_PATH];
    const char *serialized;
    bool staged;
    int status = 1;
    if (argc < 2 || (strcmp(argv[1], "manifest") && strcmp(argv[1], "staged"))) goto usage;
    staged = !strcmp(argv[1], "staged");
    if (argc != (staged ? 5 : 4)) goto usage;
    if (staged && !run_id_valid(argv[3])) { precompile_error("invalid workflow run ID"); goto done; }
    if (prepare_paths(argv[2], argv[argc - 1], staged, output)) goto done;
    if (manifest_load(&manifest) || (staged && staged_validate(&manifest, argv[3]))) goto done;
    plan = precompile_lower(&manifest, staged);
    if (!plan) { precompile_error("cannot lower manifest"); goto done; }
    serialized = json_object_to_json_string_ext(plan, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
    if (f_write(output, serialized, strlen(serialized), true)) { precompile_error("cannot write plan"); goto done; }
    status = 0;
    goto done;
usage:
    fprintf(stderr, "usage: %s manifest MANIFEST PLAN | staged MANIFEST RUN_ID PLAN\n", argv[0]);
    status = 2;
done:
    json_object_put(plan); json_object_put(manifest.value); free(manifest.raw);
    return status;
}
