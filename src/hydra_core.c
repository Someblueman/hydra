#include "libhydra.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *out) {
    fputs("usage: hydra-core [--version|--protocol-version|capabilities|validate-state <root>|validate-events <file>|json-string <text>|snapshot <root>]\n", out);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--protocol-version") == 0) {
        printf("%d\n", HYDRA_PROTOCOL_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Hydra core %s protocol %d\n", HYDRA_CORE_VERSION, HYDRA_PROTOCOL_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "capabilities") == 0) {
        printf("{\"protocol_version\":%d,\"core_version\":\"%s\",\"read_only\":true,\"capabilities\":[\"canonical-json-string\",\"events-v1-validate\",\"snapshot-v1\",\"state-v2-validate\"]}\n",
               HYDRA_PROTOCOL_VERSION, HYDRA_CORE_VERSION);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "validate-state") == 0) {
        if (hydra_validate_state(argv[2], stderr) != 0) return 1;
        fputs("{\"protocol_version\":1,\"ok\":true,\"validation\":\"state-v2\"}\n", stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "validate-events") == 0) {
        if (hydra_validate_events(argv[2], stderr) != 0) return 1;
        fputs("{\"protocol_version\":1,\"ok\":true,\"validation\":\"events-v1\"}\n", stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "json-string") == 0) {
        if (hydra_json_write_string(stdout, argv[2]) != 0 || fputc('\n', stdout) == EOF) return 1;
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "snapshot") == 0) {
        return hydra_write_snapshot(argv[2], stdout, stderr) == 0 ? 0 : 1;
    }
    usage(stderr);
    return 2;
}
