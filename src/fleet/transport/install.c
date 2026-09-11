#include "fleet/transport/bundle.h"
#include "fleet/transport/remote.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool package_valid(json_object *package, const char *digest) {
    json_object *files = f_field(package, "files"); const char *kind = f_string(package, "kind");
    size_t i, j; bool shell = false, native = false;
    if (!digest || strlen(digest) != 64 || strspn(digest, "0123456789abcdef") != 64 ||
        !kind || strcmp(kind, "install") || !f_number_is(package, "schema_version", 1) ||
        !json_object_is_type(files, json_type_array) || json_object_array_length(files) > 256) return false;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); const char *path = f_string(file, "path");
        if (!f_package_path(path) || !f_string(file, "hex")) return false;
        shell |= !strcmp(path, "bin/hydra"); native |= !strcmp(path, "libexec/hydra/hydra-fleet");
        for (j = 0; j < i; j++) if (!strcmp(path, f_string(json_object_array_get_idx(files, j), "path"))) return false;
    }
    return shell && native;
}
static int destination(const char *digest, const char *prefix, char dest[F_PATH]) {
    char root[F_PATH];
    if (prefix) return prefix[0] != '/' ? -1 : f_copy(dest, F_PATH, prefix);
    if (!getenv("HOME") || f_path(root, sizeof(root), getenv("HOME"), ".local/share/hydra/fleet")) return -1;
    return f_path(dest, F_PATH, root, digest);
}
static bool installed_bytes(json_object *files, const char *dest) {
    struct stat st; size_t i;
    if (lstat(dest, &st) || !S_ISDIR(st.st_mode)) return false;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); char path[F_PATH], *hex; bool match;
        if (f_path(path, sizeof(path), dest, f_string(file, "path")) || !(hex = f_hex_read(path))) return false;
        match = !strcmp(hex, f_string(file, "hex")); free(hex);
        if (!match) return false;
    }
    return true;
}
static json_object *installed_result(const char *dest, const char *digest) {
    char exe[F_PATH]; json_object *data;
    if (f_path(exe, sizeof(exe), dest, "bin/hydra")) return NULL;
    data = json_object_new_object(); f_string_add(data, "hydra", exe); f_string_add(data, "sha256", digest);
    return f_success("fleet-bootstrap", data);
}
json_object *f_install_check(json_object *package, const char *digest, const char *prefix) {
    char dest[F_PATH];
    if (!package_valid(package, digest) || destination(digest, prefix, dest) || !installed_bytes(f_field(package, "files"), dest))
        return f_error("fleet-bootstrap", "outcome_unknown", "reviewed installed bytes are absent or disagree; no installation was attempted");
    return installed_result(dest, digest);
}
static int write_files(json_object *files, const char *stage) {
    size_t i;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); const char *rel = f_string(file, "path");
        char path[F_PATH], parent[F_PATH]; unsigned mode = !strcmp(rel, "bin/hydra") || !strcmp(rel, "libexec/hydra/hydra-fleet") ? 0755 : 0644;
        if (f_path(path, sizeof(path), stage, rel) || f_copy(parent, sizeof(parent), path)) return -1;
        *strrchr(parent, '/') = '\0';
        if (f_mkdirs(parent) || f_hex_write(path, f_string(file, "hex"), mode)) return -1;
    }
    return 0;
}
static bool executable_version(const char *stage, const char *relative, const char *expected) {
    char exe[F_PATH]; struct f_capture cap = {0}; bool ok;
    if (f_path(exe, sizeof(exe), stage, relative)) return false;
    char *argv[] = {exe, "--version", NULL};
    ok = !f_run(argv, NULL, 0, 5, &cap) && !cap.status && strstr(cap.out, expected);
    f_capture_free(&cap); return ok;
}
static bool qualify_stage(const char *stage) {
    char checkhome[F_PATH], exe[F_PATH]; struct f_capture cap = {0}; json_object *reply = NULL; bool ok = false;
    if (f_path(checkhome, sizeof(checkhome), stage, ".qualification") || setenv("HYDRA_HOME", checkhome, 1)) return false;
    unsetenv("HYDRA_ROOT"); unsetenv("HYDRA_FLEET_BIN");
    if (!executable_version(stage, "bin/hydra", "Hydra version 2.") ||
        !executable_version(stage, "libexec/hydra/hydra-fleet", "Hydra fleet protocol 1\n") ||
        f_path(exe, sizeof(exe), stage, "bin/hydra")) goto done;
    char *argv[] = {exe, "fleet", "handshake", "--json", NULL};
    if (f_run(argv, NULL, 0, 5, &cap) || cap.status) goto done;
    reply = f_parse(cap.out); ok = json_object_get_boolean(f_field(reply, "ok")) && f_handshake_compatible(f_field(reply, "data"));
done:
    json_object_put(reply); f_capture_free(&cap);
    if (!access(checkhome, F_OK) && f_remove_tree(checkhome)) ok = false;
    return ok;
}
json_object *f_install(json_object *package, const char *digest, const char *prefix) {
    char dest[F_PATH], parent[F_PATH], stage[F_PATH] = ""; struct stat st; json_object *result = NULL;
    if (!package_valid(package, digest) || destination(digest, prefix, dest)) goto done;
    if (!lstat(dest, &st)) return f_install_check(package, digest, prefix);
    if (f_copy(parent, sizeof(parent), dest) || !strrchr(parent, '/')) goto done;
    *strrchr(parent, '/') = '\0';
    if (f_mkdirs(parent) || snprintf(stage, sizeof(stage), "%s/.install.XXXXXX", parent) >= (int)sizeof(stage) || !mkdtemp(stage)) { stage[0] = '\0'; goto done; }
    if (write_files(f_field(package, "files"), stage) || !qualify_stage(stage)) goto done;
    if (!lstat(dest, &st)) result = f_install_check(package, digest, prefix);
    else if (!rename(stage, dest)) { stage[0] = '\0'; result = installed_result(dest, digest); }
done:
    if (stage[0]) f_remove_tree(stage);
    return result ? result : f_error("fleet-bootstrap", "install_failed", "package paths, executable platform, or exact destination failed validation");
}
static bool input_digest(const char *text, const char *digest) {
    struct f_capture cap = {0}; char *argv[] = {"shasum", "-a", "256", NULL}; bool matches = false;
    if (!text || !digest || strlen(digest) != 64) return false;
    if (f_run(argv, text, strlen(text), 5, &cap)) goto done;
    if (cap.status == 127) {
        char *fallback[] = {"sha256sum", NULL}; f_capture_free(&cap);
        if (f_run(fallback, text, strlen(text), 5, &cap)) goto done;
    }
    matches = !cap.status && strlen(cap.out) >= 65 && cap.out[64] == ' ' && !strncmp(cap.out, digest, 64);
done:
    f_capture_free(&cap); return matches;
}
json_object *f_install_cli(int argc, char **argv) {
    const char *prefix = argc == 4 ? argv[3] : NULL; char *text; json_object *package, *result;
    if ((argc != 2 && argc != 4) || (argc == 4 && (strcmp(argv[2], "--prefix") || prefix[0] != '/')))
        return f_error("fleet-bootstrap", "invalid_input", "install or install-check DIGEST [--prefix /exact/path]");
    text = f_read(NULL, F_LIMIT); package = text ? f_parse(text) : NULL;
    if (!package || !input_digest(text, argv[1])) result = f_error("fleet-bootstrap", "hash_mismatch", "package digest failed");
    else if (!strcmp(argv[0], "install-check")) result = f_install_check(package, argv[1], prefix);
    else result = f_install(package, argv[1], prefix);
    json_object_put(package); free(text); return result;
}
