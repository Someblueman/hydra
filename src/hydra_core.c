#include "libhydra.h"
#include "hydra_statistics.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *out) {
    fputs("usage: hydra-core [--version|--protocol-version|validate-events <file>|snapshot <root>|statistics-json <feed>|statistics-compare <left> <right> [--format json|text]]\n", out);
}

static int statistics_command(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "statistics-json")) return hs_statistics_cli_json(argv[2], stdout, stderr);
    if ((argc == 4 || argc == 6) && !strcmp(argv[1], "statistics-compare")) {
        if (argc == 6 && (strcmp(argv[4], "--format") || (strcmp(argv[5], "json") && strcmp(argv[5], "text")))) return -1;
        return hs_statistics_cli_compare(argv[2], argv[3], argc == 6 && !strcmp(argv[5], "text"), stdout, stderr);
    }
    return -1;
}

static int information_command(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--protocol-version") == 0) {
        printf("%d\n", HYDRA_PROTOCOL_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Hydra core %s protocol %d\n", HYDRA_CORE_VERSION, HYDRA_PROTOCOL_VERSION);
        return 0;
    }
    return -1;
}

static int data_command(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "validate-events") == 0) {
        if (hydra_validate_events(argv[2], stderr) != 0) return 1;
        fputs("{\"protocol_version\":1,\"ok\":true,\"validation\":\"events-v1\"}\n", stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "snapshot") == 0) {
        return hydra_write_snapshot(argv[2], stdout, stderr) == 0 ? 0 : 1;
    }
    return -1;
}

int main(int argc, char **argv) {
    int result = information_command(argc, argv);
    if (result < 0) result = statistics_command(argc, argv);
    if (result < 0) result = data_command(argc, argv);
    if (result >= 0) return result;
    usage(stderr);
    return 2;
}
