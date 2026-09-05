#define _XOPEN_SOURCE 700
#include "task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* No checkout, filters, hooks, remotes, or working-tree writes during preparation. */
static int git(const char *directory, char *const args[], struct f_capture *cap) {
    char *argv[32] = {"git", "--literal-pathspecs", "-c", "core.hooksPath=/dev/null", "-c", "protocol.file.allow=always", "-C", (char *)directory};
    size_t i;
    for (i = 0; args[i] && i < 22; i++) argv[i + 8] = args[i];
    if (args[i]) return -1;
    argv[i + 8] = NULL;
    if (f_run(argv, NULL, 0, 60, cap)) return -1;
    return cap->status;
}
static int git_ok(const char *directory, char *const args[]) {
    struct f_capture cap = {0}; int status = git(directory, args, &cap);
    f_capture_free(&cap); return status;
}
static int init_bare(const char *directory, const char *commit) {
    char *args[] = {"init", "--bare", "--template=", strlen(commit) == 64 ? "--object-format=sha256" : "--object-format=sha1", NULL};
    return git_ok(directory, args);
}
static int preflight(const char *directory, const char *commit, json_object *work) {
    struct f_capture cap = {0}; int status = -1; char object[1200];
    char *type[] = {"cat-file", "-t", (char *)commit, NULL};
    char *tree[] = {"ls-tree", "-r", (char *)commit, NULL};
    char *lfs[] = {"grep", "-I", "-l", "-e", "^version https://git-lfs.github.com/spec/v1$", (char *)commit, "--", NULL};
    char *exists[] = {"cat-file", "-e", object, NULL};
    char *line;
    if (git(directory, type, &cap) || strcmp(cap.out, "commit\n")) goto done;
    f_capture_free(&cap);
    if (git(directory, tree, &cap)) goto done;
    for (line = cap.out; line && *line;) {
        if (!strncmp(line, "160000 ", 7)) goto done;
        line = strchr(line, '\n'); if (!line) break; line++;
    }
    f_capture_free(&cap);
    if (git(directory, lfs, &cap) != 1) goto done;
    f_capture_free(&cap);
    snprintf(object, sizeof(object), "%s:.hydra-task", commit);
    if (git(directory, exists, &cap) == 0) goto done;
    f_capture_free(&cap);
    if (!strcmp(f_string(work, "kind"), "workflow")) {
        char *file_type[] = {"ls-tree", (char *)commit, "--", (char *)f_string(work, "path"), NULL};
        if (git(directory, file_type, &cap) || (strncmp(cap.out, "100644 ", 7) && strncmp(cap.out, "100755 ", 7))) goto done;
    }
    status = 0;
done:
    f_capture_free(&cap); return status;
}
/* Walk every input component through directory descriptors; never follow links. */
static int input_copy(const char *source, const char *relative, const char *destination) {
    char path[1024], *part, *next; int dir = -1, fd = -1, status = -1; struct stat st;
    char *bytes = NULL; size_t length = 0;
    if (!task_path(relative) || f_copy(path, sizeof(path), relative)) return -1;
    dir = open(source, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (dir < 0) return -1;
    for (part = path; (next = strchr(part, '/')); part = next + 1) {
        *next = '\0'; fd = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(dir); dir = fd; fd = -1; if (dir < 0) goto done;
    }
    fd = openat(dir, part, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > TASK_FILE_LIMIT) goto done;
    bytes = malloc(TASK_FILE_LIMIT + 1); if (!bytes) goto done;
    while (length <= TASK_FILE_LIMIT) {
        ssize_t n = read(fd, bytes + length, TASK_FILE_LIMIT + 1 - length);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) goto done;
        if (!n) break;
        length += (size_t)n;
    }
    if (length > TASK_FILE_LIMIT) goto done;
    status = f_write(destination, bytes, length, false);
done:
    if (dir >= 0) close(dir);
    if (fd >= 0) close(fd);
    free(bytes); return status;
}
static int inputs_prepare(const char *source, const char *scratch, json_object *spec, json_object *payload) {
    json_object *selected = json_object_get(f_field(spec, "inputs")), *manifest = json_object_new_array();
    char path[F_PATH], hash[65]; size_t i, total = 0; int status = -1;
    json_object_object_add(spec, "inputs", manifest);
    if (f_path(path, sizeof(path), scratch, "input")) goto done;
    for (i = 0; i < json_object_array_length(selected); i++) {
        const char *relative = task_text(json_object_array_get_idx(selected, i)); char *hex; json_object *entry;
        if (input_copy(source, relative, path) || f_hash(path, hash) || !(hex = f_hex_read(path))) goto done;
        unlink(path); total += strlen(hex) / 2;
        if (total > TASK_FILE_LIMIT) { free(hex); goto done; }
        entry = json_object_new_object(); f_string_add(entry, "path", relative); f_string_add(entry, "sha256", hash);
        json_object_object_add(entry, "bytes", json_object_new_int64((int64_t)strlen(hex) / 2));
        json_object_array_add(manifest, entry);
        json_object_array_add(payload, json_object_new_string(hex)); free(hex);
    }
    status = 0;
done:
    json_object_put(selected); return status;
}
json_object *task_prepare(const char *source, json_object *draft) {
    char scratch[] = "/tmp/hydra-task-prepare.XXXXXX", bundle[F_PATH], absolute[F_PATH], hash[65];
    json_object *spec = task_spec(draft, false), *package = NULL, *inputs; char *hex = NULL;
    const char *commit; const char *error = "invalid_spec";
    if (!spec) goto done;
    if (!realpath(source, absolute) || !mkdtemp(scratch)) { error = "io_failed"; goto done; }
    commit = f_string(f_field(spec, "source"), "commit"); error = "source_preflight_failed";
    if (preflight(absolute, commit, f_field(spec, "work")) || init_bare(scratch, commit)) goto clean;
    {
        char *fetch[] = {"fetch", "--no-tags", "--no-recurse-submodules", "--", absolute, (char *)commit, NULL};
        char *ref[] = {"update-ref", "refs/heads/task-source", (char *)commit, NULL};
        char *create[] = {"bundle", "create", bundle, "refs/heads/task-source", NULL};
        if (f_path(bundle, sizeof(bundle), scratch, "source.bundle") || git_ok(scratch, fetch) || git_ok(scratch, ref) || git_ok(scratch, create)) goto clean;
    }
    error = "package_too_large";
    if (!(hex = f_hex_read(bundle)) || strlen(hex) > TASK_PACKAGE_LIMIT - TASK_FILE_LIMIT * 2 || f_hash(bundle, hash)) goto clean;
    f_string_add(f_field(spec, "source"), "bundle_sha256", hash);
    package = json_object_new_object(); json_object_object_add(package, "schema_version", json_object_new_int(1));
    json_object_object_add(package, "spec", json_object_get(spec)); f_string_add(package, "bundle_hex", hex);
    inputs = json_object_new_array(); json_object_object_add(package, "input_hex", inputs);
    error = "invalid_input_file";
    if (inputs_prepare(absolute, scratch, spec, inputs)) goto clean;
    {
        json_object *validated = task_spec(spec, true);
        error = "invalid_spec";
        if (!validated) goto clean;
        json_object_put(validated);
    }
    error = "io_failed";
    if (task_json_hash(spec, scratch, hash)) goto clean;
    f_string_add(package, "spec_sha256", hash);
    error = "package_too_large";
    if (strlen(json_object_to_json_string_ext(package, JSON_C_TO_STRING_PLAIN)) > TASK_PACKAGE_LIMIT) goto clean;
    error = NULL;
clean:
    f_remove_tree(scratch);
done:
    free(hex); json_object_put(spec);
    if (!error) return f_success("fleet-task-prepare", package);
    json_object_put(package);
    return f_error("fleet-task-prepare", error, "require an exact local commit without submodules or LFS pointers, a valid task specification, and selected regular files within the transfer limits");
}
json_object *task_inspect(json_object *package) {
    const char *const keys[] = {"schema_version", "spec", "bundle_hex", "input_hex", "spec_sha256", NULL};
    char scratch[] = "/tmp/hydra-task-inspect.XXXXXX", path[F_PATH], hash[65];
    json_object *spec = NULL, *payload = f_field(package, "input_hex"), *manifest, *result = NULL;
    const char *hex = f_string(package, "bundle_hex"), *digest = f_string(package, "spec_sha256"); size_t i, total = 0;
    if (!task_keys(package, keys) || !f_number_is(package, "schema_version", 1) || !task_hex(digest, 64) ||
        strlen(json_object_to_json_string_ext(package, JSON_C_TO_STRING_PLAIN)) > TASK_PACKAGE_LIMIT ||
        !(spec = task_spec(f_field(package, "spec"), true)) || !json_object_is_type(payload, json_type_array)) goto done;
    manifest = f_field(spec, "inputs");
    if (json_object_array_length(manifest) != json_object_array_length(payload) || !mkdtemp(scratch)) goto done;
    if (task_json_hash(spec, scratch, hash) || strcmp(hash, digest) || f_path(path, sizeof(path), scratch, "source.bundle") ||
        f_hex_write(path, hex, 0600) || f_hash(path, hash) || strcmp(hash, f_string(f_field(spec, "source"), "bundle_sha256"))) goto clean;
    for (i = 0; i < json_object_array_length(manifest); i++) {
        json_object *file = json_object_array_get_idx(manifest, i);
        const char *bytes = task_text(json_object_array_get_idx(payload, i));
        if (!bytes || strlen(bytes) / 2 != (size_t)json_object_get_int64(f_field(file, "bytes"))) goto clean;
        total += strlen(bytes) / 2; if (total > TASK_FILE_LIMIT) goto clean;
        if (f_path(path, sizeof(path), scratch, "input") || f_hex_write(path, bytes, 0600) || f_hash(path, hash) || strcmp(hash, f_string(file, "sha256"))) goto clean;
        unlink(path);
    }
    {
        const char *commit = f_string(f_field(spec, "source"), "commit");
        char *verify[] = {"bundle", "verify", "source.bundle", NULL};
        char *heads[] = {"bundle", "list-heads", "source.bundle", NULL};
        char *fetch[] = {"fetch", "--no-tags", "source.bundle", "refs/heads/task-source", NULL};
        char *resolve[] = {"rev-parse", "FETCH_HEAD", NULL}; struct f_capture cap = {0}; bool matches; char expected[128];
        if (init_bare(scratch, commit) || git_ok(scratch, verify)) goto clean;
        snprintf(expected, sizeof(expected), "%s refs/heads/task-source\n", commit);
        matches = !git(scratch, heads, &cap) && !strcmp(cap.out, expected);
        f_capture_free(&cap);
        if (!matches || git_ok(scratch, fetch)) goto clean;
        matches = !git(scratch, resolve, &cap) && strlen(cap.out) == strlen(commit) + 1 && !strncmp(cap.out, commit, strlen(commit));
        f_capture_free(&cap);
        if (!matches || preflight(scratch, commit, f_field(spec, "work"))) goto clean;
    }
    result = f_success("fleet-task-inspect", json_object_get(spec));
clean:
    f_remove_tree(scratch);
done:
    json_object_put(spec);
    return result ? result : f_error("fleet-task-inspect", "invalid_package", "task specification, source bundle, or selected input failed validation; no work was launched");
}
