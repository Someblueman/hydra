#ifndef HYDRA_TUI_MODEL_H
#define HYDRA_TUI_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_HEADS 512
#define MAX_TASKS 512
#define MAX_RECOVERY 512
#define TEXT 256
#define SOURCE_TEXT 768
#define MAX_DATA_BYTES (8U * 1024U * 1024U)

struct head {
    char remote_host[128], remote_project[SOURCE_TEXT], remote_branch[TEXT];
    char branch[TEXT], session[TEXT], profile[TEXT], group[TEXT], pr[TEXT];
    char status[32], liveness[32], declared[64], observed[64], confidence[64];
    char instance[TEXT], desired[64], source[SOURCE_TEXT], head_id[TEXT];
    char adapter[64], adapter_confidence[64], adapter_source[SOURCE_TEXT];
    char notification_source[SOURCE_TEXT];
    unsigned notifications;
    unsigned events, signals, messages, claims, scopes, queue, resources, diff, gates, approved;
};

struct recovery {
    char kind[64], label[TEXT], source[SOURCE_TEXT], confidence[64], action[TEXT];
};

struct host_observation {
    char name[128], state[40], error[128], connection[40], freshness[32];
    char last_confirmed[40], age[40];
    unsigned heads, tasks;
};

struct task_observation {
    char host[128], task_id[128], run_id[128], step_id[128], attempt_id[128];
    char workspace[SOURCE_TEXT], profile[128], owner[64], state[64];
    char waiting_reason[32], waiting_detail[TEXT], next_action[TEXT];
    char receiver_observed[40], last_confirmed[40], freshness[32];
    char result_state[32], verification_state[32];
    unsigned pending;
};

struct model {
    struct head heads[MAX_HEADS];
    struct recovery recovery[MAX_RECOVERY];
    size_t head_count, recovery_count;
    struct host_observation hosts[16];
    size_t host_count;
    struct task_observation tasks[MAX_TASKS];
    size_t task_count;
};

/* Caller owns model and error buffers. Input FILE remains caller-owned. */
int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size);
int load_fixture(const char *path, struct model *model, char *error, size_t error_size);

#endif
