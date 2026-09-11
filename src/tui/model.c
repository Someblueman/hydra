#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

size_t split_fields(char *line, char **fields, size_t capacity) {
    size_t count = 0U;
    char *cursor = line;
    if (capacity == 0U) return 0U;
    fields[count++] = cursor;
    while (*cursor != '\0') {
        if (*cursor == '\t') {
            *cursor = '\0';
            if (count < capacity) fields[count++] = cursor + 1;
        }
        cursor++;
    }
    return count;
}

static int fleet_record(struct model *model, char **fields, char *error, size_t error_size) {
    struct head *head;
    if (model->head_count >= MAX_HEADS || strlen(fields[1]) >= 128U || strlen(fields[2]) >= SOURCE_TEXT || strlen(fields[3]) >= TEXT || strlen(fields[4]) >= TEXT || strlen(fields[5]) >= TEXT) {
        copy_text(error, error_size, "fleet row exceeds native bounds"); return -1;
    }
    head = &model->heads[model->head_count++];
    copy_text(head->remote_host, sizeof(head->remote_host), fields[1]);
    copy_text(head->remote_project, sizeof(head->remote_project), fields[2]);
    copy_text(head->remote_branch, sizeof(head->remote_branch), fields[3]);
    (void)snprintf(head->branch, sizeof(head->branch), "%.80s/%.170s", fields[1], fields[3]);
    copy_text(head->head_id, sizeof(head->head_id), fields[4]);
    copy_text(head->instance, sizeof(head->instance), fields[5]);
    copy_text(head->desired, sizeof(head->desired), fields[6]);
    copy_text(head->status, sizeof(head->status), fields[6]);
    copy_text(head->liveness, sizeof(head->liveness), "unknown");
    copy_text(head->source, sizeof(head->source), "remote durable state");
    return 0;
}
static int head_record(struct model *model, char **fields, char *error, size_t error_size) {
    struct head *head;
    if (model->head_count >= MAX_HEADS) {
        copy_text(error, error_size, "native data exceeds the 512-head safety bound");
        return -1;
    }
    head = &model->heads[model->head_count++];
    memset(head, 0, sizeof(*head));
    copy_text(head->branch, sizeof(head->branch), fields[1]);
    copy_text(head->session, sizeof(head->session), fields[2]);
    copy_text(head->profile, sizeof(head->profile), fields[3]);
    copy_text(head->group, sizeof(head->group), fields[4]);
    copy_text(head->pr, sizeof(head->pr), fields[5]);
    copy_text(head->status, sizeof(head->status), fields[6]);
    copy_text(head->liveness, sizeof(head->liveness), fields[7]);
    copy_text(head->declared, sizeof(head->declared), fields[8]);
    copy_text(head->observed, sizeof(head->observed), fields[9]);
    copy_text(head->confidence, sizeof(head->confidence), fields[10]);
    copy_text(head->instance, sizeof(head->instance), fields[11]);
    if (!parse_unsigned(fields[12], &head->events) ||
        !parse_unsigned(fields[13], &head->signals) ||
        !parse_unsigned(fields[14], &head->messages) ||
        !parse_unsigned(fields[15], &head->claims) ||
        !parse_unsigned(fields[16], &head->scopes) ||
        !parse_unsigned(fields[17], &head->queue) ||
        !parse_unsigned(fields[18], &head->resources) ||
        !parse_unsigned(fields[19], &head->diff) ||
        !parse_unsigned(fields[20], &head->gates) ||
        !parse_unsigned(fields[21], &head->approved) ||
        !parse_unsigned(fields[28], &head->notifications)) {
        copy_text(error, error_size, "native data contains an invalid numeric field");
        return -1;
    }
    copy_text(head->desired, sizeof(head->desired), fields[22]);
    copy_text(head->source, sizeof(head->source), fields[23]);
    copy_text(head->head_id, sizeof(head->head_id), fields[24]);
    copy_text(head->adapter, sizeof(head->adapter), fields[25]);
    copy_text(head->adapter_confidence, sizeof(head->adapter_confidence), fields[26]);
    copy_text(head->adapter_source, sizeof(head->adapter_source), fields[27]);
    copy_text(head->notification_source, sizeof(head->notification_source), fields[29]);
    return 0;
}

static int host_record(struct model *model, char **fields, char *error, size_t error_size) {
    struct host_observation *host;
    size_t i;
    if (model->host_count == 16 || !fields[1][0] || strlen(fields[1]) >= 128 || strlen(fields[4]) >= 128 ||
        (strcmp(fields[2], "responded") && strcmp(fields[2], "failed"))) {
        copy_text(error, error_size, "invalid fleet host observation"); return -1;
    }
    for (i = 0; i < model->host_count; i++) if (!strcmp(model->hosts[i].name, fields[1])) {
        copy_text(error, error_size, "duplicate fleet host observation"); return -1;
    }
    host = &model->hosts[model->host_count++];
    memset(host, 0, sizeof(*host));
    copy_text(host->name, sizeof(host->name), fields[1]); copy_text(host->state, sizeof(host->state), fields[2]);
    copy_text(host->error, sizeof(host->error), fields[4]);
    copy_text(host->connection, sizeof(host->connection), !strcmp(fields[2], "responded") ? "reachable" : "unreachable");
    copy_text(host->freshness, sizeof(host->freshness), "unknown");
    copy_text(host->last_confirmed, sizeof(host->last_confirmed), "-");
    copy_text(host->age, sizeof(host->age), "-");
    if (!parse_unsigned(fields[3], &host->heads)) { copy_text(error, error_size, "invalid fleet head count"); return -1; }
    return 0;
}

static bool host_connection(const char *value) {
    return !strcmp(value, "reachable") || !strcmp(value, "unreachable") ||
        !strcmp(value, "authentication") || !strcmp(value, "unsupported") ||
        !strcmp(value, "malformed") || !strcmp(value, "remote_error") ||
        !strcmp(value, "unknown");
}

static bool freshness_state(const char *value) {
    return !strcmp(value, "fresh") || !strcmp(value, "stale") || !strcmp(value, "unknown");
}

static bool bounded_field(const char *value, size_t limit) {
    const unsigned char *p = (const unsigned char *)value;
    if (!value || strlen(value) >= limit || strchr(value, '\t')) return false;
    for (; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}

static int host_record_v3(struct model *model, char **fields, char *error, size_t error_size) {
    struct host_observation *host;
    size_t i;
    unsigned heads;
    if (model->host_count == 16 || !bounded_field(fields[1], sizeof(model->hosts[0].name)) ||
        !fields[1][0] || (strcmp(fields[2], "responded") && strcmp(fields[2], "failed")) ||
        !parse_unsigned(fields[3], &heads) || !bounded_field(fields[4], sizeof(model->hosts[0].error)) ||
        !host_connection(fields[5]) || !freshness_state(fields[6]) ||
        !bounded_field(fields[7], sizeof(model->hosts[0].last_confirmed)) ||
        !bounded_field(fields[8], sizeof(model->hosts[0].age))) {
        copy_text(error, error_size, "invalid version 3 fleet host observation"); return -1;
    }
    for (i = 0; i < model->host_count; i++) if (!strcmp(model->hosts[i].name, fields[1])) {
        copy_text(error, error_size, "duplicate fleet host observation"); return -1;
    }
    host = &model->hosts[model->host_count++]; memset(host, 0, sizeof(*host));
    copy_text(host->name, sizeof(host->name), fields[1]); copy_text(host->state, sizeof(host->state), fields[2]);
    copy_text(host->error, sizeof(host->error), fields[4]); copy_text(host->connection, sizeof(host->connection), fields[5]);
    copy_text(host->freshness, sizeof(host->freshness), fields[6]); copy_text(host->last_confirmed, sizeof(host->last_confirmed), fields[7]);
    copy_text(host->age, sizeof(host->age), fields[8]);
    host->heads = heads;
    return 0;
}

static int task_record(struct model *model, size_t count, char **fields, char *error, size_t error_size) {
    struct task_observation *task;
    if (model->task_count >= MAX_TASKS || !bounded_field(fields[1], sizeof(model->tasks[0].host)) ||
        !bounded_field(fields[2], sizeof(model->tasks[0].task_id)) || !bounded_field(fields[3], sizeof(model->tasks[0].run_id)) ||
        !bounded_field(fields[4], sizeof(model->tasks[0].step_id)) || !bounded_field(fields[5], sizeof(model->tasks[0].attempt_id)) ||
        !bounded_field(fields[6], sizeof(model->tasks[0].workspace)) || !bounded_field(fields[7], sizeof(model->tasks[0].profile)) ||
        !bounded_field(fields[8], sizeof(model->tasks[0].owner)) || !bounded_field(fields[9], sizeof(model->tasks[0].state)) ||
        !bounded_field(fields[10], sizeof(model->tasks[0].waiting_reason)) || !bounded_field(fields[11], sizeof(model->tasks[0].waiting_detail)) ||
        !bounded_field(fields[12], sizeof(model->tasks[0].next_action)) || !bounded_field(fields[13], sizeof(model->tasks[0].receiver_observed)) ||
        !bounded_field(fields[14], sizeof(model->tasks[0].last_confirmed)) || !freshness_state(fields[15])) {
        copy_text(error, error_size, "invalid fleet task observation"); return -1;
    }
    task = &model->tasks[model->task_count++]; memset(task, 0, sizeof(*task));
    copy_text(task->host, sizeof(task->host), fields[1]); copy_text(task->task_id, sizeof(task->task_id), fields[2]);
    copy_text(task->run_id, sizeof(task->run_id), fields[3]); copy_text(task->step_id, sizeof(task->step_id), fields[4]);
    copy_text(task->attempt_id, sizeof(task->attempt_id), fields[5]); copy_text(task->workspace, sizeof(task->workspace), fields[6]);
    copy_text(task->profile, sizeof(task->profile), fields[7]); copy_text(task->owner, sizeof(task->owner), fields[8]);
    copy_text(task->state, sizeof(task->state), fields[9]); copy_text(task->waiting_reason, sizeof(task->waiting_reason), fields[10]);
    copy_text(task->waiting_detail, sizeof(task->waiting_detail), fields[11]); copy_text(task->next_action, sizeof(task->next_action), fields[12]);
    copy_text(task->receiver_observed, sizeof(task->receiver_observed), fields[13]); copy_text(task->last_confirmed, sizeof(task->last_confirmed), fields[14]);
    copy_text(task->freshness, sizeof(task->freshness), fields[15]);
    if (!parse_unsigned(fields[16], &task->pending)) { copy_text(error, error_size, "invalid fleet task pending count"); return -1; }
    copy_text(task->result_state, sizeof(task->result_state), count > 17U ? fields[17] : "unavailable");
    copy_text(task->verification_state, sizeof(task->verification_state), count > 18U ? fields[18] : "unavailable");
    copy_text(task->spec_sha256, sizeof(task->spec_sha256), count > 19U ? fields[19] : "-");
    copy_text(task->cancellation, sizeof(task->cancellation), count > 20U ? fields[20] : "-");
    copy_text(task->cancellation_scope, sizeof(task->cancellation_scope), count > 21U ? fields[21] : "-");
    copy_text(task->cancel_requested_at, sizeof(task->cancel_requested_at), count > 22U ? fields[22] : "-");
    copy_text(task->request_id, sizeof(task->request_id), count > 23U ? fields[23] : "-");
    {
        size_t i;
        for (i = 0; i < model->host_count; i++) if (!strcmp(model->hosts[i].name, task->host)) {
            model->hosts[i].tasks++;
            break;
        }
    }
    return 0;
}

static bool valid_handshake(size_t count, char **fields) {
    if (count != 2U) return false;
    if (!strcmp(fields[0], "HYDRA_TUI")) return !strcmp(fields[1], "2");
    return !strcmp(fields[0], "HYDRA_FLEET_TUI") &&
        (!strcmp(fields[1], "1") || !strcmp(fields[1], "2") || !strcmp(fields[1], "3"));
}

static void recovery_record(struct model *model, char **fields) {
    struct recovery *recovery;
    if (model->recovery_count >= MAX_RECOVERY) return;
    recovery = &model->recovery[model->recovery_count++];
    memset(recovery, 0, sizeof(*recovery));
    copy_text(recovery->kind, sizeof(recovery->kind), fields[1]);
    copy_text(recovery->label, sizeof(recovery->label), fields[2]);
    copy_text(recovery->source, sizeof(recovery->source), fields[3]);
    copy_text(recovery->confidence, sizeof(recovery->confidence), fields[4]);
    copy_text(recovery->action, sizeof(recovery->action), fields[5]);
}

struct stream_state {
    bool fleet_stream;
    bool fleet_hosts;
    bool fleet_v3;
};

static int parse_handshake(size_t count, char **fields, struct stream_state *state, char *error, size_t error_size) {
    if (!valid_handshake(count, fields)) {
        copy_text(error, error_size, "native data protocol handshake failed");
        return -1;
    }
    state->fleet_stream = strcmp(fields[0], "HYDRA_FLEET_TUI") == 0;
    state->fleet_v3 = state->fleet_stream && !strcmp(fields[1], "3");
    state->fleet_hosts = state->fleet_stream && (!strcmp(fields[1], "2") || state->fleet_v3);
    return 0;
}

static int fleet_record_line(struct model *model, const struct stream_state *state, size_t count, char **fields,
                             char *error, size_t error_size) {
    if (state->fleet_v3 && count == 9U && !strcmp(fields[0], "T")) return host_record_v3(model, fields, error, error_size);
    if (state->fleet_hosts && count == 5 && !strcmp(fields[0], "T")) return host_record(model, fields, error, error_size);
    if (state->fleet_stream && count == 7U && !strcmp(fields[0], "F")) return fleet_record(model, fields, error, error_size);
    if (state->fleet_v3 && (count == 17U || count == 19U || count == 24U) && !strcmp(fields[0], "O")) return task_record(model, count, fields, error, error_size);
    return 1;
}

static int local_record_line(struct model *model, size_t count, char **fields, char *error, size_t error_size) {
    if (count == 30U && !strcmp(fields[0], "H")) return head_record(model, fields, error, error_size);
    if (count == 6U && !strcmp(fields[0], "R")) { recovery_record(model, fields); return 0; }
    return 1;
}

static int parse_record(struct model *model, const struct stream_state *state, size_t count, char **fields,
                        char *error, size_t error_size) {
    int result;
    if (count == 6U && !strcmp(fields[0], "R")) { recovery_record(model, fields); return 0; }
    result = state->fleet_stream ? fleet_record_line(model, state, count, fields, error, error_size) :
        local_record_line(model, count, fields, error, error_size);
    if (result != 1) return result;
    copy_text(error, error_size, "native data contains a malformed record");
    return -1;
}

int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size) {
    char *line = NULL;
    size_t line_size = 0U;
    ssize_t length;
    bool handshake = false; struct stream_state state = {0};
    memset(model, 0, sizeof(*model));
    while ((length = getline(&line, &line_size, input)) >= 0) {
        char *fields[40];
        size_t count;
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (!handshake) {
            if (parse_handshake(count, fields, &state, error, error_size)) { free(line); return -1; }
            handshake = true;
            continue;
        }
        if (parse_record(model, &state, count, fields, error, error_size)) { free(line); return -1; }
    }
    free(line);
    if (!handshake) {
        copy_text(error, error_size, "native data is empty");
        return -1;
    }
    return 0;
}

int load_fixture(const char *path, struct model *model, char *error, size_t error_size) {
    FILE *input = fopen(path, "r");
    int result;
    if (input == NULL) {
        snprintf(error, error_size, "cannot open fixture: %s", path);
        return -1;
    }
    result = load_model_stream(input, model, error, error_size);
    fclose(input);
    return result;
}
