#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/transport/bundle.h"
#include "fleet/fleet.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool f_package_path(const char *path) {
    const char *file;
    if (!path) return false;
    if (!strcmp(path, "bin/hydra") || !strcmp(path, "libexec/hydra/hydra-fleet") || !strcmp(path, "share/licenses/hydra/LICENSE") || !strcmp(path, "share/licenses/hydra/json-c.txt")) return true;
    if (strncmp(path, "lib/hydra/", 10)) return false;
    file = path + 10;
    return f_name(file) && strlen(file) > 3 && !strcmp(file + strlen(file) - 3, ".sh");
}
static int package_file(json_object *files, const char *path, const char *source) {
    char *hex = f_hex_read(source); json_object *file;
    if (!hex) return -1;
    file = json_object_new_object(); f_string_add(file, "path", path); f_string_add(file, "hex", hex); free(hex);
    json_object_array_add(files, file); return 0;
}
json_object *f_package(const char *source, const char *binary) {
    json_object *package = json_object_new_object(), *files = json_object_new_array();
    char path[F_PATH], root[F_PATH]; DIR *dir = NULL; struct dirent *entry; bool ok = false;
    json_object_object_add(package, "schema_version", json_object_new_int(1)); f_string_add(package, "kind", "install");
    f_string_add(package, "hydra_version", F_VERSION); json_object_object_add(package, "files", files);
    if (f_path(path, sizeof(path), source, "bin/hydra") || package_file(files, "bin/hydra", path) || package_file(files, "libexec/hydra/hydra-fleet", binary) || f_path(root, sizeof(root), source, "lib") || !(dir = opendir(root))) goto done;
    while ((entry = readdir(dir))) {
        char rel[F_PATH]; size_t n = strlen(entry->d_name);
        if (n <= 3 || strcmp(entry->d_name + n - 3, ".sh")) continue;
        if (!f_name(entry->d_name) || f_path(path, sizeof(path), root, entry->d_name) || f_path(rel, sizeof(rel), "lib/hydra", entry->d_name) || package_file(files, rel, path)) goto done;
    }
    if (f_path(path, sizeof(path), source, "LICENSE") || package_file(files, "share/licenses/hydra/LICENSE", path) || f_path(path, sizeof(path), source, "docs/licenses/json-c.txt") || package_file(files, "share/licenses/hydra/json-c.txt", path)) goto done;
    if (strlen(json_object_to_json_string_ext(package, JSON_C_TO_STRING_PLAIN)) > F_LIMIT - 1024) goto done;
    ok = true;
done:
    if (dir) closedir(dir);
    if (!ok) { json_object_put(package); return f_error("fleet-package", "package_failed", "source and target-platform fleet binary must be regular files within the package size limit"); }
    return f_success("fleet-package", package);
}
static const char *package_binary(json_object *package) {
    json_object *files = f_field(package, "files"); const char *hex = NULL; size_t i;
    if (!json_object_is_type(files, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *entry = json_object_array_get_idx(files, i); const char *path = f_string(entry, "path");
        if (!f_package_path(path)) return NULL;
        if (!strcmp(path, "libexec/hydra/hydra-fleet")) hex = f_string(entry, "hex");
    }
    return hex;
}
static bool install_path(struct f_remote *remote, json_object *result, const char *prefix) {
    const char *exe = f_string(f_field(result, "data"), "hydra"); char expected[F_PATH];
    if (!exe || exe[0] != '/') return false;
    if (prefix && (f_path(expected, sizeof(expected), prefix, "bin/hydra") || strcmp(exe, expected))) return false;
    return f_copy(remote->hydra, sizeof(remote->hydra), exe) == 0;
}
json_object *f_bootstrap(struct f_remote *remote, const char *file, const char *digest, const char *prefix, unsigned seconds) {
    char actual[65], binaryhash[65], temp[] = "/tmp/hydra-fleet-binary.XXXXXX", command[F_PATH * 4 + 4096];
    char *text = NULL, *binary = NULL, *input = NULL, *quoted_prefix = NULL; json_object *package = NULL, *result = NULL;
    const char *hex = NULL; size_t binarysize; int fd = -1; bool temporary_owned = false; FILE *fp = NULL; struct f_capture cap = {0};
    if (!digest || f_hash(file, actual) || strcmp(actual, digest) || !(text = f_read(file, F_LIMIT)) || !(package = f_parse(text))) goto done;
    hex = package_binary(package);
    if (!hex) goto done;
    fd = mkstemp(temp); if (fd < 0) goto done; temporary_owned = true; close(fd); fd = -1; unlink(temp);
    if (f_hex_write(temp, hex, 0700) || f_hash(temp, binaryhash)) goto done;
    binarysize = strlen(hex) / 2; binary = malloc(binarysize); if (!binary || !(fp = fopen(temp, "rb")) || fread(binary, 1, binarysize, fp) != binarysize) goto done;
    fclose(fp); fp = NULL;
    input = malloc(binarysize + strlen(text)); if (!input) goto done;
    memcpy(input, binary, binarysize); memcpy(input + binarysize, text, strlen(text));
    quoted_prefix = f_quote(prefix ? prefix : "");
    if (!quoted_prefix) goto done;
    /* Hashes are verified; the optional exact prefix is shell quoted. */
    snprintf(command, sizeof(command),
        "set -eu; command -v git >/dev/null; "
        "stage=$(mktemp -d); trap 'rm -rf \"$stage\"' EXIT HUP INT TERM; "
        "cat > \"$stage/input\"; head -c %zu \"$stage/input\" > \"$stage/fleet\"; "
        "if command -v sha256sum >/dev/null; then actual=$(sha256sum \"$stage/fleet\"); else actual=$(shasum -a 256 \"$stage/fleet\"); fi; "
        "test \"${actual%%%% *}\" = '%s'; chmod 700 \"$stage/fleet\"; tail -c +%zu \"$stage/input\" | \"$stage/fleet\" install '%s'%s%s", binarysize, binaryhash, binarysize + 1, digest, prefix ? " --prefix " : "", prefix ? quoted_prefix : "");
    if (f_ssh(remote, command, input, binarysize + strlen(text), seconds, false, &cap)) goto done;
    result = f_parse(cap.out);
    if (!result || (cap.status && json_object_get_boolean(f_field(result, "ok")))) { json_object_put(result); result = f_error("fleet-bootstrap", "outcome_unknown", "installation may have run; reconcile its exact prefix before retrying"); }
    if (json_object_get_boolean(f_field(result, "ok")) && !install_path(remote, result, prefix)) {
        json_object_put(result); result = f_error("fleet-bootstrap", "outcome_unknown", "returned install path disagrees with the reviewed prefix");
    }
done:
    if (fd >= 0) close(fd);
    if (fp) fclose(fp);
    if (temporary_owned) unlink(temp);
    free(text); free(binary); free(input); free(quoted_prefix); json_object_put(package); f_capture_free(&cap);
    return result ? result : f_error("fleet-bootstrap", "invalid_package", "package hash, content, or binary is invalid");
}
