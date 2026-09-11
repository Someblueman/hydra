#include "example.h"
#include <string.h>

const char *f_home, *f_hydra, *ex_program;
int main(int argc, char **argv) {
    ex_program = argv[0];
    if (argc == 2) {
        if (!strcmp(argv[1], "manifest-check")) return ex_manifest_check();
        if (!strcmp(argv[1], "pattern-check")) return ex_pattern_check();
        if (!strcmp(argv[1], "staged-produce")) return ex_staged_produce();
        if (!strcmp(argv[1], "staged-check")) return ex_staged_check(false);
        if (!strcmp(argv[1], "feature-evidence")) return ex_feature_evidence();
        if (!strcmp(argv[1], "research-report")) return ex_research_report();
        if (!strcmp(argv[1], "research-check")) return ex_research_check();
        if (!strcmp(argv[1], "performance-measure")) return ex_performance_measure();
        if (!strcmp(argv[1], "performance-analyze")) return ex_performance_analyze();
        if (!strcmp(argv[1], "performance-check")) return ex_performance_check();
    }
    if (argc == 3 && !strcmp(argv[1], "staged-check") && !strcmp(argv[2], "stage1"))
        return ex_staged_check(true);
    fprintf(stderr, "usage: %s manifest-check|pattern-check|staged-produce|staged-check [stage1]|"
            "feature-evidence|research-report|research-check|performance-measure|performance-analyze|performance-check\n", argv[0]);
    return 2;
}
