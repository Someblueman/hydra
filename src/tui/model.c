#define _POSIX_C_SOURCE 200809L
#include "model.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static size_t split_fields(char *line, char **fields, size_t capacity) {
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

int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size) {
    char *line = NULL;
    size_t line_size = 0U;
    ssize_t length;
    bool handshake = false, fleet_stream = false;
    memset(model, 0, sizeof(*model));
    while ((length = getline(&line, &line_size, input)) >= 0) {
        char *fields[40];
        size_t count;
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (!handshake) {
            if (count != 2U || !((strcmp(fields[0], "HYDRA_TUI") == 0 && strcmp(fields[1], "2") == 0) || (strcmp(fields[0], "HYDRA_FLEET_TUI") == 0 && strcmp(fields[1], "1") == 0))) {
                copy_text(error, error_size, "native data protocol handshake failed");
                free(line);
                return -1;
            }
            fleet_stream = strcmp(fields[0], "HYDRA_FLEET_TUI") == 0;
            handshake = true;
            continue;
        }
        if (fleet_stream && count == 7U && strcmp(fields[0], "F") == 0) {
            if (fleet_record(model, fields, error, error_size)) { free(line); return -1; }
        } else if (!fleet_stream && count == 30U && strcmp(fields[0], "H") == 0) {
            if (head_record(model, fields, error, error_size)) { free(line); return -1; }
        } else if (count == 6U && strcmp(fields[0], "R") == 0) {
            struct recovery *recovery;
            if (model->recovery_count >= MAX_RECOVERY) continue;
            recovery = &model->recovery[model->recovery_count++];
            memset(recovery, 0, sizeof(*recovery));
            copy_text(recovery->kind, sizeof(recovery->kind), fields[1]);
            copy_text(recovery->label, sizeof(recovery->label), fields[2]);
            copy_text(recovery->source, sizeof(recovery->source), fields[3]);
            copy_text(recovery->confidence, sizeof(recovery->confidence), fields[4]);
            copy_text(recovery->action, sizeof(recovery->action), fields[5]);
        } else {
            copy_text(error, error_size, "native data contains a malformed record");
            free(line);
            return -1;
        }
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

