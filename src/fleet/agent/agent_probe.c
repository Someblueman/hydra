#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/agent/agent.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

bool agent_location_name(const char *executable) {
    size_t length = executable ? strlen(executable) : 0;
    return length && length <= 64 && executable[0] != '.' && executable[0] != '-' &&
        strspn(executable, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == length;
}
static bool location_owner(const struct stat *st) { return st->st_uid == geteuid() || st->st_uid == 0; }
bool agent_location_valid(const char *executable, const char *path) {
    struct stat entry, target; const char *base;
    if (!agent_location_name(executable) || !path || path[0] != '/' || strlen(path) >= F_PATH || strchr(path, '\n')) return false;
    base = strrchr(path, '/') + 1;
    if (strcmp(base, executable) || lstat(path, &entry) || !location_owner(&entry)) return false;
    if (stat(path, &target) || !S_ISREG(target.st_mode) || !location_owner(&target) || (target.st_mode & 0022)) return false;
    return access(path, X_OK) == 0;
}
char *agent_location_recorded(const char *executable) {
    char path[F_PATH], text[F_PATH + 1]; struct stat st; ssize_t n; int fd;
    if (!agent_location_name(executable) || snprintf(path, sizeof(path), "%s/agents/locations/%s", f_home, executable) >= (int)sizeof(path)) return NULL;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) { close(fd); return NULL; }
    n = read(fd, text, F_PATH); close(fd);
    if (n <= 0) return NULL;
    text[n] = '\0'; text[strcspn(text, "\n")] = '\0';
    return agent_location_valid(executable, text) ? strdup(text) : NULL;
}
static bool executable_file(const char *path) {
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode) && !access(path, X_OK);
}
/* PATH first (executable regular files only), then a valid recorded location
 * (lib/profiles.sh agent_location_resolve parity). */
static char *resolved(const char *executable) {
    char candidate[F_PATH], canonical[F_PATH], *paths, *cursor, *directory;
    /* Preserve the invoked basename: symlink dispatchers such as mise use it. */
    if (executable[0] == '/') return !access(executable, X_OK) ? strdup(executable) : NULL;
    const char *path_env = getenv("PATH");
    paths = strdup(path_env ? path_env : ""); if (!paths) return NULL;
    cursor = paths;
    while (cursor) {
        directory = cursor; cursor = strchr(cursor, ':'); if (cursor) *cursor++ = '\0';
        if (!realpath(*directory ? directory : ".", canonical) || f_path(candidate, sizeof(candidate), canonical, executable) || !executable_file(candidate)) continue;
        free(paths); return strdup(candidate);
    }
    free(paths); return agent_location_recorded(executable);
}
json_object *agent_probe(json_object *profile) {
    json_object *evidence = json_object_new_object(), *probed = json_object_new_object(), *args = NULL;
    struct f_capture cap = {0}; char *path = resolved(f_string(profile, "executable")), *argv[AGENT_ARGS + 2], date[32]; size_t i;
    time_t now = time(NULL); struct tm utc; bool supported = false;
    gmtime_r(&now, &utc); strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
    f_string_add(evidence, "probed_at", date);
    json_object_object_add(evidence, "declared", agent_capabilities(profile));
    json_object_object_add(evidence, "probed", probed);
    json_object_object_add(evidence, "observed", NULL);
    json_object_object_add(evidence, "executable_path", path ? json_object_new_string(path) : NULL);
    json_object_object_add(evidence, "executable_version", NULL);
    if (path) {
        char *version[] = {path, "--version", NULL};
        if (!f_run(version, NULL, 0, 5, &cap) && !cap.status && cap.out && *cap.out) {
            size_t length = strcspn(cap.out, "\r\n");
            if (length <= 256) {
                bool printable = true;
                for (i = 0; i < length; i++) if ((unsigned char)cap.out[i] < 32 || (unsigned char)cap.out[i] >= 127) printable = false;
                if (printable) json_object_object_add(evidence, "executable_version", json_object_new_string_len(cap.out, (int)length));
            }
        }
        f_capture_free(&cap);
        args = agent_arguments(profile, "probe_argv", NULL);
        if (args) {
            for (i = 0; i < json_object_array_length(args); i++) argv[i] = (char *)f_text(json_object_array_get_idx(args, i));
            argv[i] = NULL; argv[0] = path;
            supported = !f_run(argv, NULL, 0, 5, &cap) && !cap.status;
            for (i = 0; supported && i < json_object_array_length(f_field(profile, "probe_tokens")); i++) {
                const char *token = f_text(json_object_array_get_idx(f_field(profile, "probe_tokens"), i));
                supported = strstr(cap.out, token) || strstr(cap.err, token);
            }
        }
    }
    json_object_object_add(probed, "executable_available", json_object_new_boolean(path != NULL));
    json_object_object_add(probed, "invocation_help", json_object_new_boolean(supported));
    f_string_add(evidence, "scope", "Executable version and help only; authentication, prompt, resume and hooks require run observations.");
    free(path); json_object_put(args); f_capture_free(&cap); return evidence;
}
