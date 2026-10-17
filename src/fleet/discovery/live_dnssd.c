#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* mDNS/DNS-SD adapter over the normal `avahi-browse` client (terminating,
 * resolving, parsable output). Without the client the source is reported as
 * unavailable: Hydra never scans the network itself. Live multicast
 * acquisition is unqualified; see docs/FLEET.md. */
static bool on_path(const char *name) {
    const char *path = getenv("PATH"); char copy[16384], file[F_PATH], *save = NULL, *dir;
    struct stat st;
    if (!path || f_copy(copy, sizeof(copy), path)) return false;
    for (dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        if (!*dir || f_path(file, sizeof(file), dir, name)) continue;
        if (!stat(file, &st) && S_ISREG(st.st_mode) && !access(file, X_OK)) return true;
    }
    return false;
}
/* avahi -k output escapes bytes as \ddd; only printable results are kept. */
static bool unescape(const char *in, char out[256]) {
    size_t n = 0;
    while (*in) {
        unsigned value;
        if (*in == '\\' && in[1] >= '0' && in[1] <= '9' && in[2] >= '0' && in[2] <= '9' && in[3] >= '0' && in[3] <= '9') {
            value = (unsigned)((in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0'));
            if (value > 255) return false;
            out[n++] = (char)value; in += 4;
        } else out[n++] = *in++;
        if (n + 1 >= 256) return false;
    }
    out[n] = '\0';
    return hd_text(out, 128);
}
static void service_line(json_object *report, char *line) {
    char *fields[10] = {0}, *save = NULL, *field; size_t count = 0; char name[256], label[300]; json_object *labels;
    for (field = strtok_r(line, ";", &save); field && count < 10; field = strtok_r(NULL, ";", &save)) fields[count++] = field;
    if (count < 9 || strcmp(fields[0], "=")) return;
    if (strcmp(fields[4], "_ssh._tcp")) { hd_skip(report, NULL, "service_type"); return; }
    if (!unescape(fields[3], name)) { hd_skip(report, NULL, "invalid_name"); return; }
    if (strcmp(fields[8], "22")) { hd_skip(report, name, "port"); return; }
    if (!f_target(fields[6]) || !hd_text(fields[6], 256)) { hd_skip(report, name, "invalid_target"); return; }
    labels = json_object_new_array();
    json_object_array_add(labels, json_object_new_string("mdns"));
    if (hd_text(fields[2], 16)) json_object_array_add(labels, json_object_new_string(fields[2]));
    if (hd_text(fields[1], 64) && snprintf(label, sizeof(label), "interface:%s", fields[1]) < (int)sizeof(label)) json_object_array_add(labels, json_object_new_string(label));
    if (hd_text(fields[5], 128) && snprintf(label, sizeof(label), "domain:%s", fields[5]) < (int)sizeof(label)) json_object_array_add(labels, json_object_new_string(label));
    hd_record_add(report, name, fields[6], labels);
    json_object_put(labels);
}
json_object *hd_parse_avahi(const char *text) {
    json_object *report = hd_report("acquired", NULL); char *copy = strdup(text ? text : ""), *save = NULL, *line;
    if (!copy) { json_object_put(report); return hd_report("unavailable", "unreadable"); }
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        line[strcspn(line, "\r")] = '\0';
        if (line[0] == '=') service_line(report, line);
    }
    free(copy);
    if (json_object_get_boolean(f_field(report, "truncated"))) { f_string_add(report, "status", "partial"); f_string_add(report, "reason", "record_limit"); }
    return report;
}
json_object *hd_scan_dnssd(const char *domain, unsigned seconds) {
    struct f_capture cap = {0}; json_object *report; bool limited;
    char *argv[] = {"avahi-browse", "-rtpk", "-d", (char *)domain, "_ssh._tcp", NULL};
    if (!on_path("avahi-browse")) return hd_report("unavailable", on_path("dns-sd") ? "client_unsupported" : "client_missing");
    if (f_run(argv, NULL, 0, seconds, &cap)) { f_capture_free(&cap); return hd_report("unavailable", "client_failed"); }
    limited = cap.out_bytes > HD_SOURCE_BYTES;
    if (limited) cap.out[HD_SOURCE_BYTES] = '\0';
    if (cap.timeout && !cap.out_bytes) report = hd_report("unavailable", "timeout");
    else if (cap.status && !cap.out_bytes) report = hd_report("unavailable", f_stopped ? "cancelled" : "client_failed");
    else {
        report = hd_parse_avahi(cap.out);
        if (cap.timeout || cap.status || limited) {
            f_string_add(report, "status", "partial");
            f_string_add(report, "reason", cap.timeout ? "timeout" : limited ? "output_limit" : "client_failed");
        }
    }
    json_object_object_add(report, "bytes", json_object_new_int64((int64_t)cap.out_bytes));
    json_object_object_add(report, "client_status", json_object_new_int(cap.status));
    f_capture_free(&cap);
    return report;
}
