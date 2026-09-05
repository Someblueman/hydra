#include "fleet.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool package_path(const char *path) {
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
json_object *f_install(json_object *package, const char *digest) {
    json_object *files = f_field(package, "files"), *result = NULL;
    char root[F_PATH], dest[F_PATH], stage[F_PATH] = "", exe[F_PATH]; size_t i;
    bool shell = false, native = false; struct f_capture cap = {0};
    if (!digest || strlen(digest) != 64 || !f_name(digest) || !f_string(package, "kind") || strcmp(f_string(package, "kind"), "install") || !f_number_is(package, "schema_version", 1) || !json_object_is_type(files, json_type_array) || json_object_array_length(files) > 256) goto done;
    if (!getenv("HOME") || f_path(root, sizeof(root), getenv("HOME"), ".local/share/hydra/fleet") || f_mkdirs(root) || f_path(dest, sizeof(dest), root, digest)) goto done;
    if (snprintf(stage, sizeof(stage), "%s/.install.XXXXXX", root) >= (int)sizeof(stage) || !mkdtemp(stage)) { stage[0] = '\0'; goto done; }
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); const char *rel = f_string(file, "path"), *hex = f_string(file, "hex");
        char path[F_PATH], parent[F_PATH]; unsigned mode;
        if (!package_path(rel) || !hex || f_path(path, sizeof(path), stage, rel)) goto done;
        shell |= !strcmp(rel, "bin/hydra"); native |= !strcmp(rel, "libexec/hydra/hydra-fleet");
        mode = !strcmp(rel, "bin/hydra") || !strcmp(rel, "libexec/hydra/hydra-fleet") ? 0755 : 0644;
        f_copy(parent, sizeof(parent), path); *strrchr(parent, '/') = '\0';
        if (f_mkdirs(parent) || f_hex_write(path, hex, mode)) goto done;
    }
    if (!shell || !native || f_path(exe, sizeof(exe), stage, "bin/hydra")) goto done;
    { char checkhome[F_PATH];
      if (f_path(checkhome, sizeof(checkhome), stage, ".qualification") || setenv("HYDRA_HOME", checkhome, 1)) goto done; }
    unsetenv("HYDRA_ROOT"); unsetenv("HYDRA_FLEET_BIN");
    { char *argv[] = {exe, (char *)"--version", NULL};
      if (f_run(argv, NULL, 0, 5, &cap) || cap.status || !strstr(cap.out, "Hydra version 2.")) goto done;
      f_capture_free(&cap); }
    if (f_path(exe, sizeof(exe), stage, "libexec/hydra/hydra-fleet")) goto done;
    { char *argv[] = {exe, (char *)"--version", NULL};
      if (f_run(argv, NULL, 0, 5, &cap) || cap.status || strcmp(cap.out, "Hydra fleet protocol 1\n")) goto done;
      f_capture_free(&cap); }
    if (f_path(exe, sizeof(exe), stage, "bin/hydra")) goto done;
    { char *argv[] = {exe, (char *)"fleet", (char *)"handshake", (char *)"--json", NULL}; json_object *handshake;
      if (f_run(argv, NULL, 0, 5, &cap) || cap.status) goto done;
      handshake = f_parse(cap.out);
      if (!handshake || json_object_get_int(f_field(f_field(handshake, "data"), "fleet_protocol")) != F_PROTOCOL) { json_object_put(handshake); goto done; }
      json_object_put(handshake); f_capture_free(&cap);
      if (f_path(exe, sizeof(exe), stage, ".qualification") || f_remove_tree(exe)) goto done; }
    /* Existing pins are reusable only if their bytes still match every packaged file. */
    { struct stat st;
      if (!lstat(dest, &st)) {
          if (!S_ISDIR(st.st_mode)) goto done;
          for (i = 0; i < json_object_array_length(files); i++) {
              json_object *file = json_object_array_get_idx(files, i); char path[F_PATH], *hex;
              if (f_path(path, sizeof(path), dest, f_string(file, "path")) || !(hex = f_hex_read(path))) goto done;
              if (strcmp(hex, f_string(file, "hex"))) { free(hex); goto done; } free(hex);
          }
      } else { if (rename(stage, dest)) goto done; stage[0] = '\0'; }
    }
    if (f_path(exe, sizeof(exe), dest, "bin/hydra")) goto done;
    result = json_object_new_object(); f_string_add(result, "hydra", exe); f_string_add(result, "sha256", digest); result = f_success("fleet-bootstrap", result);
done:
    f_capture_free(&cap); if (stage[0]) f_remove_tree(stage);
    return result ? result : f_error("fleet-bootstrap", "install_failed", "package paths, version, executable platform, or existing pinned bytes failed validation");
}
json_object *f_bootstrap(struct f_remote *remote, const char *file, const char *digest, unsigned seconds) {
    char actual[65], binaryhash[65], temp[] = "/tmp/hydra-fleet-binary.XXXXXX", command[4096];
    char *text = NULL, *binary = NULL, *input = NULL; json_object *package = NULL, *files, *result = NULL;
    const char *hex = NULL; size_t i, binarysize; int fd = -1; bool temporary_owned = false; FILE *fp = NULL; struct f_capture cap = {0};
    if (!digest || f_hash(file, actual) || strcmp(actual, digest) || !(text = f_read(file, F_LIMIT)) || !(package = f_parse(text))) goto done;
    files = f_field(package, "files"); if (!json_object_is_type(files, json_type_array)) goto done;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *entry = json_object_array_get_idx(files, i); const char *path = f_string(entry, "path");
        if (!package_path(path)) goto done;
        if (!strcmp(path, "libexec/hydra/hydra-fleet")) hex = f_string(entry, "hex");
    }
    if (!hex) goto done;
    fd = mkstemp(temp); if (fd < 0) goto done; temporary_owned = true; close(fd); fd = -1; unlink(temp);
    if (f_hex_write(temp, hex, 0700) || f_hash(temp, binaryhash)) goto done;
    binarysize = strlen(hex) / 2; binary = malloc(binarysize); if (!binary || !(fp = fopen(temp, "rb")) || fread(binary, 1, binarysize, fp) != binarysize) goto done;
    fclose(fp); fp = NULL;
    input = malloc(binarysize + strlen(text)); if (!input) goto done;
    memcpy(input, binary, binarysize); memcpy(input + binarysize, text, strlen(text));
    /* Only numeric lengths and verified hexadecimal hashes enter this fixed script. */
    snprintf(command, sizeof(command),
        "set -eu; command -v git >/dev/null; command -v tmux >/dev/null; "
        "stage=$(mktemp -d); trap 'rm -rf \"$stage\"' EXIT HUP INT TERM; "
        "head -c %zu > \"$stage/fleet\"; "
        "if command -v sha256sum >/dev/null; then actual=$(sha256sum \"$stage/fleet\"); else actual=$(shasum -a 256 \"$stage/fleet\"); fi; "
        "test \"${actual%%%% *}\" = '%s'; chmod 700 \"$stage/fleet\"; \"$stage/fleet\" install '%s'", binarysize, binaryhash, digest);
    if (f_ssh(remote, command, input, binarysize + strlen(text), seconds, false, &cap)) goto done;
    result = f_parse(cap.out);
    if (!result) result = f_error("fleet-bootstrap", "bootstrap_failed", cap.err);
    if (json_object_get_boolean(f_field(result, "ok"))) {
        const char *exe = f_string(f_field(result, "data"), "hydra");
        if (!exe || *exe != '/' || f_copy(remote->hydra, sizeof(remote->hydra), exe) || f_remote_save(remote)) {
            json_object_put(result); result = f_error("fleet-bootstrap", "alias_update_failed", "remote install may exist; inspect before retrying");
        }
    }
done:
    if (fd >= 0) close(fd);
    if (fp) fclose(fp);
    if (temporary_owned) unlink(temp);
    free(text); free(binary); free(input); json_object_put(package); f_capture_free(&cap);
    return result ? result : f_error("fleet-bootstrap", "invalid_package", "package hash, content, or binary is invalid");
}
