#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/setup/setup.h"
#include "fleet/revocation/revocation.h"
#include "fleet/fleet.h"
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <termios.h>

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
    /* A revoked transport binding never loads: no operation, alias reuse,
     * endpoint change or source refresh can reach the host through it. */
    if (remote_path(path, name) || revocation_blocked("transport", name) || !(text = f_read(path, 16384))) return -1;
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
 * another actor after preflight. Identical duplicate publication is harmless.
 * f_write syncs the directory for a record it publishes; a record that was
 * already there is synced here before it is reported as published. */
static int sync_alias_directory(void) {
    char directory[F_PATH]; int fd, status;
    if (f_path(directory, sizeof(directory), f_home, "fleet/remotes")) return -1;
    fd = open(directory, O_RDONLY); if (fd < 0) return -1;
    status = fsync(fd); close(fd); return status;
}
int f_remote_enrolled(const struct f_remote *remote, bool publish) {
    char path[F_PATH], dir[F_PATH]; json_object *expected = remote_record(remote), *existing = NULL; int status = -1;
    bool written = false;
    if (remote_path(path, remote->name) || revocation_blocked("transport", remote->name)) goto done;
    if (!access(path, F_OK)) {
        existing = f_read_json(path, 16384); status = json_object_equal(existing, expected) ? 0 : -1; goto done;
    }
    if (!publish) { status = 0; goto done; }
    if (f_path(dir, sizeof(dir), f_home, "fleet/remotes") || f_mkdirs(dir)) goto done;
    const char *text = json_object_to_json_string_ext(expected, JSON_C_TO_STRING_PLAIN);
    if (!f_write(path, text, strlen(text), false)) { status = 0; written = true; }
    else { existing = f_read_json(path, 16384); status = json_object_equal(existing, expected) ? 0 : -1; }
done:
    if (!status && publish && !written) status = sync_alias_directory();
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
/* Parses `remote add` options after NAME and DEST; NULL on success. */
static json_object *alias_options(struct f_remote *remote, int argc, char **argv) {
    int i;
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--multiplex")) remote->multiplex = true;
        else if ((!strcmp(argv[i], "--hydra") || !strcmp(argv[i], "--home") || !strcmp(argv[i], "--ssh-config")) && i + 1 < argc && argv[i+1][0] == '/') {
            char *dst = !strcmp(argv[i], "--home") ? remote->home : !strcmp(argv[i], "--ssh-config") ? remote->ssh_config : remote->hydra;
            if (f_copy(dst, F_PATH, argv[++i])) return f_error("remote", "invalid_input", "path is too long");
        } else return f_error("remote", "invalid_input", "unknown or missing alias option");
    }
    return NULL;
}
json_object *f_remote_cli(int argc, char **argv) {
    struct f_remote remote; char path[F_PATH]; json_object *error;
    memset(&remote, 0, sizeof(remote));
    /* A revoked alias name cannot be reused by add or by a new guided setup. */
    if (argc >= 2 && (!strcmp(argv[0], "add") || setup_command(argv[0])) && f_name(argv[1]) && revocation_blocked("transport", argv[1]))
        return revocation_error("remote", "transport", argv[1]);
    /* Guided setup and its step commands share one private state machine. */
    if (argc >= 1 && setup_command(argv[0])) return setup_cli(argc, argv);
    if (argc == 1 && !strcmp(argv[0], "list")) return f_success("remote-list", f_remotes());
    if (argc == 2 && !strcmp(argv[0], "remove") && !remote_path(path, argv[1])) {
        if (unlink(path)) return f_error("remote", "io_failed", "cannot remove remote alias");
        return f_success("remote-remove", json_object_new_object());
    }
    if (argc < 3 || strcmp(argv[0], "add") || !f_name(argv[1]) || !f_target(argv[2]) ||
        f_copy(remote.name, sizeof(remote.name), argv[1]) || f_copy(remote.target, sizeof(remote.target), argv[2]))
        return f_error("remote", "invalid_input", "remote add NAME [USER@]SSH_ALIAS [--hydra /path] [--home /path] [--ssh-config /path] [--multiplex]");
    f_copy(remote.hydra, sizeof(remote.hydra), "hydra");
    if ((error = alias_options(&remote, argc, argv))) return error;
    if (f_remote_save(&remote)) return f_error("remote", "io_failed", "cannot store alias");
    return f_success("remote-add", json_object_new_object());
}
const char *f_transport_code(const struct f_capture *cap) {
    if (f_stopped) return "cancelled";
    if (cap->timeout || strstr(cap->err, "Connection timed out") || strstr(cap->err, "Operation timed out")) return "timeout";
    if (strstr(cap->err, "Host key verification failed") || strstr(cap->err, "REMOTE HOST IDENTIFICATION HAS CHANGED")) return "host_key_failed";
    if (strstr(cap->err, "Permission denied") || strstr(cap->err, "Authentication failed")) return "authentication_failed";
    if (cap->status == 255) return "offline";
    if (cap->status == 125) return "output_limit";
    return "remote_failed";
}
/* ssh -v/-vv chatter and success banners that never explain a failure. */
static bool ssh_noise(const char *line, size_t length) {
    static const char *const prefixes[] = {
        "debug", "OpenSSH_", "Authenticated to ", "Transferred: ", "Bytes per second", "Warning: Permanently added",
        "Pseudo-terminal will not", "Shared connection to ", "Exit status "
    };
    size_t i;
    if (!length) return true;
    /* "Connection to HOST closed." ends a normal session; "... closed by
     * remote host." does not. */
    if (!strncmp(line, "Connection to ", 14) && length > 8 && !strncmp(line + length - 8, " closed.", 8)) return true;
    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
        if (length >= strlen(prefixes[i]) && !strncmp(line, prefixes[i], strlen(prefixes[i]))) return true;
    return false;
}
void f_ssh_reason(const char *err, char *out, size_t size) {
    const char *line = err, *best = NULL; size_t best_length = 0, i, n;
    if (!size) return;
    out[0] = '\0';
    while (line && *line) {
        size_t length = strcspn(line, "\r\n");
        if (!ssh_noise(line, length)) { best = line; best_length = length; }
        line += length;
        line += strspn(line, "\r\n");
    }
    if (!best) return;
    for (i = 0, n = 0; i < best_length && n + 1 < size; i++)
        out[n++] = (unsigned char)best[i] < 0x20 || best[i] == 0x7f ? '?' : best[i];
    out[n] = '\0';
}
char *f_peer_from_log(const char *text) {
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
        peer = f_peer_from_log(remote->ssh_log[0] ? log : stderr_text);
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
static int ssh_captured(const struct f_remote *remote, const char *command, const char *input, size_t size,
                        unsigned connect_seconds, unsigned seconds, struct f_capture *cap);
/* One answer of a completed session, from its text. status is the exit
 * status that belongs to this answer: a successful answer from a command
 * that did not exit 0 is not believed. */
static json_object *session_answer(const struct f_remote *remote, const struct f_capture *cap, const char *text, int status) {
    json_object *result;
    result = f_parse(text);
    if (result && status && json_object_get_boolean(f_field(result, "ok"))) { json_object_put(result); result = NULL; }
    if (!response_valid(result)) {
        json_object_put(result);
        result = f_error("fleet", cap->status ? f_transport_code(cap) : "invalid_response", cap->err[0] ? cap->err : "missing or invalid fleet response");
    } else if (json_object_get_boolean(f_field(result, "ok"))) {
        response_peer(result, remote, cap->err);
    }
    return result;
}
/* The receiver command for one request on standard input. With a handshake
 * first, the same session answers a handshake request (given on the command
 * line, so standard input still carries only the caller's request) and then,
 * if that command exited 0, the request: two lines of output. A receiver
 * without Hydra fails at the first command exactly as it does alone. between
 * (NULL: none) is one more request answered after the handshake, also given
 * on the command line: three lines. */
#define SERVE_COMMAND (F_PATH * 8 + 128)
#define SESSION_COMMAND (SERVE_COMMAND * 3 + 1024)
static int serve_command(const struct f_remote *remote, bool handshake_first, const char *between, char *command, size_t size) {
    char *exe = f_quote(remote->hydra), *home = f_quote(remote->home), *serve = malloc(SERVE_COMMAND); int n = -1;
    char *middle = between ? f_quote(between) : NULL;
    if (exe && home && serve && (!between || middle)) {
        n = snprintf(serve, SERVE_COMMAND, "env LC_ALL=C %s%s %s fleet serve", remote->home[0] ? "HYDRA_HOME=" : "", remote->home[0] ? home : "", exe);
        if (n < 0 || n >= SERVE_COMMAND) n = -1;
        else if (!handshake_first) n = snprintf(command, size, "%s", serve);
        else if (!between) n = snprintf(command, size, "printf '%%s' '{\"protocol\":%d,\"action\":\"handshake\"}' | %s && %s", F_PROTOCOL, serve, serve);
        else n = snprintf(command, size, "printf '%%s' '{\"protocol\":%d,\"action\":\"handshake\"}' | %s && printf '%%s' %s | %s && %s",
                          F_PROTOCOL, serve, middle, serve, serve);
    }
    free(exe); free(home); free(serve); free(middle);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
json_object *f_request_measured(const struct f_remote *remote, json_object *request, unsigned seconds,
                                 size_t *request_bytes, size_t *response_bytes, bool *request_complete) {
    struct f_capture cap = {0}; json_object *result = NULL;
    char *command = malloc(SERVE_COMMAND);
    const char *input = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    if (request_bytes) *request_bytes = 0;
    if (response_bytes) *response_bytes = 0;
    if (request_complete) *request_complete = false;
    if (!command || serve_command(remote, false, NULL, command, SERVE_COMMAND) ||
        f_ssh(remote, command, input, strlen(input), seconds, false, &cap)) goto done;
    if (response_bytes) *response_bytes = cap.out_bytes;
    if (request_bytes) *request_bytes = cap.in_bytes;
    if (request_complete) *request_complete = cap.measurement_complete;
    result = session_answer(remote, &cap, cap.out, cap.status);
 done:
    free(command); f_capture_free(&cap);
    return result ? result : f_error("fleet", "transport_failed", "cannot start SSH transport");
}
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds) {
    return f_request_measured(remote, request, seconds, NULL, NULL, NULL);
}
/* The session of f_request_observed, with between (NULL: none) answered
 * after the handshake: *between_answer is then its answer, or NULL when the
 * session did not get that far. */
static json_object *observed_session(const struct f_remote *remote, json_object *between, json_object *request,
                                     unsigned connect_seconds, unsigned seconds, json_object **handshake, json_object **between_answer) {
    struct f_capture cap = {0}; json_object *result = NULL;
    char *command = malloc(SESSION_COMMAND), *line, *last;
    const char *input = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    *handshake = NULL;
    if (between_answer) *between_answer = NULL;
    if (!command || serve_command(remote, true, between ? json_object_to_json_string_ext(between, JSON_C_TO_STRING_PLAIN) : NULL, command, SESSION_COMMAND) ||
        ssh_captured(remote, command, input, strlen(input), connect_seconds, seconds, &cap)) {
        *handshake = f_error("fleet", "transport_failed", "cannot start SSH transport");
        goto done;
    }
    /* Each answer is one line: plain JSON holds no raw newline. The session's
     * exit status is that of its last command. A line that was ended is the
     * whole answer of a command the session got past, so it is judged on its
     * own; one that was not ended is judged with the status of the session,
     * as a session of its own would be. */
    line = strchr(cap.out, '\n');
    if (line) *line++ = '\0';
    *handshake = session_answer(remote, &cap, cap.out, line ? 0 : cap.status);
    if (!json_object_get_boolean(f_field(*handshake, "ok"))) goto done;
    if (between) {
        last = line ? strchr(line, '\n') : NULL;
        if (last) *last++ = '\0';
        *between_answer = session_answer(remote, &cap, line ? line : "", last ? 0 : cap.status);
        /* A command that failed ends the session: the request was not asked. */
        if (!json_object_get_boolean(f_field(*between_answer, "ok"))) goto done;
        line = last;
    }
    result = session_answer(remote, &cap, line ? line : "", cap.status);
 done:
    free(command); f_capture_free(&cap);
    return result;
}
json_object *f_request_observed(const struct f_remote *remote, json_object *request, unsigned connect_seconds, unsigned seconds,
                                json_object **handshake) {
    json_object *result = observed_session(remote, NULL, request, connect_seconds, seconds, handshake, NULL);
    /* Without a handshake there is no second answer: the request's answer
     * is the handshake's failure. */
    return result ? result : f_parse(json_object_to_json_string_ext(*handshake, JSON_C_TO_STRING_PLAIN));
}
/* Hydra 2.x, 3.x and 4.x all speak fleet protocol 1 over state 2; features
 * are negotiated by capability, not by version. */
bool f_handshake_compatible(json_object *data) {
    const char *version = f_string(data, "hydra_version");
    return f_number_is(data, "fleet_protocol", F_PROTOCOL) && f_number_is(data, "state_schema", 2) &&
        f_number_is(data, "event_schema", 1) && f_number_is(data, "json_schema", 1) &&
        version && (!strncmp(version, "2.", 2) || !strncmp(version, "3.", 2) || !strncmp(version, "4.", 2)) &&
        json_object_is_type(f_field(data, "capabilities"), json_type_array);
}
json_object *f_handshake_judged(const struct f_remote *remote, json_object *handshake) {
    if (json_object_get_boolean(f_field(handshake, "ok")) && !f_handshake_compatible(f_field(handshake, "data"))) {
        json_object_put(handshake); handshake = f_error("fleet", "version_mismatch", "remote Hydra or fleet protocol is incompatible");
    }
    f_string_add(handshake, "host", remote->name); return handshake;
}
static bool advertises(json_object *handshake, const char *capability) {
    json_object *caps = f_field(f_field(handshake, "data"), "capabilities"); size_t i;
    for (i = 0; json_object_is_type(caps, json_type_array) && i < json_object_array_length(caps); i++) {
        json_object *cap = json_object_array_get_idx(caps, i);
        if (json_object_is_type(cap, json_type_string) && !strcmp(json_object_get_string(cap), capability)) return true;
    }
    return false;
}
json_object *f_request_if_capable(const struct f_remote *remote, const char *capability, json_object *request,
                                  unsigned connect_seconds, unsigned seconds) {
    json_object *handshake = NULL, *answer = f_request_observed(remote, request, connect_seconds, seconds, &handshake);
    handshake = f_handshake_judged(remote, handshake);
    if (!json_object_get_boolean(f_field(handshake, "ok")) || !advertises(handshake, capability)) { json_object_put(answer); answer = NULL; }
    json_object_put(handshake); return answer;
}
/* The rest of f_observe's judgment, for a receiver whose handshake (borrowed)
 * was judged good: res, which this takes over, or capability_unavailable when
 * the handshake does not advertise the action, whatever the receiver
 * answered to it. */
static json_object *observation_judged(const struct f_remote *remote, const char *action, json_object *handshake, json_object *res) {
    if (!advertises(handshake, action)) { json_object_put(res); res = f_error("fleet", "capability_unavailable", "remote lacks required capability"); }
    if (!strcmp(action, "list") && json_object_get_boolean(f_field(res, "ok")) && !json_object_is_type(f_field(f_field(res, "data"), "heads"), json_type_array)) {
        json_object_put(res); res = f_error("fleet", "invalid_response", "snapshot heads must be an array");
    }
    f_string_add(res, "host", remote->name); return res;
}
static json_object *observation_request(const char *action) {
    json_object *req = json_object_new_object();
    json_object_object_add(req, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(req, "action", action);
    return req;
}
/* Every action observed here only reads on the receiver (handshake, list,
 * overview, doctor), so the handshake and the action share one SSH session
 * (f_request_observed). The answer is judged as before: a failed handshake
 * is returned as it is, an incompatible receiver is version_mismatch, and an
 * action the receiver does not advertise is capability_unavailable whatever
 * the receiver answered to it. */
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds) {
    json_object *req = observation_request(action), *res = NULL, *handshake = NULL;
    if (!strcmp(action, "handshake")) { res = f_handshake_judged(remote, f_request(remote, req, seconds)); json_object_put(req); return res; }
    res = f_request_observed(remote, req, seconds, seconds, &handshake); json_object_put(req);
    handshake = f_handshake_judged(remote, handshake);
    if (!json_object_get_boolean(f_field(handshake, "ok"))) { json_object_put(res); return handshake; }
    res = observation_judged(remote, action, handshake, res);
    json_object_put(handshake); return res;
}
json_object *f_observe_pair(const struct f_remote *remote, const char *first, const char *action, unsigned seconds, json_object **first_result) {
    json_object *between = observation_request(first), *req = observation_request(action), *handshake = NULL, *answer = NULL, *res;
    res = observed_session(remote, between, req, seconds, seconds, &handshake, &answer);
    json_object_put(between); json_object_put(req);
    handshake = f_handshake_judged(remote, handshake);
    if (!json_object_get_boolean(f_field(handshake, "ok"))) {
        json_object_put(res); json_object_put(answer);
        *first_result = f_parse(json_object_to_json_string_ext(handshake, JSON_C_TO_STRING_PLAIN));
        return handshake;
    }
    *first_result = observation_judged(remote, first, handshake, answer);
    /* A first action that failed ended the session before the second was
     * asked; it is asked in a session of its own, as it used to be. */
    res = res ? observation_judged(remote, action, handshake, res) : f_observe(remote, action, seconds);
    json_object_put(handshake); return res;
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
/* Builds the strict SSH argv shared by captured and terminal sessions. The
 * option buffers are caller-owned and must outlive argv. Hydra either runs
 * its own master (--multiplex) or none: a user ControlMaster/ControlPersist
 * from ~/.ssh/config is never started (its daemonized master would hold a
 * captured pipe) nor reused (a reused master skips host-key verification
 * and the peer fingerprint Hydra reads from the -vv log). */
static int ssh_argv(const struct f_remote *remote, const char *command, unsigned seconds, bool tty, bool verbose,
                    char *argv[48], char timeout[64], char socket[F_PATH]) {
    size_t n = 0;
    if (remote->require_existing_master && (!remote->control_path[0] || !remote->peer_fingerprint[0])) return -1;
    snprintf(timeout, 64, "ConnectTimeout=%u", seconds);
    argv[n++] = "ssh"; argv[n++] = tty ? "-t" : "-T";
    if (verbose) argv[n++] = "-vv";
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
    } else {
        argv[n++] = "-o"; argv[n++] = "ControlMaster=no";
        argv[n++] = "-o"; argv[n++] = "ControlPath=none";
    }
    argv[n++] = (char *)remote->target; argv[n++] = (char *)command; argv[n] = NULL;
    return 0;
}
/* A captured session whose connection and whole run have separate bounds. */
static int ssh_captured(const struct f_remote *remote, const char *command, const char *input, size_t size,
                        unsigned connect_seconds, unsigned seconds, struct f_capture *cap) {
    char timeout[64], socket[F_PATH]; char *argv[48];
    if (ssh_argv(remote, command, connect_seconds, false, true, argv, timeout, socket)) return -1;
    return f_run(argv, input, size, seconds, cap);
}
int f_ssh(const struct f_remote *remote, const char *command, const char *input,
          size_t size, unsigned seconds, bool tty, struct f_capture *cap) {
    char timeout[64], socket[F_PATH]; char *argv[48];
    if (ssh_argv(remote, command, seconds, tty, true, argv, timeout, socket)) return -1;
    if (tty) { execvp("ssh", argv); return -1; }
    return f_run(argv, input, size, seconds, cap);
}
static void interactive_child(char *const argv[]) {
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE};
    size_t i;
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) signal(signals[i], SIG_DFL);
    execvp("ssh", argv);
    _exit(127);
}
static int wait_child(pid_t pid, int *status) {
    for (;;) {
        if (waitpid(pid, status, 0) == pid) return 0;
        if (errno != EINTR) return -1;
    }
}
int f_ssh_interactive(const struct f_remote *remote, const char *command, unsigned seconds, int *exit_status) {
    char timeout[64], socket[F_PATH]; char *argv[48]; struct termios saved;
    bool terminal = isatty(STDIN_FILENO) && !tcgetattr(STDIN_FILENO, &saved);
    pid_t pid; int status = 0, waited;
    *exit_status = -1;
    if (ssh_argv(remote, command, seconds, true, remote->ssh_log[0] != '\0', argv, timeout, socket)) return -1;
    fflush(stdout); fflush(stderr);
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) interactive_child(argv);
    waited = wait_child(pid, &status);
    if (terminal) (void)tcsetattr(STDIN_FILENO, TCSADRAIN, &saved);
    if (waited) return -1;
    if (WIFSIGNALED(status)) *exit_status = 128 + WTERMSIG(status);
    else if (WIFEXITED(status)) *exit_status = WEXITSTATUS(status);
    if (f_stopped && *exit_status != 0) *exit_status = 128 + (int)f_stopped;
    return 0;
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
/* Adds one "key value" line of ssh -G output; the first occurrence wins. */
static void config_line(json_object *out, char *line) {
    char *space = strchr(line, ' ');
    if (!space || space == line) return;
    *space = '\0';
    if (!f_field(out, line)) f_string_add(out, line, space + 1);
}
json_object *f_ssh_query_config(const struct f_remote *remote, unsigned seconds) {
    struct f_capture cap = {0}; char *argv[12], *line, *save = NULL; size_t n = 0; json_object *out = NULL;
    argv[n++] = "ssh"; argv[n++] = "-G";
    if (remote->ssh_config[0]) { argv[n++] = "-F"; argv[n++] = (char *)remote->ssh_config; }
    if (remote->principal[0]) { argv[n++] = "-l"; argv[n++] = (char *)remote->principal; }
    argv[n++] = "--"; argv[n++] = (char *)remote->target; argv[n] = NULL;
    if (!f_run(argv, NULL, 0, seconds, &cap) && !cap.status && cap.out) {
        out = json_object_new_object();
        for (line = strtok_r(cap.out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) config_line(out, line);
    }
    f_capture_free(&cap);
    return out;
}
