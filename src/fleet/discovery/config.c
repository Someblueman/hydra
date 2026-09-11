#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* This file is passed with -F, which OpenSSH also passes to ProxyJump children.
 * First-value-wins policy precedes the operator's existing configuration. */
static const char policy[] =
    "Host *\n"
    " BatchMode yes\n StrictHostKeyChecking yes\n UpdateHostKeys no\n"
    " ControlMaster no\n ControlPath none\n ControlPersist no\n"
    " ForwardAgent no\n ForwardX11 no\n ClearAllForwardings yes\n"
    " PermitLocalCommand no\n RequestTTY no\n ConnectionAttempts 1\n"
    " CanonicalizeHostname no\n";

bool hd_text(const char *text, size_t limit) {
    const unsigned char *p;
    if (!text || !*text || strlen(text) >= limit) return false;
    for (p = (const unsigned char *)text; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}
bool hd_keys(json_object *object, const char *const *keys) {
    if (!json_object_is_type(object, json_type_object)) return false;
    json_object_object_foreach(object, key, value) {
        size_t i; (void)value;
        for (i = 0; keys[i] && strcmp(key, keys[i]); i++) { }
        if (!keys[i]) return false;
    }
    return true;
}
void hd_id(char id[520], const char *target) {
    static const char hex[] = "0123456789abcdef";
    size_t i, n = strlen(target);
    memcpy(id, "cand_", 5);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)target[i];
        id[5 + i * 2] = hex[c >> 4]; id[6 + i * 2] = hex[c & 15];
    }
    id[5 + n * 2] = '\0';
}
static int include_config(FILE *file, const char *source, bool required) {
    /* Include expands globs/tokens. Reject them rather than silently changing
     * an explicitly selected filename into a wider configuration source. */
    if (!hd_text(source, F_PATH) || source[0] != '/' || strpbrk(source, "\"\\*?[%$~")) return -1;
    if (access(source, R_OK)) return required ? -1 : 0;
    return fprintf(file, "Host *\nInclude \"%s\"\n", source) < 0 ? -1 : 0;
}
int hd_config(char path[F_PATH], const char *source) {
    char user[F_PATH]; int fd, status = -1; FILE *file;
    if (f_copy(path, F_PATH, "/tmp/hydra-discovery-ssh.XXXXXX")) return -1;
    fd = mkstemp(path); if (fd < 0) return -1;
    file = fdopen(fd, "w"); if (!file) { close(fd); unlink(path); return -1; }
    if (fputs(policy, file) == EOF) goto done;
    if (source) {
        if (include_config(file, source, true)) goto done;
    } else {
        if (!getenv("HOME") || f_path(user, sizeof(user), getenv("HOME"), ".ssh/config") ||
            include_config(file, user, false) || include_config(file, "/etc/ssh/ssh_config", false)) goto done;
    }
    status = 0;
done:
    if (fclose(file)) status = -1;
    if (status) unlink(path);
    return status;
}
static bool effective_key(const char *key) {
    static const char *const keys[] = {"hostname", "user", "port", "proxyjump", "hostkeyalias",
        "identityfile", "identitiesonly", "userknownhostsfile", "globalknownhostsfile", NULL};
    size_t i;
    for (i = 0; keys[i]; i++) if (!strcmp(keys[i], key)) return true;
    return false;
}
static json_object *effective_parse(char *text) {
    json_object *data = json_object_new_object(); char *save = NULL, *line;
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *space = strchr(line, ' '); json_object *values;
        if (!space) continue;
        *space++ = '\0';
        if (!strcmp(line, "proxycommand")) {
            json_object_object_add(data, "proxycommand_configured", json_object_new_boolean(strcmp(space, "none") != 0));
            continue;
        }
        if (!effective_key(line)) continue;
        if (!hd_text(space, F_PATH)) { json_object_put(data); return NULL; }
        values = f_field(data, line);
        if (!values) { values = json_object_new_array(); json_object_object_add(data, line, values); }
        json_object_array_add(values, json_object_new_string(space));
    }
    if (!f_field(data, "hostname") || !f_field(data, "user") || !f_field(data, "port")) {
        json_object_put(data); return NULL;
    }
    return data;
}
/* Bind the entire effective policy without disclosing ProxyCommand arguments.
 * The public projection remains intentionally limited to non-secret fields. */
static int policy_hash(const char *text, char hash[65]) {
    struct f_capture cap = {0}; int status = -1;
    char *argv[] = {"shasum", "-a", "256", NULL};
    if (f_run(argv, text, strlen(text), 5, &cap)) goto done;
    if (cap.status == 127) {
        char *fallback[] = {"sha256sum", NULL};
        f_capture_free(&cap);
        if (f_run(fallback, text, strlen(text), 5, &cap)) goto done;
    }
    if (!cap.status && strlen(cap.out) >= 64 && strspn(cap.out, "0123456789abcdef") >= 64) {
        memcpy(hash, cap.out, 64); hash[64] = '\0'; status = 0;
    }
done:
    f_capture_free(&cap); return status;
}
json_object *hd_resolve(const char *target, const char *config, unsigned seconds) {
    struct f_capture cap = {0}; json_object *data = NULL, *result; char hash[65];
    char *argv[] = {"ssh", "-G", "-F", (char *)config, (char *)target, NULL};
    if (!f_run(argv, NULL, 0, seconds, &cap) && !cap.status && cap.out_bytes < 65536 &&
        !memchr(cap.out, '\0', cap.out_bytes) && !policy_hash(cap.out, hash)) data = effective_parse(cap.out);
    if (data) f_string_add(data, "policy_sha256", hash);
    result = data ? f_success("host-resolve", data) :
        f_error("host-resolve", f_stopped ? "cancelled" : (cap.timeout ? "timeout" : "ssh_config_failed"),
                "effective OpenSSH configuration could not be resolved within limits");
    f_capture_free(&cap); return result;
}
