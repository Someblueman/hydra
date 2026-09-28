/* Host key trust (design §3). Hydra never accepts a changed key and never
 * edits or removes known_hosts lines: after explicit approval it appends
 * exactly the line OpenSSH itself captured, then proves it with a strict
 * probe. Every refusal leaves every known_hosts file byte-identical. */
#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HOSTKEY_FILES 8
#define HOSTKEY_LINE 8192

struct hostkey {
    char lookup[600];                           /* [host]:port or HostKeyAlias */
    char files[HOSTKEY_FILES][F_PATH];          /* user files first */
    size_t count;
    bool hashed;
    char line[HOSTKEY_LINE];                    /* captured known_hosts line */
    char fingerprint[256], key_type[64];
};
enum probe { PROBE_KNOWN, PROBE_UNKNOWN, PROBE_CHANGED, PROBE_FAILED };

/* ---- Effective OpenSSH configuration ---- */
static void add_file(struct hostkey *hk, const char *word) {
    const char *home = getenv("HOME");
    char *target;
    if (!word[0] || !strcmp(word, "none") || hk->count >= HOSTKEY_FILES) return;
    target = hk->files[hk->count];
    if (!strncmp(word, "~/", 2) && home) {
        if (snprintf(target, F_PATH, "%s/%s", home, word + 2) >= F_PATH) return;
    } else if (word[0] != '/' || f_copy(target, F_PATH, word)) return;
    hk->count++;
}
static void add_files(struct hostkey *hk, const char *list) {
    char copy[F_PATH * 2], *word, *save = NULL;
    if (!list || f_copy(copy, sizeof(copy), list)) return;
    for (word = strtok_r(copy, " ", &save); word; word = strtok_r(NULL, " ", &save)) add_file(hk, word);
}
/* known_hosts lookup name: HostKeyAlias, host, or [host]:port. */
static int lookup_name(json_object *config, char lookup[600]) {
    const char *alias = f_string(config, "hostkeyalias"), *host = f_string(config, "hostname"), *port = f_string(config, "port");
    int n;
    if (!host) return -1;
    if (alias && alias[0] && strcmp(alias, "none")) n = snprintf(lookup, 600, "%s", alias);
    else if (!port || !strcmp(port, "22")) n = snprintf(lookup, 600, "%s", host);
    else n = snprintf(lookup, 600, "[%s]:%s", host, port);
    return n > 0 && n < 600 ? 0 : -1;
}
static int hostkey_config(const struct setup_ctx *ctx, struct hostkey *hk) {
    json_object *config = f_ssh_query_config(&ctx->remote, ctx->seconds);
    const char *hash = f_string(config, "hashknownhosts");
    int status;
    memset(hk, 0, sizeof(*hk));
    status = config ? lookup_name(config, hk->lookup) : -1;
    hk->hashed = hash && !strcmp(hash, "yes");
    add_files(hk, f_string(config, "userknownhostsfile"));
    add_files(hk, f_string(config, "globalknownhostsfile"));
    json_object_put(config);
    return !status && hk->count ? 0 : -1;
}

/* ---- Strict probe with the user's own SSH policy ---- */
/* Key type from the "Server host key: TYPE SHA256:..." line of an ssh -v log. */
static void presented_type(const char *log, char type[64]) {
    const char *start = log ? strstr(log, "Server host key: ") : NULL;
    size_t length;
    type[0] = '\0';
    if (!start) return;
    start += strlen("Server host key: ");
    length = strcspn(start, " \r\n");
    if (length && length < 64) { memcpy(type, start, length); type[length] = '\0'; }
}
static enum probe strict_probe(const struct setup_ctx *ctx, char peer[256], char type[64], const char **code) {
    struct f_remote remote = ctx->remote; struct f_capture cap = {0}; char *found; enum probe result;
    remote.multiplex = false; /* never trust an existing master for identity */
    peer[0] = '\0'; type[0] = '\0'; *code = "transport_failed";
    if (f_ssh(&remote, "exit 0", NULL, 0, ctx->seconds, false, &cap)) { f_capture_free(&cap); return PROBE_FAILED; }
    if ((found = f_peer_from_log(cap.err))) { f_copy(peer, 256, found); free(found); }
    presented_type(cap.err, type);
    *code = f_transport_code(&cap);
    if (strstr(cap.err, "REMOTE HOST IDENTIFICATION HAS CHANGED")) result = PROBE_CHANGED;
    else if (strstr(cap.err, "Host key verification failed")) result = PROBE_UNKNOWN;
    else if (!cap.status || !strcmp(*code, "authentication_failed") || !strcmp(*code, "remote_failed")) result = PROBE_KNOWN;
    else result = PROBE_FAILED;
    f_capture_free(&cap);
    return result;
}

/* ---- Existing entries: any line for this host means refusal ---- */
/* True when ssh-keygen -F output holds an entry whose key type is type. */
static bool output_has_type(char *out, const char *type) {
    char *line, *save = NULL;
    for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *field_save = NULL, *key;
        if (line[0] == '#' || !strtok_r(line, " \t", &field_save)) continue;
        key = strtok_r(NULL, " \t", &field_save);
        if (key && type && !strcmp(key, type)) return true;
    }
    return false;
}
/* 1 when path holds an entry for lookup (unreadable files count as entries);
 * *same_type is set when an entry has key type type. */
static int file_entry(const char *lookup, const char *path, const char *type, bool *same_type) {
    struct stat st; struct f_capture cap = {0}; int found;
    char *argv[] = {"ssh-keygen", "-F", (char *)lookup, "-f", (char *)path, NULL};
    if (stat(path, &st)) return errno == ENOENT ? 0 : 1;
    if (!S_ISREG(st.st_mode)) return 0;
    if (access(path, R_OK)) return 1;
    if (f_run(argv, NULL, 0, 10, &cap)) { f_capture_free(&cap); return 1; }
    found = !cap.status && cap.out && cap.out[0];
    if (found && output_has_type(cap.out, type)) *same_type = true;
    f_capture_free(&cap);
    return found;
}
/* Index of the first file holding an entry, or -1. */
static int existing_entry(const struct hostkey *hk, const char *type, bool *same_type) {
    size_t i; int first = -1;
    *same_type = false;
    for (i = 0; i < hk->count; i++)
        if (file_entry(hk->lookup, hk->files[i], type, same_type) && first < 0) first = (int)i;
    return first;
}

/* ---- The file Hydra may append to ---- */
static bool private_directory(const char *path) {
    char dir[F_PATH], *slash; struct stat st;
    if (f_copy(dir, sizeof(dir), path) || !(slash = strrchr(dir, '/'))) return false;
    if (slash == dir) slash[1] = '\0'; else *slash = '\0';
    if (lstat(dir, &st)) return errno == ENOENT;
    return S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022);
}
static bool writable_file(const struct stat *st) {
    return S_ISREG(st->st_mode) && st->st_uid == geteuid() && !(st->st_mode & 0022) && (st->st_mode & S_IWUSR);
}
/* Checks without touching the file; absent is fine when its directory is private. */
static bool target_usable(const char *path) {
    struct stat st;
    if (!private_directory(path)) return false;
    if (lstat(path, &st)) return errno == ENOENT;
    return writable_file(&st) && !access(path, W_OK);
}
static int ensure_directory(const char *path) {
    char dir[F_PATH], *slash;
    if (f_copy(dir, sizeof(dir), path) || !(slash = strrchr(dir, '/')) || slash == dir) return 0;
    *slash = '\0';
    return f_mkdirs(dir);
}
/* Appends one line with a single write; a missing final newline is supplied. */
static int append_line(const char *path, const char *line) {
    char buffer[HOSTKEY_LINE + 2], last = '\n'; struct stat st; size_t n = 0, length = strlen(line); int fd, status = -1;
    if (ensure_directory(path) || !private_directory(path)) return -1;
    fd = open(path, O_RDWR | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !writable_file(&st) || (st.st_size > 0 && pread(fd, &last, 1, st.st_size - 1) != 1)) goto done;
    if (last != '\n') buffer[n++] = '\n';
    memcpy(buffer + n, line, length); n += length; buffer[n++] = '\n';
    if (write(fd, buffer, n) == (ssize_t)n && !fsync(fd)) status = 0;
done:
    close(fd);
    return status;
}

/* ---- Credential-free capture through the user's own SSH path ---- */
static int fingerprint_of(struct hostkey *hk, const char *scratch) {
    struct f_capture cap = {0}; char *argv[] = {"ssh-keygen", "-lf", (char *)scratch, NULL};
    char *save = NULL, *bits, *fp, *type = NULL; int status = -1;
    if (f_run(argv, NULL, 0, 10, &cap) || cap.status || !cap.out) goto done;
    bits = strtok_r(cap.out, " \n", &save); fp = strtok_r(NULL, " \n", &save);
    type = strrchr(save ? save : "", '(');
    if (!bits || !fp || strncmp(fp, "SHA256:", 7) || f_copy(hk->fingerprint, sizeof(hk->fingerprint), fp)) goto done;
    if (type) { type[strcspn(type, ")")] = '\0'; f_copy(hk->key_type, sizeof(hk->key_type), type + 1); }
    status = 0;
done:
    f_capture_free(&cap);
    return status;
}
static int captured_line(struct hostkey *hk, const char *scratch) {
    char *text = f_read(scratch, HOSTKEY_LINE), *newline;
    int status = -1;
    if (!text) return -1;
    newline = strchr(text, '\n');
    if (newline && !newline[1]) *newline = '\0';
    if (text[0] && !strchr(text, '\n') && !strchr(text, '\r') && !f_copy(hk->line, sizeof(hk->line), text)) status = 0;
    free(text);
    return status;
}
static void capture_argv(const struct setup_ctx *ctx, char *argv[40], char option[F_PATH + 32], char timeout[48]) {
    static const char *const fixed[] = {
        "BatchMode=yes", "StrictHostKeyChecking=accept-new", "GlobalKnownHostsFile=/dev/null",
        "PreferredAuthentications=none", "PubkeyAuthentication=no", "PasswordAuthentication=no",
        "KbdInteractiveAuthentication=no", "ForwardAgent=no", "ClearAllForwardings=yes", "ControlPath=none",
        "ControlMaster=no", "CheckHostIP=no", "UpdateHostKeys=no", "LogLevel=ERROR"
    };
    size_t n = 0, i;
    argv[n++] = "ssh"; argv[n++] = "-T";
    if (ctx->remote.ssh_config[0]) { argv[n++] = "-F"; argv[n++] = (char *)ctx->remote.ssh_config; }
    if (ctx->remote.principal[0]) { argv[n++] = "-l"; argv[n++] = (char *)ctx->remote.principal; }
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) { argv[n++] = "-o"; argv[n++] = (char *)fixed[i]; }
    argv[n++] = "-o"; argv[n++] = option; argv[n++] = "-o"; argv[n++] = timeout;
    argv[n++] = (char *)ctx->remote.target; argv[n++] = "exit"; argv[n] = NULL;
}
static json_object *capture_key(const struct setup_ctx *ctx, struct hostkey *hk) {
    char dir[] = "/tmp/hydra-hostkey.XXXXXX", scratch[F_PATH], option[F_PATH + 32], timeout[48], *argv[40];
    struct f_capture cap = {0}; json_object *error = NULL;
    if (!mkdtemp(dir) || f_path(scratch, sizeof(scratch), dir, "known_hosts"))
        return setup_error(ctx, "io_failed", "cannot create a private scratch directory", NULL, NULL);
    snprintf(option, sizeof(option), "UserKnownHostsFile=%s", scratch);
    snprintf(timeout, sizeof(timeout), "ConnectTimeout=%u", ctx->seconds);
    capture_argv(ctx, argv, option, timeout);
    (void)f_run(argv, NULL, 0, ctx->seconds + 5, &cap);
    if (access(scratch, F_OK))
        error = setup_error(ctx, cap.timeout ? "timeout" : "offline", "the host did not present a host key",
                            "check the destination, network and SSH configuration, then rerun", NULL);
    else if (captured_line(hk, scratch) || fingerprint_of(hk, scratch))
        error = setup_error(ctx, "host_key_ambiguous", "the host presented an unexpected set of host keys",
                            "add the host key to known_hosts yourself after verifying it out of band", NULL);
    f_capture_free(&cap);
    unlink(scratch); rmdir(dir);
    return error;
}

/* ---- Results ---- */
static json_object *trusted(struct setup_ctx *ctx, const struct hostkey *hk, const char *peer, const char *how) {
    json_object *detail = json_object_new_object(), *copy = NULL; char summary[F_PATH + 400];
    const char *fp = peer[0] ? peer : hk->fingerprint;
    f_copy(ctx->remote.accepted_host_key, sizeof(ctx->remote.accepted_host_key), fp);
    snprintf(summary, sizeof(summary), "%s%s%s (%s)", hk->key_type, hk->key_type[0] ? " " : "", fp[0] ? fp : "verified by ssh", hk->files[0]);
    f_string_add(detail, "fingerprint", fp); f_string_add(detail, "known_hosts", hk->files[0]);
    f_string_add(detail, "host", hk->lookup); f_string_add(detail, "result", how);
    f_string_add(detail, "summary", summary);
    if (json_object_deep_copy(detail, &copy, NULL) || setup_state_step(ctx, "host_key", "done", detail)) {
        json_object_put(copy);
        return setup_error(ctx, "state_unavailable", "cannot record the trusted host key", NULL, NULL);
    }
    return f_success(ctx->command, copy);
}
static json_object *changed(struct setup_ctx *ctx, const struct hostkey *hk, const char *peer) {
    json_object *data = json_object_new_object(); char recovery[F_PATH * 2 + 800]; char *host = f_quote(hk->lookup), *file = f_quote(hk->files[0]);
    snprintf(recovery, sizeof(recovery),
             "Hydra never accepts a changed key. Verify %s out of band; only if the change is expected run: ssh-keygen -R %s -f %s, then rerun",
             peer[0] ? peer : "the new key", host ? host : "HOST", file ? file : "FILE");
    free(host); free(file);
    f_string_add(data, "presented_fingerprint", peer); f_string_add(data, "host", hk->lookup);
    f_string_add(data, "known_hosts", hk->files[0]);
    (void)setup_state_step(ctx, "host_key", "blocked", NULL);
    return setup_error(ctx, "host_key_changed", "the host presented a different key than known_hosts or the alias records", recovery, data);
}
static json_object *ambiguous(struct setup_ctx *ctx, const struct hostkey *hk, int index) {
    json_object *data = json_object_new_object();
    f_string_add(data, "host", hk->lookup); f_string_add(data, "known_hosts", hk->files[index]);
    (void)setup_state_step(ctx, "host_key", "blocked", NULL);
    return setup_error(ctx, "host_key_ambiguous", "known_hosts already has a different entry for this host",
                       "inspect the existing entry (ssh-keygen -F HOST -f FILE); Hydra will not add a second key", data);
}
static json_object *unwritable(struct setup_ctx *ctx, const char *path) {
    json_object *data = json_object_new_object();
    f_string_add(data, "known_hosts", path);
    return setup_error(ctx, "known_hosts_unwritable", "known_hosts is not a private writable file owned by you",
                       "fix its owner and mode (0600, not group or world writable, not a symlink) or add the key yourself", data);
}
static json_object *probe_failed(const struct setup_ctx *ctx, const char *code) {
    return setup_error(ctx, code, "cannot reach the host with strict SSH", "check the destination, network and SSH configuration", NULL);
}
static json_object *approval(struct setup_ctx *ctx, const struct hostkey *hk, const char *fingerprint) {
    const char *const argv[] = {"trust-key", ctx->name, NULL};
    json_object *plan = json_object_new_object(), *result;
    f_string_add(plan, "fingerprint", hk->fingerprint); f_string_add(plan, "key_type", hk->key_type);
    f_string_add(plan, "host", hk->lookup); f_string_add(plan, "known_hosts", hk->files[0]);
    json_object_object_add(plan, "hashed", json_object_new_boolean(hk->line[0] == '|'));
    f_string_add(plan, "change", "append one line; existing lines are never edited or removed");
    result = setup_plan_gate(ctx, "host_key", plan, fingerprint, argv);
    json_object_put(plan);
    return result;
}
/* After approval: re-check, persist intent, append, then prove it. */
static json_object *write_and_verify(struct setup_ctx *ctx, const struct hostkey *hk) {
    char peer[256], type[64]; const char *code; bool same; int index = existing_entry(hk, NULL, &same);
    if (index >= 0) return ambiguous(ctx, hk, index);
    if (setup_state_step(ctx, "host_key", "in_progress", NULL))
        return setup_error(ctx, "state_unavailable", "cannot record setup progress", NULL, NULL);
    if (append_line(hk->files[0], hk->line)) {
        (void)setup_state_step(ctx, "host_key", "failed", NULL);
        return unwritable(ctx, hk->files[0]);
    }
    if (strict_probe(ctx, peer, type, &code) != PROBE_KNOWN || (peer[0] && strcmp(peer, hk->fingerprint))) {
        (void)setup_state_step(ctx, "host_key", "failed", NULL);
        return setup_error(ctx, "host_key_unknown", "the approved key was appended but strict SSH still rejects the host",
                           "inspect known_hosts and the SSH configuration (UserKnownHostsFile, HostKeyAlias)", NULL);
    }
    return trusted(ctx, hk, peer, "trusted");
}
/* Strict SSH already trusts the host. A key recorded earlier (by this setup or
 * by the alias being upgraded) must still match: a changed key is refused. */
static json_object *known(struct setup_ctx *ctx, const struct hostkey *hk, const char *peer, const char *fingerprint) {
    const char *recorded = ctx->remote.accepted_host_key;
    if (fingerprint && peer[0] && strcmp(fingerprint, peer))
        return setup_error(ctx, "approval_mismatch", "known_hosts already trusts a different fingerprint for this host", NULL, NULL);
    if (recorded[0] && peer[0] && strcmp(recorded, peer)) return changed(ctx, hk, peer);
    return trusted(ctx, hk, peer, "already trusted");
}
static json_object *trust(struct setup_ctx *ctx, struct hostkey *hk, const char *fingerprint) {
    char peer[256], type[64]; const char *code; json_object *error; bool same; int index;
    switch (strict_probe(ctx, peer, type, &code)) {
    case PROBE_KNOWN: return known(ctx, hk, peer, fingerprint);
    case PROBE_CHANGED:
        /* Only a recorded key of the presented type is a changed key; other
         * types known for this host are an ambiguity Hydra will not resolve. */
        index = existing_entry(hk, type, &same);
        return same || index < 0 ? changed(ctx, hk, peer) : ambiguous(ctx, hk, index);
    case PROBE_FAILED: return probe_failed(ctx, code);
    case PROBE_UNKNOWN: break;
    }
    if ((index = existing_entry(hk, NULL, &same)) >= 0) return ambiguous(ctx, hk, index);
    if (!target_usable(hk->files[0])) return unwritable(ctx, hk->files[0]);
    if ((error = capture_key(ctx, hk)) || (error = approval(ctx, hk, fingerprint))) return error;
    return write_and_verify(ctx, hk);
}
json_object *setup_step_trust_key(struct setup_ctx *ctx, const char *fingerprint) {
    struct hostkey hk;
    if (hostkey_config(ctx, &hk))
        return setup_error(ctx, "ssh_config_invalid", "ssh -G cannot evaluate the destination and known_hosts files",
                           "check the destination and --ssh-config file", NULL);
    return trust(ctx, &hk, fingerprint);
}
