#ifndef HYDRA_TUI_MODEL_H
#define HYDRA_TUI_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_HEADS 512
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

struct model {
    struct head heads[MAX_HEADS];
    struct recovery recovery[MAX_RECOVERY];
    size_t head_count, recovery_count;
};

/* Caller owns model and error buffers. Input FILE remains caller-owned. */
int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size);
int load_fixture(const char *path, struct model *model, char *error, size_t error_size);

#endif
