#define _XOPEN_SOURCE 700
#include "fleet/discovery/discovery.h"
#include "fleet/enrollment/enrollment.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include "fleet/transport/remote.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int private_directory(char path[F_PATH]) {
    char home[F_PATH]; struct stat st;
    int home_fd = -1, fleet_fd = -1, directory_fd = -1, status = -1;
    if (f_mkdirs(f_home) || !realpath(f_home, home)) goto done;
    home_fd = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (home_fd < 0 || fstat(home_fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) goto done;
    fleet_fd = task_owned_directory(home_fd, "fleet", true);
    if (fleet_fd < 0) goto done;
    directory_fd = task_owned_directory(fleet_fd, "discovery", true);
    if (directory_fd < 0 || f_path(path, F_PATH, home, "fleet/discovery")) goto done;
    status = 0;
done:
    if (directory_fd >= 0) close(directory_fd);
    if (fleet_fd >= 0) close(fleet_fd);
    if (home_fd >= 0) close(home_fd);
    return status;
}
static int progress_lock(struct hd_progress *progress, const char *export) {
    char directory[F_PATH], id[65], filename[80], lock_path[F_PATH]; struct stat st;
    json_object *label = json_object_new_object();
    f_string_add(label, "export", export);
    int status = enrollment_hash(label, id); json_object_put(label);
    if (status || private_directory(directory) || snprintf(filename, sizeof(filename), "%s.json", id) >= (int)sizeof(filename) ||
        f_path(progress->path, sizeof(progress->path), directory, filename) ||
        snprintf(lock_path, sizeof(lock_path), "%s.lock", progress->path) >= (int)sizeof(lock_path)) return -1;
    progress->lock = open(lock_path, O_CREAT | O_RDWR | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (progress->lock < 0 || fstat(progress->lock, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) ||
        fcntl(progress->lock, F_SETFD, FD_CLOEXEC)) return -1;
    return flock(progress->lock, LOCK_EX | LOCK_NB) ? -2 : 0;
}
static json_object *stable_sources(json_object *sources) {
    json_object *out = NULL;
    if (json_object_deep_copy(sources, &out, NULL)) return NULL;
    for (size_t i = 0; i < json_object_array_length(out); i++) {
        json_object *source = json_object_array_get_idx(out, i);
        json_object_object_del(source, "age_seconds");
        if (!strcmp(f_string(source, "kind"), "ssh-config")) json_object_object_del(source, "observed_at");
    }
    return out;
}
static json_object *binding(const struct hd_options *options, json_object *rows) {
    json_object *out = json_object_new_object(), *selected = json_object_new_array(); char digest[65];
    f_string_add(out, "capability", options->capability);
    if (options->inventory) {
        if (f_hash(options->inventory, digest)) goto bad;
        f_string_add(out, "inventory_sha256", digest);
    }
    if (options->config) {
        if (f_hash(options->config, digest)) goto bad;
        f_string_add(out, "ssh_config_sha256", digest);
    }
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i), *item = json_object_new_object();
        const char *policy = f_string(f_field(f_field(row, "resolution"), "data"), "policy_sha256");
        if (!policy) { json_object_put(item); goto bad; }
        f_string_add(item, "candidate_id", f_string(row, "candidate_id"));
        f_string_add(item, "target", f_string(row, "target")); f_string_add(item, "policy_sha256", policy);
        json_object_object_add(item, "sources", stable_sources(f_field(row, "sources")));
        json_object_array_add(selected, item);
    }
    json_object_object_add(out, "selected", selected); return out;
bad:
    json_object_put(selected); json_object_put(out); return NULL;
}
static bool restore_row(json_object *row, json_object *prior) {
    static const char *fields[] = {"status", "qualification", "qualified_at", "qualification_attempts", "qualification_history", "qualification_history_truncated", NULL};
    const char *status = f_string(prior, "status");
    json_object *count = f_field(prior, "qualification_attempts"), *history = f_field(prior, "qualification_history");
    if (count && (!json_object_is_type(count, json_type_int) || json_object_get_int64(count) < 0 || json_object_get_int64(count) >= 1000000)) return false;
    if (history && (!json_object_is_type(history, json_type_array) || json_object_array_length(history) > 4)) return false;
    if (!json_object_equal(f_field(row, "candidate_id"), f_field(prior, "candidate_id")) ||
        !json_object_equal(f_field(row, "target"), f_field(prior, "target")) || !status ||
        (strcmp(status, "not_qualified") && strcmp(status, "compatible") && strcmp(status, "qualification_failed"))) return false;
    if (!strcmp(status, "compatible") && (!json_object_get_boolean(f_field(f_field(prior, "qualification"), "ok")) ||
        !f_handshake_compatible(f_field(f_field(prior, "qualification"), "data")))) return false;
    for (size_t i = 0; fields[i]; i++)
        if (f_field(prior, fields[i])) json_object_object_add(row, fields[i], json_object_get(f_field(prior, fields[i])));
    f_string_add(row, "qualification_freshness", "retained; reauthentication required before enrollment");
    return true;
}
static bool restore(json_object *rows, json_object *record, json_object *expected) {
    json_object *prior = f_field(record, "candidates");
    if (!f_number_is(record, "schema_version", 1) || !json_object_equal(f_field(record, "binding"), expected) ||
        !json_object_is_type(prior, json_type_array) || json_object_array_length(prior) != json_object_array_length(rows)) return false;
    for (size_t i = 0; i < json_object_array_length(rows); i++)
        if (!restore_row(json_object_array_get_idx(rows, i), json_object_array_get_idx(prior, i))) return false;
    return true;
}
json_object *hd_progress_open(struct hd_progress *progress, const struct hd_options *options, json_object *rows) {
    json_object *expected = NULL, *error = NULL; struct stat st; int locked;
    if (!options->progress) return NULL;
    locked = progress_lock(progress, options->progress);
    if (locked) return f_error("fleet-discovery", locked == -2 ? "progress_busy" : "progress_unavailable", "cannot acquire private exclusive qualification progress");
    expected = binding(options, rows);
    if (!expected) { error = f_error("fleet-discovery", "binding_unavailable", "resolve every selected SSH policy before recording progress"); goto done; }
    if (!lstat(progress->path, &st)) {
        if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) ||
            !(progress->record = f_read_json(progress->path, F_LIMIT))) {
            error = f_error("fleet-discovery", "progress_invalid", "private qualification progress is invalid; preserve it for inspection"); goto done;
        }
        if (!restore(rows, progress->record, expected))
            error = f_error("fleet-discovery", "progress_binding_changed", "selected sources, candidates, SSH policy or saved records changed; preserve prior progress and use a new export path for renewed qualification");
    } else if (errno == ENOENT) {
        progress->record = json_object_new_object();
        json_object_object_add(progress->record, "schema_version", json_object_new_int(1));
        json_object_object_add(progress->record, "binding", json_object_get(expected));
    } else error = f_error("fleet-discovery", "progress_unavailable", "private qualification progress cannot be read");
done:
    json_object_put(expected); return error;
}
int hd_progress_save(struct hd_progress *progress, json_object *rows) {
    if (!progress->record) return 0;
    json_object_object_add(progress->record, "candidates", json_object_get(rows));
    return enrollment_write(progress->path, progress->record);
}
void hd_progress_close(struct hd_progress *progress) {
    json_object_put(progress->record); progress->record = NULL;
    if (progress->lock >= 0) close(progress->lock);
    progress->lock = -1;
}
