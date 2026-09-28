/* `hydra remote setup list`: one row per setup record in
 * $HYDRA_HOME/fleet/setup with its destination, overall status and next
 * step. Read-only: records are opened like `setup status` (no lock, no
 * writes), so a running setup is listed as it was last persisted. */
#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define SETUP_LIST_LIMIT 256

/* NAME.json with a valid setup NAME; locks and temporary files are skipped. */
static bool record_name(const char *file, char name[128]) {
    size_t length = strlen(file);
    if (length <= 5 || length - 5 >= 128 || strcmp(file + length - 5, ".json")) return false;
    memcpy(name, file, length - 5);
    name[length - 5] = '\0';
    return setup_name_valid(name);
}

static int name_compare(const void *left, const void *right) {
    return strcmp((const char *)left, (const char *)right);
}

/* Record names in byte order; returns the count (at most SETUP_LIST_LIMIT). */
static size_t record_names(char (*names)[128]) {
    char directory[F_PATH], fleet[F_PATH];
    struct dirent *entry;
    size_t count = 0;
    DIR *dir;
    if (f_path(fleet, sizeof(fleet), f_home, "fleet") || f_path(directory, sizeof(directory), fleet, "setup")) return 0;
    if (!(dir = opendir(directory))) return 0;
    while ((entry = readdir(dir)) != NULL && count < SETUP_LIST_LIMIT)
        if (record_name(entry->d_name, names[count])) count++;
    closedir(dir);
    qsort(names, count, sizeof(names[0]), name_compare);
    return count;
}

/* A record that cannot be opened is listed with its error, never skipped. */
static void unreadable(json_object *row, json_object *failure) {
    json_object *error = json_object_new_object(), *source = f_field(failure, "error");
    f_string_add(error, "code", f_string(source, "code") ? f_string(source, "code") : "state_invalid");
    f_string_add(error, "message", f_string(source, "message") ? f_string(source, "message") : "");
    json_object_object_add(row, "destination", NULL);
    f_string_add(row, "status", "unreadable");
    json_object_object_add(row, "complete", json_object_new_boolean(false));
    json_object_object_add(row, "next", NULL);
    json_object_object_add(row, "error", error);
}

/* status: "done" when complete, otherwise the first open step's status. */
static void describe(json_object *row, struct setup_ctx *ctx) {
    char step[80];
    bool open = setup_first_open(ctx, step);
    f_string_add(row, "destination", ctx->remote.target);
    f_string_add(row, "status", open ? setup_state_status(ctx, step) : "done");
    json_object_object_add(row, "complete", json_object_new_boolean(!open));
    json_object_object_add(row, "next", setup_next_json(ctx));
}

static json_object *setup_row(const char *name, bool json) {
    struct setup_ctx ctx;
    json_object *row = json_object_new_object(), *failure;
    ctx.command = "remote-setup-list";
    failure = setup_state_open(&ctx, name, NULL, NULL, SETUP_READONLY);
    f_string_add(row, "name", name);
    ctx.json = json;
    if (failure) unreadable(row, failure);
    else describe(row, &ctx);
    json_object_put(failure);
    setup_state_close(&ctx);
    return row;
}

json_object *setup_list(bool json) {
    char (*names)[128] = calloc(SETUP_LIST_LIMIT, sizeof(*names));
    json_object *data = json_object_new_object(), *rows = json_object_new_array();
    size_t count, i;
    json_object_object_add(data, "setup_schema", json_object_new_int(SETUP_SCHEMA));
    json_object_object_add(data, "setups", rows);
    if (!names) { json_object_put(data); return f_error("remote-setup-list", "state_unavailable", "out of memory"); }
    count = record_names(names);
    for (i = 0; i < count; i++) json_object_array_add(rows, setup_row(names[i], json));
    free(names);
    return f_success("remote-setup-list", data);
}
