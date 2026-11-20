#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/agent/agent.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t retained_bytes(size_t bytes) { return bytes > AGENT_OUTPUT_LIMIT ? AGENT_OUTPUT_LIMIT : bytes; }

/* Writes the captured output into the stage directory and renames it to the run. */
static int retain_publish(const char *root, const char *run, const char *stage, const struct f_capture *capture) {
    char path[F_PATH], destination[F_PATH];
    return f_path(path, sizeof(path), stage, "stdout") || f_write(path, capture->out, retained_bytes(capture->out_bytes), false) ||
           f_path(path, sizeof(path), stage, "stderr") || f_write(path, capture->err, retained_bytes(capture->err_bytes), false) ||
           f_path(destination, sizeof(destination), root, run) || rename(stage, destination);
}

/* The path of a retained run entry, which must be a directory. */
static int retain_entry(const char *root, const char *name, char *path, size_t size, struct stat *st) {
    return f_path(path, size, root, name) || lstat(path, st) || !S_ISDIR(st->st_mode);
}

/* Remembers path as the oldest run other than run, when it is. */
static int retain_note_oldest(const char *name, const char *run, const char *path, time_t mtime,
                              char *oldest, size_t size, time_t *oldest_time) {
    if (!strcmp(name, run) || (*oldest && mtime >= *oldest_time)) return 0;
    if (f_copy(oldest, size, path)) return -1;
    *oldest_time = mtime;
    return 0;
}

/* Counts the retained runs and names the oldest one other than run. */
static int retain_scan(const char *root, const char *run, size_t *count, char *oldest, size_t size) {
    DIR *dir = opendir(root); struct dirent *entry; struct stat st; char path[F_PATH]; time_t oldest_time = 0;
    if (!dir) return -1;
    *count = 0; oldest[0] = '\0';
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "run_", 4) || !f_name(entry->d_name)) continue;
        if (retain_entry(root, entry->d_name, path, sizeof(path), &st) ||
            retain_note_oldest(entry->d_name, run, path, st.st_mtime, oldest, size, &oldest_time)) { closedir(dir); return -1; }
        (*count)++;
    }
    closedir(dir);
    return 0;
}

static int retain_prune(const char *root, const char *run) {
    size_t count; char oldest[F_PATH];
    for (;;) {
        if (retain_scan(root, run, &count, oldest, sizeof(oldest))) return -1;
        if (count <= 10) return 0;
        if (!*oldest || f_remove_tree(oldest)) return -1;
    }
}

/* Only completed, explicitly retained provider payloads live in this directory.
 * Per-head shell execution locking serializes publication and pruning. */
int agent_retain(const char *head, const char *run, const struct f_capture *capture) {
    char root[F_PATH], stage[F_PATH];
    struct stat st; bool fresh;
    if (!f_name(run) || strncmp(run, "run_", 4) || f_path(root, sizeof(root), head, "agent-payloads")) return -1;
    /* A directory made now needs its own entry in HEAD synced as well. */
    fresh = lstat(root, &st) && errno == ENOENT;
    if (f_mkdirs(root) || lstat(root, &st) || !S_ISDIR(st.st_mode) ||
        snprintf(stage, sizeof(stage), "%s/.retain.XXXXXX", root) >= (int)sizeof(stage) || !mkdtemp(stage)) return -1;
    if (retain_publish(root, run, stage, capture)) { f_remove_tree(stage); return -1; }
    if (retain_prune(root, run)) return -1;
    return task_sync_dir(root) || (fresh && f_sync_dir(head)) ? -1 : 0;
}
