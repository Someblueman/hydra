#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/fleet.h"
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

bool f_target(const char *s) {
    if (!s || !isalnum((unsigned char)*s)) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.-@:", *s)) return false;
    return true;
}

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
        f_copy(remote->hydra, sizeof(remote->hydra), f_string(obj, "hydra")) || f_copy(remote->home, sizeof(remote->home), f_string(obj, "home")) || f_copy(remote->ssh_config, sizeof(remote->ssh_config), f_string(obj, "ssh_config"))) goto done;
    if (f_copy(remote->project, sizeof(remote->project), f_string(obj, "project")) ||
        f_copy(remote->accepted_host_key, sizeof(remote->accepted_host_key), f_string(obj, "accepted_host_key")) ||
        f_copy(remote->principal, sizeof(remote->principal), f_string(obj, "principal"))) goto done;
    if (!remote->hydra[0] || (remote->hydra[0] != '/' && strcmp(remote->hydra, "hydra"))) goto done;
    if (remote->home[0] && remote->home[0] != '/') goto done;
    remote->multiplex = json_object_get_boolean(f_field(obj, "multiplex")); status = 0;
done:
    json_object_put(obj); return status;
}
static json_object *remote_record(const struct f_remote *remote) {
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "schema_version", json_object_new_int(1));
    f_string_add(obj, "target", remote->target); f_string_add(obj, "hydra", remote->hydra); f_string_add(obj, "home", remote->home);
    if (remote->ssh_config[0]) f_string_add(obj, "ssh_config", remote->ssh_config);
    if (remote->principal[0]) f_string_add(obj, "principal", remote->principal);
    if (remote->project[0]) f_string_add(obj, "project", remote->project);
    if (remote->accepted_host_key[0]) f_string_add(obj, "accepted_host_key", remote->accepted_host_key);
    json_object_object_add(obj, "multiplex", json_object_new_boolean(remote->multiplex));
    return obj;
}
int f_remote_save(const struct f_remote *remote) {
    char path[F_PATH], dir[F_PATH]; json_object *obj = remote_record(remote); int status = -1;
    if (remote_path(path, remote->name) || f_path(dir, sizeof(dir), f_home, "fleet/remotes") || f_mkdirs(dir)) goto done;
    const char *text = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
    status = f_write(path, text, strlen(text), true);
done:
    json_object_put(obj); return status;
}
/* Enrollment never overwrites an existing alias, including one installed by
 * another actor after preflight. Identical duplicate publication is harmless. */
static int sync_alias_directory(void) {
    char directory[F_PATH]; int fd, status;
    if (f_path(directory, sizeof(directory), f_home, "fleet/remotes")) return -1;
    fd = open(directory, O_RDONLY); if (fd < 0) return -1;
    status = fsync(fd); close(fd); return status;
}
int f_remote_enrolled(const struct f_remote *remote, bool publish) {
    char path[F_PATH], dir[F_PATH]; json_object *expected = remote_record(remote), *existing = NULL; int status = -1;
    if (remote_path(path, remote->name)) goto done;
    if (!access(path, F_OK)) {
        existing = f_read_json(path, 16384); status = json_object_equal(existing, expected) ? 0 : -1; goto done;
    }
    if (!publish) { status = 0; goto done; }
    if (f_path(dir, sizeof(dir), f_home, "fleet/remotes") || f_mkdirs(dir)) goto done;
    const char *text = json_object_to_json_string_ext(expected, JSON_C_TO_STRING_PLAIN);
    if (!f_write(path, text, strlen(text), false)) status = 0;
    else { existing = f_read_json(path, 16384); status = json_object_equal(existing, expected) ? 0 : -1; }
done:
    if (!status && publish) status = sync_alias_directory();
    json_object_put(existing); json_object_put(expected); return status;
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
static char *peer_from_log(const char *text) {
    const char *start, *end;
    if (!text || !(start = strstr(text, "Server host key: ")) || !(start = strstr(start, "SHA256:"))) return NULL;
    end = start + 7;
    while (*end && strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=", *end)) end++;
    if (end == start + 7 || end - start >= 256) return NULL;
    return strndup(start, (size_t)(end - start));
}
static void response_peer(json_object *result, const struct f_remote *remote, const char *stderr_text) {
    json_object *data = f_field(result, "data"); char *log = NULL, *peer = NULL;
    if (!json_object_is_type(data, json_type_object)) return;
    /* Remote JSON cannot supply transport identity. Only a local SSH diagnostic
     * or the authenticated master required by this call may provide it. */
    json_object_object_del(data, "peer_fingerprint");
    if (remote->require_existing_master && remote->peer_fingerprint[0]) peer = strdup(remote->peer_fingerprint);
    else {
        if (remote->ssh_log[0]) log = f_read(remote->ssh_log, 131072);
        peer = peer_from_log(remote->ssh_log[0] ? log : stderr_text);
    }
    if (peer) f_string_add(data, "peer_fingerprint", peer);
    free(peer); free(log);
}
static bool response_valid(json_object *result) {
    json_object *error = f_field(result, "error");
    if (!json_object_is_type(f_field(result, "ok"), json_type_boolean) ||
        !f_number_is(result, "schema_version", 1) || !f_string(result, "command")) return false;
    if (json_object_get_boolean(f_field(result, "ok"))) return json_object_is_type(f_field(result, "data"), json_type_object);
    return f_string(error, "code") && f_string(error, "message") && f_string(error, "recovery");
}
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds) {
    struct f_capture cap = {0}; json_object *result = NULL;
    char *exe = f_quote(remote->hydra), *home = f_quote(remote->home); char command[F_PATH * 8 + 128];
    const char *input = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN); int n;
    if (!exe || !home) goto done;
    n = snprintf(command, sizeof(command), "env LC_ALL=C %s%s %s fleet serve", remote->home[0] ? "HYDRA_HOME=" : "", remote->home[0] ? home : "", exe);
    if (n < 0 || n >= (int)sizeof(command) || f_ssh(remote, command, input, strlen(input), seconds, false, &cap)) goto done;
    result = f_parse(cap.out);
    if (result && cap.status && json_object_get_boolean(f_field(result, "ok"))) { json_object_put(result); result = NULL; }
    if (!response_valid(result)) {
        json_object_put(result);
        result = f_error("fleet", cap.status ? transport_code(&cap) : "invalid_response", cap.err[0] ? cap.err : "missing or invalid fleet response");
    } else if (json_object_get_boolean(f_field(result, "ok"))) {
        response_peer(result, remote, cap.err);
    }
 done:
    free(exe); free(home); f_capture_free(&cap);
    return result ? result : f_error("fleet", "transport_failed", "cannot start SSH transport");
}
bool f_handshake_compatible(json_object *data) {
    const char *version = f_string(data, "hydra_version");
    return f_number_is(data, "fleet_protocol", F_PROTOCOL) && f_number_is(data, "state_schema", 2) &&
        f_number_is(data, "event_schema", 1) && f_number_is(data, "json_schema", 1) &&
        version && !strncmp(version, "2.", 2) && json_object_is_type(f_field(data, "capabilities"), json_type_array);
}
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds) {
    json_object *req = json_object_new_object(), *res, *data, *caps;
    json_object_object_add(req, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(req, "action", "handshake");
    res = f_request(remote, req, seconds); data = f_field(res, "data"); caps = f_field(data, "capabilities");
    if (!json_object_get_boolean(f_field(res, "ok"))) goto done;
    if (!f_handshake_compatible(data)) {
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

static int ssh_control(const struct f_remote *remote, char socket[F_PATH]) {
    char dir[F_PATH];
    if (remote->control_path[0]) return snprintf(socket, F_PATH, "ControlPath=%s", remote->control_path) >= F_PATH ? -1 : 0;
    if (f_path(dir, sizeof(dir), f_home, "fleet/sockets") || f_mkdirs(dir)) return -1;
    return snprintf(socket, F_PATH, "ControlPath=%s/%%C", dir) >= F_PATH ? -1 : 0;
}
static void ssh_options(const struct f_remote *remote, char **argv, size_t *count) {
    const struct { const char *flag; const char *value; } options[] = {
        {"-E", remote->ssh_log}, {"-F", remote->ssh_config}, {"-l", remote->principal}
    };
    size_t i;
    for (i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        if (!options[i].value[0]) continue;
        argv[(*count)++] = (char *)options[i].flag; argv[(*count)++] = (char *)options[i].value;
    }
}
int f_ssh(const struct f_remote *remote, const char *command, const char *input,
          size_t size, unsigned seconds, bool tty, struct f_capture *cap) {
    char timeout[64], socket[F_PATH]; char *argv[48]; size_t n = 0;
    if (remote->require_existing_master && (!remote->control_path[0] || !remote->peer_fingerprint[0])) return -1;
    snprintf(timeout, sizeof(timeout), "ConnectTimeout=%u", seconds);
    argv[n++] = "ssh"; argv[n++] = tty ? "-t" : "-T"; argv[n++] = "-vv";
    ssh_options(remote, argv, &n);
    argv[n++] = "-o"; argv[n++] = "BatchMode=yes";
    argv[n++] = "-o"; argv[n++] = "StrictHostKeyChecking=yes";
    argv[n++] = "-o"; argv[n++] = timeout;
    if (remote->multiplex) {
        if (ssh_control(remote, socket)) return -1;
        argv[n++] = "-o"; argv[n++] = remote->require_existing_master ? "ControlMaster=no" : "ControlMaster=auto";
        argv[n++] = "-o"; argv[n++] = "ControlPersist=60";
        argv[n++] = "-o"; argv[n++] = socket;
        if (remote->require_existing_master) { argv[n++] = "-o"; argv[n++] = "ProxyCommand=false"; }
    }
    argv[n++] = (char *)remote->target; argv[n++] = (char *)command; argv[n] = NULL;
    if (tty) { execvp("ssh", argv); return -1; }
    return f_run(argv, input, size, seconds, cap);
}
char *f_peer_fingerprint(struct f_remote *remote, unsigned seconds) {
    char directory[] = "/tmp/hydra-enroll-ssh.XXXXXX"; json_object *request, *response; char *peer = NULL;
    if (remote->multiplex) {
        if (!mkdtemp(directory)) return NULL;
        if (f_path(remote->control_path, sizeof(remote->control_path), directory, "master") ||
            f_path(remote->ssh_log, sizeof(remote->ssh_log), directory, "peer.log")) { rmdir(directory); return NULL; }
    }
    request = json_object_new_object(); json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL));
    f_string_add(request, "action", "handshake"); response = f_request(remote, request, seconds);
    if (json_object_get_boolean(f_field(response, "ok")) && f_string(f_field(response, "data"), "peer_fingerprint")) {
        peer = strdup(f_string(f_field(response, "data"), "peer_fingerprint"));
        if (peer && !f_copy(remote->peer_fingerprint, sizeof(remote->peer_fingerprint), peer)) remote->require_existing_master = remote->multiplex;
    }
    json_object_put(response); json_object_put(request); return peer;
}
void f_peer_close(struct f_remote *remote) {
    struct f_capture cap = {0}; char directory[F_PATH], *slash;
    if (!remote->control_path[0]) return;
    char *argv[] = {"ssh", "-F", remote->ssh_config[0] ? remote->ssh_config : "/dev/null", "-S", remote->control_path, "-O", "exit", remote->target, NULL};
    (void)f_run(argv, NULL, 0, 3, &cap); f_capture_free(&cap);
    if (!f_copy(directory, sizeof(directory), remote->control_path) && (slash = strrchr(directory, '/'))) {
        *slash = '\0'; unlink(remote->control_path); unlink(remote->ssh_log); rmdir(directory);
    }
    remote->control_path[0] = '\0'; remote->ssh_log[0] = '\0'; remote->peer_fingerprint[0] = '\0'; remote->require_existing_master = false;
}
