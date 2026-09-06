#define _XOPEN_SOURCE 700
#include "agent.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *resolved(const char *executable) {
    char candidate[F_PATH], canonical[F_PATH], *paths, *cursor, *directory;
    /* Preserve the invoked basename: symlink dispatchers such as mise use it. */
    if (executable[0] == '/') return !access(executable, X_OK) ? strdup(executable) : NULL;
    paths = strdup(getenv("PATH") ? getenv("PATH") : ""); if (!paths) return NULL;
    cursor = paths;
    while (cursor) {
        directory = cursor; cursor = strchr(cursor, ':'); if (cursor) *cursor++ = '\0';
        if (!realpath(*directory ? directory : ".", canonical) || f_path(candidate, sizeof(candidate), canonical, executable) || access(candidate, X_OK)) continue;
        free(paths); return strdup(candidate);
    }
    free(paths); return NULL;
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
            for (i = 0; i < json_object_array_length(args); i++) argv[i] = (char *)task_text(json_object_array_get_idx(args, i));
            argv[i] = NULL; argv[0] = path;
            supported = !f_run(argv, NULL, 0, 5, &cap) && !cap.status;
            for (i = 0; supported && i < json_object_array_length(f_field(profile, "probe_tokens")); i++) {
                const char *token = task_text(json_object_array_get_idx(f_field(profile, "probe_tokens"), i));
                supported = strstr(cap.out, token) || strstr(cap.err, token);
            }
        }
    }
    json_object_object_add(probed, "executable_available", json_object_new_boolean(path != NULL));
    json_object_object_add(probed, "invocation_help", json_object_new_boolean(supported));
    f_string_add(evidence, "scope", "Executable version and help only; authentication, prompt, resume and hooks require run observations.");
    free(path); json_object_put(args); f_capture_free(&cap); return evidence;
}
