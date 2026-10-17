#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Inventory scan of the operator's own OpenSSH configuration: literal Host
 * names become candidates that the normal `ssh -G` resolution then explains.
 * Patterns, Match blocks and glob Includes are reported as skipped, never
 * expanded. Nothing here reads keys or connects. */
static bool keyword_is(const char *word, size_t length, const char *keyword) {
    size_t i;
    if (length != strlen(keyword)) return false;
    for (i = 0; i < length; i++) if (tolower((unsigned char)word[i]) != keyword[i]) return false;
    return true;
}
static int include_path(const char *word, char out[F_PATH]) {
    const char *home = getenv("HOME"); char unquoted[F_PATH]; size_t n = strlen(word);
    if (n >= 2 && word[0] == '"' && word[n - 1] == '"') {
        if (n - 2 >= sizeof(unquoted)) return -1;
        memcpy(unquoted, word + 1, n - 2); unquoted[n - 2] = '\0'; word = unquoted;
    }
    if (strpbrk(word, "*?[\"\\%$") || !hd_text(word, F_PATH)) return -1;
    if (word[0] == '/') return f_copy(out, F_PATH, word);
    if (!home || home[0] != '/') return -1;
    if (!strncmp(word, "~/", 2)) return f_path(out, F_PATH, home, word + 2);
    if (snprintf(out, F_PATH, "%s/.ssh/%s", home, word) >= F_PATH) return -1;
    return 0;
}
static void include_file(json_object *report, const char *word, unsigned depth) {
    char path[F_PATH]; char *text; struct stat st; int64_t count = json_object_get_int64(f_field(report, "files"));
    if (include_path(word, path)) { hd_skip(report, NULL, "include_pattern"); return; }
    if (depth >= 2 || count >= HD_SOURCE_INCLUDES) { hd_skip(report, NULL, "include_limit"); return; }
    json_object_object_add(report, "files", json_object_new_int64(count + 1));
    if (stat(path, &st) || !S_ISREG(st.st_mode)) { hd_skip(report, NULL, "include_missing"); return; }
    text = f_read(path, HD_SOURCE_BYTES);
    if (!text) { hd_skip(report, NULL, "include_unreadable"); return; }
    hd_parse_ssh_hosts(text, path, report, depth + 1);
    free(text);
}
static void host_names(json_object *report, char *args) {
    char *save = NULL, *word;
    for (word = strtok_r(args, " \t", &save); word; word = strtok_r(NULL, " \t", &save)) {
        if (strpbrk(word, "*?!")) { hd_skip(report, word, "pattern"); continue; }
        if (word[0] == '"' || !f_target(word) || !hd_text(word, 256)) { hd_skip(report, NULL, "invalid_name"); continue; }
        hd_record_add(report, word, word, NULL);
    }
}
json_object *hd_parse_ssh_hosts(const char *text, const char *path, json_object *report, unsigned depth) {
    char *copy = strdup(text ? text : ""), *save = NULL, *line;
    if (!copy) return report;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *word, *args; size_t length;
        line[strcspn(line, "\r")] = '\0';
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '#') continue;
        word = line; length = strcspn(line, " \t=");
        args = line + length;
        while (*args == ' ' || *args == '\t' || *args == '=') args++;
        if (keyword_is(word, length, "host")) host_names(report, args);
        else if (keyword_is(word, length, "match")) hd_skip(report, NULL, "match_block");
        else if (keyword_is(word, length, "include")) {
            char *include_save = NULL, *item;
            for (item = strtok_r(args, " \t", &include_save); item; item = strtok_r(NULL, " \t", &include_save)) include_file(report, item, depth);
        }
    }
    free(copy);
    (void)path;
    return report;
}
json_object *hd_scan_ssh_config(const char *path) {
    struct stat st; char *text; json_object *report;
    if (stat(path, &st)) return hd_report("unavailable", "file_missing");
    if (!S_ISREG(st.st_mode)) return hd_report("unavailable", "not_a_file");
    if ((size_t)st.st_size > HD_SOURCE_BYTES) return hd_report("unavailable", "size_limit");
    text = f_read(path, HD_SOURCE_BYTES);
    if (!text) return hd_report("unavailable", "unreadable");
    report = hd_report("acquired", NULL);
    json_object_object_add(report, "files", json_object_new_int64(1));
    json_object_object_add(report, "bytes", json_object_new_int64((int64_t)strlen(text)));
    hd_parse_ssh_hosts(text, path, report, 0);
    free(text);
    if (json_object_get_boolean(f_field(report, "truncated"))) { f_string_add(report, "status", "partial"); f_string_add(report, "reason", "record_limit"); }
    return report;
}
