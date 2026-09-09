#ifndef HYDRA_TUI_WORKFLOW_H
#define HYDRA_TUI_WORKFLOW_H

#define WF_RUNS 32
#define WF_NODES 512
struct workflow_run { char id[80], name[80], state[40]; };
struct workflow_node { size_t run; char id[65], kind[32], state[40], needs[1024]; unsigned attempts; };
struct workflow_model {
    struct workflow_run runs[WF_RUNS];
    struct workflow_node nodes[WF_NODES];
    size_t run_count, node_count;
    char warning[256];
};
#endif
