#include "fleet.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int remote_path(char path[F_PATH], const char *name) {
    char dir[F_PATH], filename[160];
    if (!f_name(name) || strlen(name) >= 128 || f_path(dir, sizeof(dir), f_home, "fleet/remotes")) return -1;
    snprintf(filename, sizeof(filename), "%s.json", name);
    return f_path(path, F_PATH, dir, filename);
}
int f_remote_load(const char *name, struct f_remote *remote) {
    char path[F_PATH]; char *text; json_object *obj; int status = -1;
    memset(remote, 0, sizeof(*remote));
    if (remote_path(path, name) || !(text = f_read(path, 16384))) return -1;
    obj = f_parse(text); free(text);
    if (!obj) return -1;
    if (!f_number_is(obj, "schema_version", 1) || !f_target(f_string(obj, "target"))) goto done;
    if (f_copy(remote->name, sizeof(remote->name), name) || f_copy(remote->target, sizeof(remote->target), f_string(obj, "target")) ||
        f_copy(remote->hydra, sizeof(remote->hydra), f_string(obj, "hydra")) || f_copy(remote->home, sizeof(remote->home), f_string(obj, "home"))) goto done;
    if (!remote->hydra[0] || (remote->hydra[0] != '/' && strcmp(remote->hydra, "hydra"))) goto done;
    if (remote->home[0] && remote->home[0] != '/') goto done;
    remote->multiplex = json_object_get_boolean(f_field(obj, "multiplex")); status = 0;
done:
    json_object_put(obj); return status;
}
int f_remote_save(const struct f_remote *remote) {
    char path[F_PATH], dir[F_PATH]; json_object *obj = json_object_new_object(); int status = -1;
    if (remote_path(path, remote->name) || f_path(dir, sizeof(dir), f_home, "fleet/remotes") || f_mkdirs(dir)) goto done;
    json_object_object_add(obj, "schema_version", json_object_new_int(1));
    f_string_add(obj, "target", remote->target); f_string_add(obj, "hydra", remote->hydra); f_string_add(obj, "home", remote->home);
    json_object_object_add(obj, "multiplex", json_object_new_boolean(remote->multiplex));
    { const char *text = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN); status = f_write(path, text, strlen(text), true); }
done:
    json_object_put(obj); return status;
}
json_object *f_remotes(void) {
    char path[F_PATH]; DIR *dir; struct dirent *entry; json_object *names = json_object_new_array();
    if (f_path(path, sizeof(path), f_home, "fleet/remotes") || !(dir = opendir(path))) return names;
    while ((entry = readdir(dir))) {
        char name[128]; size_t n = strlen(entry->d_name);
        if (n <= 5 || n - 5 >= sizeof(name) || strcmp(entry->d_name + n - 5, ".json")) continue;
        memcpy(name, entry->d_name, n - 5); name[n - 5] = '\0';
        if (f_name(name)) json_object_array_add(names, json_object_new_string(name));
    }
    closedir(dir); return names;
}
json_object *f_remote_cli(int argc, char **argv) {
    struct f_remote remote; char path[F_PATH]; int i;
    memset(&remote, 0, sizeof(remote));
    if (argc == 1 && !strcmp(argv[0], "list")) return f_success("remote-list", f_remotes());
    if (argc == 2 && !strcmp(argv[0], "remove") && !remote_path(path, argv[1])) {
        if (unlink(path)) return f_error("remote", "io_failed", "cannot remove remote alias");
        return f_success("remote-remove", json_object_new_object());
    }
    if (argc < 3 || strcmp(argv[0], "add") || !f_name(argv[1]) || !f_target(argv[2]) ||
        f_copy(remote.name, sizeof(remote.name), argv[1]) || f_copy(remote.target, sizeof(remote.target), argv[2]))
        return f_error("remote", "invalid_input", "remote add NAME [USER@]SSH_ALIAS [--hydra /path] [--home /path] [--multiplex]");
    f_copy(remote.hydra, sizeof(remote.hydra), "hydra");
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--multiplex")) remote.multiplex = true;
        else if ((!strcmp(argv[i], "--hydra") || !strcmp(argv[i], "--home")) && i + 1 < argc && argv[i+1][0] == '/') {
            char *dst = !strcmp(argv[i], "--home") ? remote.home : remote.hydra;
            if (f_copy(dst, F_PATH, argv[++i])) return f_error("remote", "invalid_input", "path is too long");
        } else return f_error("remote", "invalid_input", "unknown or missing alias option");
    }
    if (f_remote_save(&remote)) return f_error("remote", "io_failed", "cannot store alias");
    return f_success("remote-add", json_object_new_object());
}
static const char *transport_code(const struct f_capture *cap) {
    if (f_stopped) return "cancelled";
    if (cap->timeout || strstr(cap->err, "Connection timed out") || strstr(cap->err, "Operation timed out")) return "timeout";
    if (strstr(cap->err, "Host key verification failed") || strstr(cap->err, "REMOTE HOST IDENTIFICATION HAS CHANGED")) return "host_key_failed";
    if (strstr(cap->err, "Permission denied") || strstr(cap->err, "Authentication failed")) return "authentication_failed";
    if (cap->status == 255) return "offline";
    if (cap->status == 125) return "output_limit";
    return "remote_failed";
}
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds) {
    struct f_capture cap = {0}; json_object *result = NULL;
    char *exe = f_quote(remote->hydra), *home = f_quote(remote->home); char command[F_PATH * 8 + 128];
    const char *input = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    int n;
    if (!exe || !home) goto done;
    n = snprintf(command, sizeof(command), "env LC_ALL=C %s%s%s %s fleet serve", remote->home[0] ? "HYDRA_HOME=" : "", remote->home[0] ? home : "", remote->home[0] ? "" : "", exe);
    if (n < 0 || n >= (int)sizeof(command) || f_ssh(remote, command, input, strlen(input), seconds, false, &cap)) goto done;
    result = f_parse(cap.out);
    if (result && cap.status && json_object_get_boolean(f_field(result, "ok"))) { json_object_put(result); result = NULL; }
    if (!result || !json_object_is_type(f_field(result, "ok"), json_type_boolean) || !f_number_is(result, "schema_version", 1) || !f_string(result, "command") || (json_object_get_boolean(f_field(result, "ok")) ? !json_object_is_type(f_field(result, "data"), json_type_object) : (!f_string(f_field(result, "error"), "code") || !f_string(f_field(result, "error"), "message") || !f_string(f_field(result, "error"), "recovery")))) {
        json_object_put(result);
        result = f_error("fleet", cap.status ? transport_code(&cap) : "invalid_response", cap.err[0] ? cap.err : "missing or invalid fleet response");
    }
done:
    free(exe); free(home); f_capture_free(&cap);
    return result ? result : f_error("fleet", "transport_failed", "cannot start SSH transport");
}
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds) {
    json_object *req = json_object_new_object(), *res, *data, *caps;
    json_object_object_add(req, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(req, "action", "handshake");
    res = f_request(remote, req, seconds); data = f_field(res, "data"); caps = f_field(data, "capabilities");
    if (!json_object_get_boolean(f_field(res, "ok"))) goto done;
    if (!f_number_is(data, "fleet_protocol", F_PROTOCOL) || !f_number_is(data, "state_schema", 2) || !f_number_is(data, "event_schema", 1) || !f_number_is(data, "json_schema", 1) || !f_string(data, "hydra_version") || strncmp(f_string(data, "hydra_version"), "2.", 2) || !json_object_is_type(caps, json_type_array)) {
        json_object_put(res); res = f_error("fleet", "version_mismatch", "remote Hydra or fleet protocol is incompatible"); goto done;
    }
    if (strcmp(action, "handshake")) {
        size_t i; bool supported = false;
        for (i = 0; i < json_object_array_length(caps); i++) {
            json_object *cap = json_object_array_get_idx(caps, i);
            if (json_object_is_type(cap, json_type_string) && !strcmp(json_object_get_string(cap), action)) supported = true;
        }
        if (!supported) { json_object_put(res); res = f_error("fleet", "capability_unavailable", "remote lacks required capability"); goto done; }
        json_object_put(res); f_string_add(req, "action", action);
        res = f_request(remote, req, seconds);
    }
done:
    if (!strcmp(action, "list") && json_object_get_boolean(f_field(res, "ok")) && !json_object_is_type(f_field(f_field(res, "data"), "heads"), json_type_array)) {
        json_object_put(res); res = f_error("fleet", "invalid_response", "snapshot heads must be an array");
    }
    f_string_add(res, "host", remote->name); json_object_put(req); return res;
}
