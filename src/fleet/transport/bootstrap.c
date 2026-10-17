#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/transport/bundle.h"
#include "fleet/transport/platform.h"
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
static bool regular_file(const char *path) {
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode);
}
/* Packages the first regular candidate below root as path. */
static int package_first(json_object *files, const char *path, const char *root, const char *const *candidates) {
    char source[F_PATH];
    for (; *candidates; candidates++)
        if (!f_path(source, sizeof(source), root, *candidates) && regular_file(source)) return package_file(files, path, source);
    return -1;
}
static bool shell_library(const char *name) {
    size_t n = strlen(name);
    return n > 3 && !strcmp(name + n - 3, ".sh") && f_name(name);
}
static int compare_names(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}
/* Sorted *.sh names of libdir; the caller frees each name and the array. */
static char **library_names(const char *libdir, size_t *count) {
    DIR *dir = opendir(libdir); struct dirent *entry; char **names = NULL, **grown; size_t used = 0;
    if (!dir) return NULL;
    while ((entry = readdir(dir))) {
        if (!shell_library(entry->d_name)) continue;
        grown = realloc(names, (used + 1) * sizeof(*names));
        if (!grown || !(grown[used] = strdup(entry->d_name))) { names = grown ? grown : names; break; }
        names = grown; used++;
    }
    closedir(dir);
    if (entry || !used) { while (used) free(names[--used]); free(names); return NULL; }
    qsort(names, used, sizeof(*names), compare_names);
    *count = used; return names;
}
/* Installed prefixes keep libraries in lib/hydra; source trees in lib. */
static int package_libraries(json_object *files, const char *root) {
    char libdir[F_PATH], installed[F_PATH], path[F_PATH], rel[F_PATH]; struct stat st; char **names; size_t count = 0, i; int status = 0;
    if (f_path(installed, sizeof(installed), root, "lib/hydra") || f_path(libdir, sizeof(libdir), root, "lib")) return -1;
    if (!lstat(installed, &st) && S_ISDIR(st.st_mode)) memcpy(libdir, installed, sizeof(libdir));
    if (!(names = library_names(libdir, &count))) return -1;
    for (i = 0; i < count; i++) {
        if (!status && (f_path(path, sizeof(path), libdir, names[i]) || f_path(rel, sizeof(rel), "lib/hydra", names[i]) || package_file(files, rel, path))) status = -1;
        free(names[i]);
    }
    free(names); return status;
}
static int package_contents(json_object *files, const char *root, const char *binary) {
    static const char *const shell[] = {"bin/hydra", NULL};
    static const char *const license[] = {"LICENSE", "share/licenses/hydra/LICENSE", NULL};
    static const char *const jsonc[] = {"docs/licenses/json-c.txt", "share/licenses/hydra/json-c.txt", "libexec/hydra/hydra-fleet.LICENSE", NULL};
    if (package_first(files, "bin/hydra", root, shell) || !regular_file(binary) || package_file(files, "libexec/hydra/hydra-fleet", binary) ||
        package_libraries(files, root)) return -1;
    return package_first(files, "share/licenses/hydra/LICENSE", root, license) || package_first(files, "share/licenses/hydra/json-c.txt", root, jsonc) ? -1 : 0;
}
static bool binary_matches(const char *binary, const struct f_platform *platform) {
    struct f_platform actual;
    return !platform || (!f_platform_binary(binary, &actual) && f_platform_equal(&actual, platform));
}
json_object *f_package_installation(const char *root, const char *binary, const struct f_platform *platform) {
    json_object *package = json_object_new_object(), *files = json_object_new_array();
    json_object_object_add(package, "schema_version", json_object_new_int(1)); f_string_add(package, "kind", "install");
    f_string_add(package, "hydra_version", F_VERSION); json_object_object_add(package, "files", files);
    if (platform) json_object_object_add(package, "platform", f_platform_json(platform));
    if (!binary_matches(binary, platform)) {
        json_object_put(package);
        return f_error("fleet-package", "platform_mismatch", "the fleet binary is not an executable for the requested platform");
    }
    if (package_contents(files, root, binary) || strlen(json_object_to_json_string_ext(package, JSON_C_TO_STRING_PLAIN)) > F_LIMIT - 1024) {
        json_object_put(package);
        return f_error("fleet-package", "package_failed", "source tree or installed prefix and target-platform fleet binary must be regular files within the package size limit");
    }
    return f_success("fleet-package", package);
}
json_object *f_package(const char *source, const char *binary) {
    return f_package_installation(source, binary, NULL);
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

/* Verified package text plus the decoded fleet binary that goes first on stdin. */
struct boot_input {
    char *text, *input, hash[65], guard[1024];
    size_t binarysize, textsize;
};
static void boot_free(struct boot_input *boot) { free(boot->text); free(boot->input); }
/* Decodes the package's fleet binary through a private temporary file so its
 * digest is computed by the same tool the remote uses. */
static char *decode_binary(const char *hex, size_t *size, char hash[65]) {
    char temp[] = "/tmp/hydra-fleet-binary.XXXXXX", *bytes = NULL; FILE *fp = NULL; int fd = mkstemp(temp);
    if (fd < 0) return NULL;
    close(fd); unlink(temp);
    *size = strlen(hex) / 2;
    if (f_hex_write(temp, hex, 0700) || f_hash(temp, hash) || !(bytes = malloc(*size + 1)) || !(fp = fopen(temp, "rb")) || fread(bytes, 1, *size, fp) != *size) {
        free(bytes); bytes = NULL;
    }
    if (fp) fclose(fp);
    unlink(temp);
    return bytes;
}
/* Shell guard: refuses before any package binary runs on a different
 * platform, answering with a fleet-bootstrap platform_mismatch envelope. */
static int platform_guard(json_object *package, char guard[1024]) {
    struct f_platform platform; char text[F_PLATFORM_TEXT]; int found = f_package_platform(package, &platform);
    guard[0] = '\0';
    if (found) return found < 0 ? -1 : 0;
    f_platform_text(&platform, text);
    snprintf(guard, 1024,
        "p=$(uname -s)-$(uname -m); case \"$p\" in Linux-*) p=linux-${p#*-};; Darwin-*) p=darwin-${p#*-};; esac; "
        "case \"$p\" in *-amd64) p=${p%%-*}-x86_64;; *-arm64) p=${p%%-*}-aarch64;; esac; "
        "if [ \"$p\" != '%s' ]; then p=$(printf '%%s' \"$p\" | tr -cd 'A-Za-z0-9_.-'); "
        "printf '{\"schema_version\":1,\"ok\":false,\"command\":\"fleet-bootstrap\",\"error\":{\"code\":\"platform_mismatch\","
        "\"message\":\"remote platform %%s does not match package platform %s; nothing was installed\","
        "\"recovery\":\"package a fleet helper built for the remote platform\"},\"data\":{\"remote_platform\":\"%%s\",\"package_platform\":\"%s\"}}\\n' \"$p\" \"$p\"; exit 1; fi; ",
        text, text, text);
    return 0;
}
static int boot_load(const char *file, const char *digest, struct boot_input *boot) {
    char actual[65], *binary; json_object *package = NULL; const char *hex; size_t total; int status = -1;
    if (!digest || f_hash(file, actual) || strcmp(actual, digest) || !(boot->text = f_read(file, F_LIMIT)) || !(package = f_parse(boot->text))) goto done;
    boot->textsize = strlen(boot->text);
    if (!(hex = package_binary(package)) || platform_guard(package, boot->guard) || !(binary = decode_binary(hex, &boot->binarysize, boot->hash))) goto done;
    /* Both parts come from one package read with the F_LIMIT bound. */
    total = boot->binarysize + boot->textsize;
    if (total <= F_LIMIT + F_LIMIT / 2) boot->input = malloc(total);
    if (boot->input) {
        memcpy(boot->input, binary, boot->binarysize); memcpy(boot->input + boot->binarysize, boot->text, boot->textsize);
        status = 0;
    }
    free(binary);
done:
    json_object_put(package); return status;
}
static int boot_command(char *command, size_t size, const struct boot_input *boot, const char *digest, const char *prefix) {
    char *quoted = f_quote(prefix ? prefix : ""); int n;
    if (!quoted) return -1;
    /* Hashes are verified; the optional exact prefix is shell quoted. */
    n = snprintf(command, size,
        "set -eu; command -v git >/dev/null; "
        "stage=$(mktemp -d); trap 'rm -rf \"$stage\"' EXIT HUP INT TERM; "
        "cat > \"$stage/input\"; %s"
        "head -c %zu \"$stage/input\" > \"$stage/fleet\"; "
        "if command -v sha256sum >/dev/null; then actual=$(sha256sum \"$stage/fleet\"); else actual=$(shasum -a 256 \"$stage/fleet\"); fi; "
        "test \"${actual%%%% *}\" = '%s'; chmod 700 \"$stage/fleet\"; tail -c +%zu \"$stage/input\" | \"$stage/fleet\" install '%s'%s%s",
        boot->guard, boot->binarysize, boot->hash, boot->binarysize + 1, digest, prefix ? " --prefix " : "", prefix ? quoted : "");
    free(quoted);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}
static json_object *boot_result(struct f_remote *remote, const struct f_capture *cap, const char *prefix) {
    json_object *result = f_parse(cap->out);
    if (!result || (cap->status && json_object_get_boolean(f_field(result, "ok")))) {
        json_object_put(result);
        return f_error("fleet-bootstrap", "outcome_unknown", "installation may have run; reconcile its exact prefix before retrying");
    }
    if (json_object_get_boolean(f_field(result, "ok")) && !install_path(remote, result, prefix)) {
        json_object_put(result);
        return f_error("fleet-bootstrap", "outcome_unknown", "returned install path disagrees with the reviewed prefix");
    }
    return result;
}
json_object *f_bootstrap(struct f_remote *remote, const char *file, const char *digest, const char *prefix, unsigned seconds) {
    char command[F_PATH * 4 + 8192]; struct boot_input boot; struct f_capture cap = {0}; json_object *result = NULL;
    memset(&boot, 0, sizeof(boot));
    if (!boot_load(file, digest, &boot) && !boot_command(command, sizeof(command), &boot, digest, prefix) &&
        !f_ssh(remote, command, boot.input, boot.binarysize + boot.textsize, seconds, false, &cap))
        result = boot_result(remote, &cap, prefix);
    boot_free(&boot); f_capture_free(&cap);
    return result ? result : f_error("fleet-bootstrap", "invalid_package", "package hash, content, or binary is invalid");
}
