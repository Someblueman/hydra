#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Ansible INI hosts files read through the operator's own files. Only host
 * lines in plain [group] sections are inventory; [group:vars] and
 * [group:children] sections are ignored because they may carry credentials.
 * Connection variables are an allowlist; anything that looks like a secret
 * is counted and dropped, never copied into a record. */
static bool secret_key(const char *key) {
    static const char *const marks[] = {"pass", "token", "secret", "private_key", "become", "vault", "credential", NULL};
    char lower[128]; size_t i;
    for (i = 0; key[i] && i + 1 < sizeof(lower); i++) lower[i] = (char)tolower((unsigned char)key[i]);
    lower[i] = '\0';
    for (i = 0; marks[i]; i++) if (strstr(lower, marks[i])) return true;
    return false;
}
struct host_line { const char *name, *host, *user, *port, *connection; bool secret; };
static void variable(struct host_line *line, char *token) {
    char *equals = strchr(token, '=');
    if (!equals) return;
    *equals++ = '\0';
    if (secret_key(token)) { line->secret = true; return; }
    if (!strcmp(token, "ansible_host") || !strcmp(token, "ansible_ssh_host")) line->host = equals;
    else if (!strcmp(token, "ansible_user") || !strcmp(token, "ansible_ssh_user")) line->user = equals;
    else if (!strcmp(token, "ansible_port") || !strcmp(token, "ansible_ssh_port")) line->port = equals;
    else if (!strcmp(token, "ansible_connection")) line->connection = equals;
}
static void host_record(json_object *report, char *text, const char *group) {
    struct host_line line = {0}; char *save = NULL, *token, target[512]; json_object *labels; const char *host;
    int64_t omitted;
    line.name = strtok_r(text, " \t", &save);
    if (!line.name) return;
    for (token = strtok_r(NULL, " \t", &save); token; token = strtok_r(NULL, " \t", &save)) variable(&line, token);
    if (line.secret) {
        omitted = json_object_get_int64(f_field(report, "credentials_omitted"));
        json_object_object_add(report, "credentials_omitted", json_object_new_int64(omitted + 1));
    }
    if (strpbrk(line.name, "[]:")) { hd_skip(report, line.name, "pattern"); return; }
    if (!hd_text(line.name, 128) || !f_target(line.name)) { hd_skip(report, NULL, "invalid_name"); return; }
    if (line.connection && strcmp(line.connection, "ssh") && strcmp(line.connection, "smart") && strcmp(line.connection, "paramiko")) {
        hd_skip(report, line.name, "connection_not_ssh"); return;
    }
    host = line.host ? line.host : line.name;
    if (snprintf(target, sizeof(target), "%s%s%s", line.user ? line.user : "", line.user ? "@" : "", host) >= (int)sizeof(target) ||
        !f_target(target) || !hd_text(target, 256)) { hd_skip(report, line.name, "invalid_target"); return; }
    labels = json_object_new_array();
    json_object_array_add(labels, json_object_new_string(group));
    if (line.port && hd_text(line.port, 16) && strspn(line.port, "0123456789") == strlen(line.port)) {
        char label[32]; snprintf(label, sizeof(label), "port:%s", line.port);
        json_object_array_add(labels, json_object_new_string(label));
    }
    hd_record_add(report, line.name, target, labels);
    json_object_put(labels);
}
json_object *hd_parse_ansible(const char *text) {
    json_object *report = hd_report("acquired", NULL); char *copy = strdup(text ? text : ""), *save = NULL, *line;
    char group[128] = "ungrouped"; bool ignore = false; int64_t ignored = 0;
    if (!copy) { json_object_put(report); return hd_report("unavailable", "unreadable"); }
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *comment, *end;
        line[strcspn(line, "\r")] = '\0';
        while (*line == ' ' || *line == '\t') line++;
        comment = strstr(line, " #"); if (comment) *comment = '\0';
        comment = strstr(line, " ;"); if (comment) *comment = '\0';
        end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        if (!*line || *line == '#' || *line == ';') continue;
        if (*line == '[') {
            char *close = strchr(line, ']');
            if (!close) { hd_skip(report, NULL, "invalid_section"); ignore = true; continue; }
            *close = '\0';
            ignore = strchr(line + 1, ':') != NULL;
            if (ignore) ignored++;
            else if (f_copy(group, sizeof(group), line + 1) || !hd_text(group, 128)) { hd_skip(report, NULL, "invalid_section"); ignore = true; }
            continue;
        }
        if (!ignore) host_record(report, line, group);
    }
    free(copy);
    json_object_object_add(report, "sections_ignored", json_object_new_int64(ignored));
    if (json_object_get_boolean(f_field(report, "truncated"))) { f_string_add(report, "status", "partial"); f_string_add(report, "reason", "record_limit"); }
    return report;
}
json_object *hd_scan_ansible(const char *path) {
    struct stat st; char *text; json_object *report;
    if (stat(path, &st)) return hd_report("unavailable", "file_missing");
    if (!S_ISREG(st.st_mode)) return hd_report("unavailable", "not_a_file");
    if ((size_t)st.st_size > HD_SOURCE_BYTES) return hd_report("unavailable", "size_limit");
    text = f_read(path, HD_SOURCE_BYTES);
    if (!text) return hd_report("unavailable", "unreadable");
    report = hd_parse_ansible(text);
    json_object_object_add(report, "bytes", json_object_new_int64((int64_t)strlen(text)));
    free(text);
    return report;
}
