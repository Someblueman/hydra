#include "fleet.h"
#include <string.h>
/* Fleet is a separate internal adapter. Control bytes cannot enter terminal rows. */
static void field(const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    for (; *p; p++) putchar(*p < 32 || *p == 127 ? '?' : *p);
}
static bool routable(const char *value, size_t limit) {
    const unsigned char *p = (const unsigned char *)value;
    if (!value || strlen(value) >= limit) return false;
    for (; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}
int f_tui_data(unsigned seconds, unsigned jobs) {
    json_object *result = f_aggregate("list", seconds, jobs), *hosts = f_field(f_field(result, "data"), "hosts"); size_t i, j;
    puts("HYDRA_FLEET_TUI\t1");
    if (!json_object_is_type(hosts, json_type_array)) puts("R\tfleet\tall\tlimit or adapter failure\tobserved\tinspect CLI");
    for (i = 0; json_object_is_type(hosts, json_type_array) && i < json_object_array_length(hosts); i++) {
        json_object *host = json_object_array_get_idx(hosts, i), *heads = f_field(f_field(host, "data"), "heads"); const char *name = f_string(host, "host");
        if (!json_object_get_boolean(f_field(host, "ok"))) {
            printf("R\tfleet\t"); field(name); printf("\t"); field(f_string(f_field(host, "error"), "code")); puts("\tobserved\treconcile"); continue;
        }
        for (j = 0; json_object_is_type(heads, json_type_array) && j < json_object_array_length(heads); j++) {
            json_object *head = json_object_array_get_idx(heads, j);
            const char *keys[] = {"project_path", "branch", "head_id", "current_instance", "desired_state", NULL}; size_t k;
            if (!routable(name, 128) || !routable(f_string(head, "project_path"), 768) || !routable(f_string(head, "branch"), 256) || !routable(f_string(head, "head_id"), 256) || !routable(f_string(head, "current_instance"), 256)) {
                printf("R\tfleet\t"); field(name); puts("\thead exceeds TUI text bounds\tobserved\tuse CLI"); continue;
            }
            printf("F\t"); field(name);
            for (k = 0; keys[k]; k++) { putchar('\t'); field(f_string(head, keys[k])); }
            putchar('\n');
        }
    }
    json_object_put(result); return ferror(stdout) ? 1 : 0;
}
