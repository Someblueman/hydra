#ifndef HYDRA_TUI_WORKFLOW_H
#define HYDRA_TUI_WORKFLOW_H
#include <stdbool.h>
#include <stddef.h>

#define WF_RUNS 32
#define WF_NODES 512
#define WF_HEADS 256
/* Unknown numbers are negative; unknown text is empty. Nothing is inferred. */
struct workflow_run {
    char id[80], name[80], state[40], kind[16], planning[256], planning_head[80], created[40], digest[16];
    long long completed;
};
/* The latest attempt of an agent (profile) step, from its receipt summary. */
struct workflow_exec {
    char profile[64], state[40], version[128], model[96], model_source[16];
    char effort[40], effort_source[16], cost[24], config_source[256];
    long long exit_status, started, finished, tokens_in, tokens_cached, tokens_out;
    unsigned attempt;
    bool present;
};
struct workflow_node {
    size_t run;
    char id[65], kind[32], state[40], needs[1024], role[16], head[256], profile[64];
    unsigned attempts;
    long long started, completed;
    struct workflow_exec exec;
};
/* A head created by one of a run's spawn steps; class is worker, verifier or
 * run (a workflow without roles). Retirement is "-", retired, kept or failed. */
struct workflow_head {
    size_t run;
    char branch[256], step[65], role[16], retirement[16], detail[256];
};
struct workflow_model {
    struct workflow_run runs[WF_RUNS];
    struct workflow_node nodes[WF_NODES];
    struct workflow_head heads[WF_HEADS];
    size_t run_count, node_count, head_count;
    char warning[256];
};
#endif
