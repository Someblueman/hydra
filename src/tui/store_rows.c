#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "store.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* The store half of the local head snapshot (store.h). Every rule here is the
 * shell reader's (lib/tui_native.sh and what it calls), field for field: what
 * a missing file reads as, which directory a branch name selects, how a
 * record that does not split into its columns is reported. Where the shell
 * reads a path built from an empty variable, the same path is read here. */

#define WALK_DEPTH 16

/* ---- files -------------------------------------------------------------- */

/* A path from a format; empty when it does not fit, so nothing is ever read
 * from a cut-off name. */
void store_path(char *out, size_t size, const char *format, ...) {
    va_list arguments;
    int length;
    va_start(arguments, format);
    length = vsnprintf(out, size, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= size) out[0] = '\0';
}

static void path_join(char *out, size_t size, const char *a, const char *b) {
    store_path(out, size, "%s%s", a, b);
}

static void path_child(char *out, size_t size, const char *directory, const char *name) {
    store_path(out, size, "%s/%s", directory, name);
}

static void text_copy(char *out, size_t size, const char *value) {
    (void)snprintf(out, size, "%s", value);
}

static bool is_directory(const char *path) {
    struct stat info;
    return path[0] && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_file(const char *path) {
    struct stat info;
    return path[0] && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool has_bytes(const char *path) {
    struct stat info;
    return path[0] && stat(path, &info) == 0 && info.st_size > 0;
}

/* The first line of a file, as `sed -n 1p` hands it to a shell variable.
 * False when the file cannot be read; out is then empty. */
static bool first_line(const char *path, char *out, size_t size) {
    char bytes[STORE_PATH];
    size_t used = 0, i;
    ssize_t length;
    int fd;
    out[0] = '\0';
    fd = path[0] ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    if (fd < 0) return false;
    do length = read(fd, bytes, sizeof(bytes)); while (length < 0 && errno == EINTR);
    close(fd);
    if (length < 0) return false;
    for (i = 0; i < (size_t)length && bytes[i] != '\n'; i++)
        if (bytes[i] && used + 1U < size) out[used++] = bytes[i];
    out[used] = '\0';
    return true;
}

bool store_first_line(const char *path, char *out, size_t size) {
    return first_line(path, out, size);
}

static void first_line_at(const char *directory, const char *name, char *out, size_t size) {
    char path[STORE_PATH];
    path_join(path, sizeof(path), directory, name);
    (void)first_line(path, out, size);
}

/* Tabs, carriage returns and newlines would break a row: they become spaces. */
static void make_safe(char *text) {
    for (; *text; text++) if (*text == '\t' || *text == '\r' || *text == '\n') *text = ' ';
}

static bool line_contains(const char *line, size_t length, const char *needle) {
    size_t size = strlen(needle), at;
    for (at = 0; at + size <= length; at++) if (!memcmp(line + at, needle, size)) return true;
    return false;
}

/* Lines of a regular file as awk counts records, and how many of them
 * contain needle (when given). */
static void count_lines(const char *path, const char *needle, unsigned *lines, unsigned *matching) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    *lines = 0;
    if (matching) *matching = 0;
    if (!is_file(path) || !(file = fopen(path, "rb"))) return;
    while ((length = getline(&line, &capacity, file)) >= 0) {
        (*lines)++;
        if (matching && line_contains(line, (size_t)length, needle)) (*matching)++;
    }
    free(line);
    fclose(file);
}

/* Lines of a file that are exactly text. */
static unsigned lines_equal(const char *path, const char *text) {
    FILE *file = is_file(path) ? fopen(path, "rb") : NULL;
    char *line = NULL;
    size_t capacity = 0, size = strlen(text);
    ssize_t length;
    unsigned count = 0;
    if (!file) return 0;
    while ((length = getline(&line, &capacity, file)) >= 0) {
        if (length > 0 && line[length - 1] == '\n') length--;
        if ((size_t)length == size && !memcmp(line, text, size)) count++;
    }
    free(line);
    fclose(file);
    return count;
}

/* Every entry below a directory, parents before children, in the order the
 * directory lists them (the order find prints them). Symbolic links are not
 * followed. */
typedef void walk_fn(const char *path, const char *name, const struct stat *info, void *context);
static void walk(const char *directory, int depth, bool recurse, walk_fn *visit, void *context) {
    DIR *stream = depth < WALK_DEPTH && is_directory(directory) ? opendir(directory) : NULL;
    struct dirent *entry;
    if (!stream) return;
    while ((entry = readdir(stream)) != NULL) {
        char path[STORE_PATH];
        struct stat info;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        path_child(path, sizeof(path), directory, entry->d_name);
        if (!path[0] || lstat(path, &info) != 0) continue;
        visit(path, entry->d_name, &info, context);
        if (recurse && S_ISDIR(info.st_mode)) walk(path, depth + 1, true, visit, context);
    }
    closedir(stream);
}

struct tally { const char *name, *text; unsigned count; bool directories; };

static void tally_kind(const char *path, const char *name, const struct stat *info, void *context) {
    struct tally *t = context;
    (void)path; (void)name;
    if (t->directories ? S_ISDIR(info->st_mode) : S_ISREG(info->st_mode)) t->count++;
}

static void tally_named(const char *path, const char *name, const struct stat *info, void *context) {
    struct tally *t = context;
    if (!S_ISREG(info->st_mode) || strcmp(name, t->name)) return;
    t->count += t->text ? lines_equal(path, t->text) : 1U;
}

/* Files, or directories, directly inside a directory. */
static unsigned count_children(const char *directory, bool directories) {
    struct tally t = {NULL, NULL, 0, directories};
    walk(directory, 0, false, tally_kind, &t);
    return t.count;
}

/* Regular files called name anywhere below a directory; with text, the lines
 * in them that are exactly text. */
static unsigned count_named(const char *directory, const char *name, const char *text) {
    struct tally t = {name, text, 0, false};
    walk(directory, 0, true, tally_named, &t);
    return t.count;
}

bool store_stamp(const char *path, struct store_stamp *stamp) {
    struct stat info;
    memset(stamp, 0, sizeof(*stamp));
    if (!path[0] || stat(path, &info) != 0) return false;
    stamp->device = info.st_dev; stamp->inode = info.st_ino; stamp->size = info.st_size;
    stamp->seconds = info.st_mtime;
#ifdef __APPLE__
    stamp->nanoseconds = info.st_mtimespec.tv_nsec;
#else
    stamp->nanoseconds = info.st_mtim.tv_nsec;
#endif
    return true;
}

bool store_stamp_equal(const struct store_stamp *a, const struct store_stamp *b) {
    return a->device == b->device && a->inode == b->inode && a->size == b->size &&
           a->seconds == b->seconds && a->nanoseconds == b->nanoseconds;
}

bool store_valid_id(const char *value) {
    static const char *const prefixes[] = {"project_", "head_", "instance_", "run_", "step_", "evt_", "claim_", "pack_"};
    const char *suffix = NULL;
    size_t i;
    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]) && !suffix; i++)
        if (!strncmp(value, prefixes[i], strlen(prefixes[i]))) suffix = value + strlen(prefixes[i]);
    if (!suffix || !*suffix) return false;
    return strspn(suffix, "0123456789abcdef") == strlen(suffix);
}

/* ---- cached line counts ------------------------------------------------- */

static struct store_cache_entry *cache_entry(struct store_cache *cache, const char *path) {
    size_t i;
    for (i = 0; i < cache->count; i++) if (!strcmp(cache->entries[i].path, path)) return &cache->entries[i];
    if (cache->count == cache->capacity) {
        size_t next = cache->capacity ? cache->capacity * 2U : 32U;
        void *grown = realloc(cache->entries, next * sizeof(*cache->entries));
        if (!grown) return NULL;
        cache->entries = grown; cache->capacity = next;
    }
    memset(&cache->entries[cache->count], 0, sizeof(*cache->entries));
    cache->entries[cache->count].path = strdup(path);
    return cache->entries[cache->count].path ? &cache->entries[cache->count++] : NULL;
}

/* An event log is read again only after it changed. */
static void count_events(struct store_cache *cache, const char *path, unsigned *lines, unsigned *signals) {
    struct store_stamp now, kept;
    struct store_cache_entry *entry = cache && store_stamp(path, &now) ? cache_entry(cache, path) : NULL;
    if (!entry) { count_lines(path, "\"type\":\"signal.", lines, signals); return; }
    kept = (struct store_stamp){entry->device, entry->inode, entry->size, entry->seconds, entry->nanoseconds};
    if (!store_stamp_equal(&now, &kept)) {
        count_lines(path, "\"type\":\"signal.", &entry->lines, &entry->matching);
        entry->device = now.device; entry->inode = now.inode; entry->size = now.size;
        entry->seconds = now.seconds; entry->nanoseconds = now.nanoseconds;
    }
    *lines = entry->lines; *signals = entry->matching;
}

void store_cache_free(struct store_cache *cache) {
    size_t i;
    for (i = 0; i < cache->count; i++) free(cache->entries[i].path);
    free(cache->entries);
    memset(cache, 0, sizeof(*cache));
}

/* ---- the project and its head directories -------------------------------- */

struct reader {
    const struct store_location *where;
    struct store_cache *cache;
    char project[STORE_TEXT];        /* a valid project ID, or empty */
    char project_dir[STORE_PATH], heads_dir[STORE_PATH], queue_dir[STORE_PATH];
    /* head_* directories in glob order, and the branch each one records. */
    char **names, **branches;
    size_t count;
};

struct text { char *bytes; size_t length, capacity; bool failed; };

static void text_add(struct text *t, const char *format, ...) {
    va_list arguments;
    int length;
    va_start(arguments, format);
    length = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    if (length < 0) { t->failed = true; return; }
    if (t->length + (size_t)length + 1U > t->capacity) {
        size_t next = (t->length + (size_t)length + 1U) * 2U;
        char *grown = realloc(t->bytes, next);
        if (!grown) { t->failed = true; return; }
        t->bytes = grown; t->capacity = next;
    }
    va_start(arguments, format);
    (void)vsnprintf(t->bytes + t->length, t->capacity - t->length, format, arguments);
    va_end(arguments);
    t->length += (size_t)length;
}

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool names_add(struct reader *r, size_t *capacity, const char *name) {
    if (r->count == *capacity) {
        size_t next = *capacity ? *capacity * 2U : 32U;
        char **grown = realloc(r->names, next * sizeof(*grown));
        if (!grown) return false;
        r->names = grown; *capacity = next;
    }
    r->names[r->count] = strdup(name);
    if (!r->names[r->count]) return false;
    r->count++;
    return true;
}

/* The head_* directories, in the order a shell glob lists them. */
static bool reader_names(struct reader *r) {
    DIR *stream = is_directory(r->heads_dir) ? opendir(r->heads_dir) : NULL;
    struct dirent *entry;
    size_t capacity = 0;
    bool ok = true;
    if (!stream) return true;
    while (ok && (entry = readdir(stream)) != NULL) {
        char path[STORE_PATH];
        if (strncmp(entry->d_name, "head_", 5)) continue;
        path_child(path, sizeof(path), r->heads_dir, entry->d_name);
        if (is_directory(path)) ok = names_add(r, &capacity, entry->d_name);
    }
    closedir(stream);
    if (ok && r->count) qsort(r->names, r->count, sizeof(*r->names), by_name);
    return ok;
}

/* The head directories and the branch each records, read once. */
static bool reader_list(struct reader *r) {
    size_t i;
    if (!reader_names(r)) return false;
    r->branches = calloc(r->count ? r->count : 1U, sizeof(*r->branches));
    if (!r->branches) return false;
    for (i = 0; i < r->count; i++) {
        char path[STORE_PATH], branch[STORE_TEXT];
        path_child(path, sizeof(path), r->heads_dir, r->names[i]);
        first_line_at(path, "/branch", branch, sizeof(branch));
        if (!(r->branches[i] = strdup(branch))) return false;
    }
    return true;
}

static void reader_free(struct reader *r) {
    size_t i;
    for (i = 0; i < r->count; i++) { free(r->names[i]); if (r->branches) free(r->branches[i]); }
    free(r->names); free(r->branches);
}

/* The project this repository is, and where its records are. */
static void reader_locate(struct reader *r) {
    const struct store_location *w = r->where;
    char path[STORE_PATH], id[STORE_TEXT] = "";
    if (w->common[0]) {
        path_join(path, sizeof(path), w->common, "/hydra/project-id");
        if (is_file(path)) (void)first_line(path, id, sizeof(id));
    }
    if (store_valid_id(id)) {
        text_copy(r->project, sizeof(r->project), id);
        store_path(r->project_dir, sizeof(r->project_dir), "%s/projects/%s", w->state_root, id);
    }
    path_join(r->heads_dir, sizeof(r->heads_dir), r->project_dir, "/heads");
    store_path(r->queue_dir, sizeof(r->queue_dir), "%s/state/v2/projects/%s/queue", w->home, r->project);
}

/* ---- one head ----------------------------------------------------------- */

/* Splits on runs of spaces as `read` does with IFS=' ': the first capacity
 * words, then whatever follows them without its trailing spaces. */
static size_t split_words(char *text, char **words, size_t capacity, char **rest) {
    size_t count = 0, end;
    while (*text == ' ') text++;
    while (*text && count < capacity) {
        words[count++] = text;
        while (*text && *text != ' ') text++;
        if (*text) *text++ = '\0';
        while (*text == ' ') text++;
    }
    end = strlen(text);
    while (end && text[end - 1] == ' ') text[--end] = '\0';
    *rest = text;
    return count;
}

/* The second field of a record as awk splits it, on spaces and tabs. */
static void second_field(const char *record, char *out, size_t size) {
    size_t length;
    record += strspn(record, " \t");
    record += strcspn(record, " \t");
    record += strspn(record, " \t");
    length = strcspn(record, " \t");
    (void)snprintf(out, size, "%.*s", (int)length, record);
}

/* The first directory, in glob order, that records this branch: stopped and
 * malformed records included. */
static const char *branch_directory(const struct reader *r, const char *branch) {
    size_t i;
    for (i = 0; branch[0] && i < r->count; i++) if (!strcmp(r->branches[i], branch)) return r->names[i];
    return NULL;
}

static void dash_field(const char *directory, const char *name, char *out, size_t size) {
    first_line_at(directory, name, out, size);
    if (!out[0]) text_copy(out, size, "-");
}

/* An active head's record as one line: branch session profile group
 * created-at dependencies pr. False for a stopped or incomplete record. */
static bool head_record(const char *directory, char *record, size_t size) {
    char state[STORE_TEXT], branch[STORE_TEXT], session[STORE_TEXT];
    char profile[STORE_TEXT], group[STORE_TEXT], created[STORE_TEXT], needs[STORE_TEXT], pr[STORE_TEXT];
    first_line_at(directory, "/desired-state", state, sizeof(state));
    if (strcmp(state, "running") && strcmp(state, "headless") && strcmp(state, "stopping")) return false;
    first_line_at(directory, "/branch", branch, sizeof(branch));
    first_line_at(directory, "/session", session, sizeof(session));
    if (!branch[0] || !session[0]) return false;
    dash_field(directory, "/profile", profile, sizeof(profile));
    dash_field(directory, "/group", group, sizeof(group));
    dash_field(directory, "/created-at", created, sizeof(created));
    dash_field(directory, "/dependencies", needs, sizeof(needs));
    dash_field(directory, "/pr", pr, sizeof(pr));
    (void)snprintf(record, size, "%s %s %s %s %s %s %s", branch, session, profile, group, created, needs, pr);
    return true;
}

/* Binds the row to the directory its branch selects. A directory whose name
 * is not a valid identity keeps the name and no location; one that no longer
 * records the branch is dropped. */
static void head_bind(const struct reader *r, struct store_head *h, const char *record) {
    char first[STORE_TEXT], recorded[STORE_TEXT];
    const char *directory;
    (void)snprintf(first, sizeof(first), "%.*s", (int)strcspn(record + strspn(record, " "), " "),
                   record + strspn(record, " "));
    text_copy(h->mode, sizeof(h->mode), "interactive");
    directory = branch_directory(r, first);
    if (!directory) return;
    text_copy(h->head_id, sizeof(h->head_id), directory);
    if (r->project[0] && store_valid_id(h->head_id))
        store_path(h->head_dir, sizeof(h->head_dir), "%s/projects/%s/heads/%s", r->where->state_root, r->project, h->head_id);
    if (!h->head_dir[0]) return;
    first_line_at(h->head_dir, "/branch", recorded, sizeof(recorded));
    if (strcmp(recorded, h->branch)) { h->head_id[0] = '\0'; h->head_dir[0] = '\0'; return; }
    first_line_at(h->head_dir, "/terminal-mode", recorded, sizeof(recorded));
    if (!strcmp(recorded, "headless")) text_copy(h->mode, sizeof(h->mode), "headless");
}

/* What the current instance declared and what Hydra observed of it. */
static void head_instance(struct store_head *h) {
    char path[STORE_PATH], directory[STORE_PATH], value[STORE_TEXT];
    first_line_at(h->head_dir, "/current-instance", h->instance, sizeof(h->instance));
    path_join(path, sizeof(path), h->head_dir, "/desired-state");
    if (!first_line(path, h->desired, sizeof(h->desired))) text_copy(h->desired, sizeof(h->desired), "unavailable");
    path_join(path, sizeof(path), h->head_dir, "/profile");
    if (first_line(path, value, sizeof(value))) text_copy(h->profile, sizeof(h->profile), value);
    store_path(directory, sizeof(directory), "%s/instances/%s", h->head_dir, h->instance);
    if (!is_directory(directory)) return;
    first_line_at(directory, "/declared-outcome", h->declared, sizeof(h->declared));
    path_join(path, sizeof(path), directory, "/observed-status");
    if (!first_line(path, h->observed, sizeof(h->observed))) text_copy(h->observed, sizeof(h->observed), "unavailable");
    path_join(path, sizeof(path), directory, "/observed-confidence");
    if (!first_line(path, h->confidence, sizeof(h->confidence))) text_copy(h->confidence, sizeof(h->confidence), "unavailable");
}

/* Queue entries naming the branch: *.queue files with a `branch=` line for it. */
static unsigned queue_entries(const char *directory, const char *branch) {
    DIR *stream = is_directory(directory) ? opendir(directory) : NULL;
    struct dirent *entry;
    char wanted[STORE_TEXT + 8];
    unsigned count = 0;
    if (!stream) return 0;
    (void)snprintf(wanted, sizeof(wanted), "branch=%s", branch);
    while ((entry = readdir(stream)) != NULL) {
        char path[STORE_PATH];
        size_t length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || length < 6U || strcmp(entry->d_name + length - 6U, ".queue")) continue;
        path_child(path, sizeof(path), directory, entry->d_name);
        if (lines_equal(path, wanted)) count++;
    }
    closedir(stream);
    return count;
}

static unsigned head_resources(const struct reader *r, const char *head) {
    char directory[STORE_PATH], path[STORE_PATH], value[STORE_TEXT];
    unsigned count = 0;
    store_path(directory, sizeof(directory), "%s/resources/%s", r->project_dir, head);
    if (!is_directory(directory)) return 0;
    path_join(path, sizeof(path), directory, "/ports");
    count_lines(path, NULL, &count, NULL);
    first_line_at(directory, "/compose-project", value, sizeof(value));
    if (value[0]) count++;
    first_line_at(directory, "/database", value, sizeof(value));
    if (value[0]) count++;
    return count;
}

/* Everything counted for a head: its events, messages, approvals and what it
 * holds in the project. */
static void head_counts(const struct reader *r, struct store_head *h) {
    char path[STORE_PATH];
    path_join(path, sizeof(path), h->head_dir, "/events/events.jsonl");
    count_events(r->cache, path, &h->events, &h->signals);
    path_join(path, sizeof(path), h->head_dir, "/messages/queue");
    h->messages = count_children(path, false);
    path_join(path, sizeof(path), h->head_dir, "/scopes");
    count_lines(path, NULL, &h->scopes, NULL);
    path_join(path, sizeof(path), h->head_dir, "/gates");
    h->gates = count_children(path, true);
    h->approved = count_named(path, "approved-at", NULL);
    path_join(path, sizeof(path), r->project_dir, "/claims");
    h->claims = count_named(path, "owner-head", h->head_id);
    h->resources = head_resources(r, h->head_id);
    h->queue = queue_entries(r->queue_dir, h->branch);
    first_line_at(h->head_dir, "/worktree", path, sizeof(path));
    if (is_directory(path)) text_copy(h->worktree, sizeof(h->worktree), path);
}

/* ---- agent profiles ----------------------------------------------------- */

static bool profile_named(const char *name) {
    return name[0] && strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_-") == strlen(name);
}

static bool profile_builtin(const char *name) {
    static const char *const names[] = {"none", "claude", "codex", "cursor", "agy", "opencode", "copilot", "aider", "gemini"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(name, names[i])) return true;
    return false;
}

static bool profile_file(const char *home, const char *name, const char *file) {
    char path[STORE_PATH];
    store_path(path, sizeof(path), "%s/profiles/%s/%s", home, name, file);
    return is_file(path);
}

static bool profile_exists(const char *home, const char *name) {
    if (profile_file(home, name, "adapter.json")) return false;
    if (profile_builtin(name)) return true;
    return profile_named(name) && profile_file(home, name, "executable");
}

static const char *builtin_confidence(const char *name) {
    static const char *const verified[] = {"claude", "codex", "agy", "cursor", "opencode"};
    size_t i;
    for (i = 0; i < sizeof(verified) / sizeof(verified[0]); i++) if (!strcmp(name, verified[i])) return "verified-local-help";
    return !strcmp(name, "none") ? "exact" : "launch-only";
}

/* One recorded field of a profile; "unavailable" when it has none. */
static void profile_field(const char *home, const char *name, const char *field, char *out, size_t size) {
    char path[STORE_PATH];
    text_copy(out, size, "unavailable");
    if (!profile_named(name) || profile_file(home, name, "adapter.json")) return;
    if (profile_file(home, name, "executable")) {
        if (!profile_file(home, name, field)) return;
        store_path(path, sizeof(path), "%s/profiles/%s/%s", home, name, field);
        (void)first_line(path, out, size);
    } else if (profile_builtin(name)) {
        text_copy(out, size, !strcmp(field, "adapter") ? "none" : builtin_confidence(name));
    }
}

static void head_adapter(const struct reader *r, struct store_head *h) {
    const char *home = r->where->home;
    text_copy(h->adapter_source, sizeof(h->adapter_source), "hydra capabilities --json");
    if (!strcmp(h->profile, "-") || !strcmp(h->profile, "none")) {
        text_copy(h->adapter, sizeof(h->adapter), "none");
        text_copy(h->adapter_confidence, sizeof(h->adapter_confidence), "exact");
    } else if (profile_exists(home, h->profile)) {
        profile_field(home, h->profile, "adapter", h->adapter, sizeof(h->adapter));
        profile_field(home, h->profile, "confidence", h->adapter_confidence, sizeof(h->adapter_confidence));
        if (profile_file(home, h->profile, "executable"))
            store_path(h->adapter_source, sizeof(h->adapter_source), "%s/profiles/%s", home, h->profile);
        make_safe(h->adapter); make_safe(h->adapter_confidence); make_safe(h->adapter_source);
    } else {
        text_copy(h->adapter, sizeof(h->adapter), "unavailable");
        text_copy(h->adapter_confidence, sizeof(h->adapter_confidence), "unavailable");
    }
}

/* One row from an active head's record. */
static void head_read(const struct reader *r, const char *state_source, char *record, struct store_head *h) {
    char *words[7] = {"", "", "", "", "", "", ""}, *rest = "";
    char whole[STORE_TEXT * 8];
    memset(h, 0, sizeof(*h));
    text_copy(whole, sizeof(whole), record);
    second_field(whole, h->live_key, sizeof(h->live_key));
    (void)split_words(record, words, 7, &rest);
    text_copy(h->branch, sizeof(h->branch), words[0]);
    text_copy(h->session, sizeof(h->session), words[1]);
    text_copy(h->profile, sizeof(h->profile), words[2][0] ? words[2] : "-");
    text_copy(h->group, sizeof(h->group), words[3]);
    text_copy(h->pr, sizeof(h->pr), words[6]);
    h->extra = rest[0] != '\0';
    text_copy(h->observed, sizeof(h->observed), "unavailable");
    text_copy(h->confidence, sizeof(h->confidence), "unavailable");
    text_copy(h->desired, sizeof(h->desired), "unavailable");
    text_copy(h->source, sizeof(h->source), state_source);
    head_bind(r, h, whole);
    if (h->head_id[0]) {
        head_instance(h);
        head_counts(r, h);
        text_copy(h->source, sizeof(h->source), h->head_dir);
        make_safe(h->source);
    }
    head_adapter(r, h);
}

/* ---- recovery findings the store alone decides --------------------------- */

static bool mode_file_valid(const char *path) {
    return !is_file(path) || lines_equal(path, "interactive") || lines_equal(path, "headless");
}

static void recorded_mode(const char *path, char *out, size_t size) {
    if (has_bytes(path)) (void)first_line(path, out, size);
    else text_copy(out, size, "interactive");
}

/* A session of "-" belongs to headless records, and only to them. */
static bool session_fits(const char *mode, const char *session) {
    if (!strcmp(mode, "headless")) return !strcmp(session, "-");
    if (!strcmp(mode, "interactive")) return strcmp(session, "-") != 0;
    return true;
}

static bool head_fields_valid(const char *directory, const char *id, const char *branch, const char *mode) {
    char path[STORE_PATH], value[STORE_TEXT], session[STORE_TEXT];
    first_line_at(directory, "/head-id", value, sizeof(value));
    if (!store_valid_id(id) || strcmp(value, id)) return false;
    first_line_at(directory, "/session", session, sizeof(session));
    if (!branch[0] || !session[0]) return false;
    path_join(path, sizeof(path), directory, "/terminal-mode");
    return mode_file_valid(path) && session_fits(mode, session);
}

static bool instance_fields_valid(const char *directory, const char *instance, const char *mode) {
    char instance_dir[STORE_PATH], path[STORE_PATH], value[STORE_TEXT], instance_mode[STORE_TEXT];
    store_path(instance_dir, sizeof(instance_dir), "%s/instances/%s", directory, instance);
    if (!store_valid_id(instance) || !is_directory(instance_dir)) return false;
    first_line_at(instance_dir, "/instance-id", value, sizeof(value));
    path_join(path, sizeof(path), instance_dir, "/terminal-mode");
    if (strcmp(value, instance) || !mode_file_valid(path)) return false;
    recorded_mode(path, instance_mode, sizeof(instance_mode));
    first_line_at(instance_dir, "/session", value, sizeof(value));
    return !strcmp(instance_mode, mode) && session_fits(instance_mode, value);
}

/* Head directories whose records do not form a valid head. */
static void invalid_heads(const struct reader *r, struct text *out) {
    size_t i;
    for (i = 0; r->project_dir[0] && i < r->count; i++) {
        char directory[STORE_PATH], path[STORE_PATH], branch[STORE_TEXT], instance[STORE_TEXT], mode[STORE_TEXT];
        path_child(directory, sizeof(directory), r->heads_dir, r->names[i]);
        first_line_at(directory, "/branch", branch, sizeof(branch));
        first_line_at(directory, "/current-instance", instance, sizeof(instance));
        path_join(path, sizeof(path), directory, "/terminal-mode");
        recorded_mode(path, mode, sizeof(mode));
        if (head_fields_valid(directory, r->names[i], branch, mode) && instance_fields_valid(directory, instance, mode)) continue;
        if (!branch[0]) text_copy(branch, sizeof(branch), r->names[i]);
        make_safe(branch); make_safe(directory);
        text_add(out, "R\tmalformed-state\t%s\t%s\texact\thydra state verify\n", branch, directory);
    }
}

static bool process_exists(const char *digits) {
    char *end;
    long value;
    errno = 0;
    value = strtol(digits, &end, 10);
    if (errno || *end || value > INT_MAX) return false;
    return kill((pid_t)value, 0) == 0;
}

/* A lock whose owner, a process on this machine, is gone. */
static bool lock_stale(const struct store_location *where, const char *directory) {
    char pid[STORE_TEXT], host[STORE_TEXT];
    first_line_at(directory, "/pid", pid, sizeof(pid));
    first_line_at(directory, "/host", host, sizeof(host));
    if (!pid[0] || strspn(pid, "0123456789") != strlen(pid) || strcmp(host, where->host)) return false;
    return !process_exists(pid);
}

struct lock_walk { const struct store_location *where; struct text *out; };

static void lock_visit(const char *path, const char *name, const struct stat *info, void *context) {
    struct lock_walk *w = context;
    char safe[STORE_PATH];
    size_t length = strlen(name);
    if (!S_ISDIR(info->st_mode) || length < 5U || strcmp(name + length - 5U, ".lock") || !lock_stale(w->where, path)) return;
    text_copy(safe, sizeof(safe), path);
    make_safe(safe);
    text_add(w->out, "R\tstale-lock\t%s\t%s\texact\thydra doctor\n", name, safe);
}

static void stale_locks(const struct store_location *where, struct text *out) {
    struct lock_walk w = {where, out};
    char directory[STORE_PATH];
    path_join(directory, sizeof(directory), where->home, "/locks");
    walk(directory, 0, true, lock_visit, &w);
}

/* The directory part of a path, as dirname prints it. */
static void parent_of(const char *path, char *out, size_t size) {
    size_t length = strlen(path);
    while (length > 1U && path[length - 1U] == '/') length--;
    while (length && path[length - 1U] != '/') length--;
    while (length > 1U && path[length - 1U] == '/') length--;
    if (!length) { text_copy(out, size, "."); return; }
    (void)snprintf(out, size, "%.*s", (int)length, path);
}

struct leftover_walk { const struct reader *reader; bool found; };

static void leftover_visit(const char *path, const char *name, const struct stat *info, void *context) {
    struct leftover_walk *w = context;
    char record[STORE_PATH];
    (void)info;
    if (strncmp(name, "head_", 5) || !store_valid_id(name) || !is_directory(path)) return;
    path_child(record, sizeof(record), w->reader->heads_dir, name);
    if (!is_directory(record)) w->found = true;
}

/* Whether a head_<id> directory under the project's worktree root has no head
 * record. Only such a directory can be a leftover worktree. */
static bool leftovers_possible(const struct reader *r) {
    struct leftover_walk w = {r, false};
    char root[STORE_PATH] = "", parent[STORE_PATH];
    if (!r->project[0] || !is_directory(r->project_dir) || !r->where->common[0]) return false;
    first_line_at(r->where->common, "/hydra/worktree-root", root, sizeof(root));
    if (!root[0] && r->where->toplevel[0]) {
        parent_of(r->where->toplevel, parent, sizeof(parent));
        store_path(root, sizeof(root), "%s/.hydra-worktrees/%s", parent, r->project);
    }
    walk(root, 0, false, leftover_visit, &w);
    return w.found;
}

/* ---- workflow runs ------------------------------------------------------- */

static void mix_bytes(unsigned long long *hash, const void *bytes, size_t length) {
    const unsigned char *at = bytes;
    while (length--) { *hash ^= *at++; *hash *= 1099511628211ULL; }
}

/* Folds a file's or directory's change stamp into a hash; a missing one
 * counts as itself, so its appearance is a change too. */
static void mix_stamp(unsigned long long *hash, const char *directory, const char *name) {
    struct store_stamp stamp;
    char path[STORE_PATH];
    long long fields[5];
    path_join(path, sizeof(path), directory, name);
    (void)store_stamp(path, &stamp);
    fields[0] = (long long)stamp.device; fields[1] = (long long)stamp.inode; fields[2] = (long long)stamp.size;
    fields[3] = (long long)stamp.seconds; fields[4] = (long long)stamp.nanoseconds;
    mix_bytes(hash, name, strlen(name) + 1U);
    mix_bytes(hash, fields, sizeof(fields));
}

static void mix_child(const char *path, const char *name, const struct stat *info, void *context) {
    (void)info;
    mix_bytes(context, name, strlen(name) + 1U);
    mix_stamp(context, path, "");
}

static bool run_ended(const char *directory) {
    char state[STORE_TEXT];
    first_line_at(directory, "/state", state, sizeof(state));
    return !strcmp(state, "finished") || !strcmp(state, "failed") || !strcmp(state, "cancelled");
}

static void run_visit(const char *path, const char *name, const struct stat *info, void *context) {
    struct store_runs *runs = context;
    char directory[STORE_PATH];
    (void)info;
    if (strncmp(name, "run_", 4)) return;
    runs->count++;
    if (!run_ended(path)) runs->settled = false;
    mix_bytes(&runs->graphs, name, strlen(name) + 1U);
    mix_stamp(&runs->graphs, path, "/graph.tsv");
    mix_bytes(&runs->records, name, strlen(name) + 1U);
    mix_stamp(&runs->records, path, "");
    mix_stamp(&runs->records, path, "/state");
    mix_stamp(&runs->records, path, "/graph.tsv");
    mix_stamp(&runs->records, path, "/steps");
    mix_stamp(&runs->records, path, "/retirement");
    path_join(directory, sizeof(directory), path, "/steps");
    walk(directory, 0, false, mix_child, &runs->records);
    path_join(directory, sizeof(directory), path, "/retirement");
    walk(directory, 0, false, mix_child, &runs->records);
}

void store_runs(const struct store_location *where, struct store_runs *runs) {
    struct reader r;
    char directory[STORE_PATH];
    memset(&r, 0, sizeof(r));
    memset(runs, 0, sizeof(*runs));
    r.where = where;
    reader_locate(&r);
    text_copy(runs->project, sizeof(runs->project), r.project);
    runs->settled = true;
    runs->graphs = runs->records = 1469598103934665603ULL;
    if (!r.project[0]) return;
    path_join(directory, sizeof(directory), r.project_dir, "/workflows/runs");
    walk(directory, 0, false, run_visit, runs);
}

/* ---- the snapshot ------------------------------------------------------- */

static void read_notifications(const struct reader *r, struct store_snapshot *s) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    if (!r->where->common[0]) return;
    path_join(s->notification_source, sizeof(s->notification_source), r->where->common, "/hydra/notifications");
    file = has_bytes(s->notification_source) ? fopen(s->notification_source, "rb") : NULL;
    while (file && getline(&line, &capacity, file) >= 0) {
        char *save = NULL, *word;
        size_t fields = 0;
        for (word = strtok_r(line, " \t\n", &save); word; word = strtok_r(NULL, " \t\n", &save)) fields++;
        if (fields == 3U) s->notifications++;
    }
    free(line);
    if (file) fclose(file);
    make_safe(s->notification_source);
}

static bool heads_add(struct store_snapshot *s, size_t *capacity) {
    if (s->head_count < *capacity) return true;
    {
        size_t next = *capacity ? *capacity * 2U : 16U;
        struct store_head *grown = realloc(s->heads, next * sizeof(*grown));
        if (!grown) return false;
        s->heads = grown; *capacity = next;
    }
    return true;
}

static bool read_heads(const struct reader *r, struct store_snapshot *s) {
    size_t capacity = 0, i;
    for (i = 0; i < r->count; i++) {
        char directory[STORE_PATH], record[STORE_TEXT * 8];
        path_child(directory, sizeof(directory), r->heads_dir, r->names[i]);
        if (!head_record(directory, record, sizeof(record))) continue;
        if (!heads_add(s, &capacity)) return false;
        head_read(r, s->state_source, record, &s->heads[s->head_count]);
        /* A record of spaces alone names no branch and is not a row. */
        if (!s->heads[s->head_count].branch[0]) continue;
        if (strcmp(s->heads[s->head_count].mode, "headless")) s->interactive = true;
        s->head_count++;
    }
    return true;
}

bool store_read(const struct store_location *where, struct store_cache *cache, struct store_snapshot *snapshot) {
    struct reader r;
    struct text findings = {NULL, 0, 0, false};
    bool ok;
    memset(&r, 0, sizeof(r));
    memset(snapshot, 0, sizeof(*snapshot));
    r.where = where; r.cache = cache;
    reader_locate(&r);
    text_copy(snapshot->state_source, sizeof(snapshot->state_source), r.project_dir);
    make_safe(snapshot->state_source);
    read_notifications(&r, snapshot);
    ok = reader_list(&r) && read_heads(&r, snapshot);
    if (ok) {
        invalid_heads(&r, &findings);
        stale_locks(where, &findings);
        snapshot->leftovers = leftovers_possible(&r);
        ok = !findings.failed;
    }
    snapshot->findings = findings.bytes; snapshot->findings_length = findings.length;
    reader_free(&r);
    if (!ok) store_snapshot_free(snapshot);
    return ok;
}

void store_snapshot_free(struct store_snapshot *snapshot) {
    free(snapshot->heads); free(snapshot->findings);
    memset(snapshot, 0, sizeof(*snapshot));
}

/* ---- rows --------------------------------------------------------------- */

static bool session_live(const char *sessions, const char *name) {
    size_t length = strlen(name);
    while (length && *sessions) {
        size_t line = strcspn(sessions, "\n");
        if (line == length && !memcmp(sessions, name, length)) return true;
        sessions += line + (sessions[line] == '\n');
    }
    return false;
}

static void write_head(const struct store_snapshot *s, const struct store_head *h, const char *sessions,
                       store_diff_fn *diff, void *context, FILE *out) {
    const char *status = session_live(sessions, h->live_key) ? "active" : "dead", *liveness = "stopped";
    unsigned changed = 0;
    if (!strcmp(h->mode, "headless")) { status = "active"; liveness = "unavailable"; }
    else if (!strcmp(status, "active")) liveness = "live";
    if (!h->worktree[0] || !diff || !diff(context, h->worktree, &changed)) changed = 0;
    fprintf(out, "H\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%s\t%s\t%s\t%s\t%u\t%s\t%s\n",
            h->branch, h->session, h->profile, h->group, h->pr, status, liveness,
            h->declared, h->observed, h->confidence, h->instance,
            h->events, h->signals, h->messages, h->claims, h->scopes, h->queue, h->resources, changed, h->gates, h->approved,
            h->desired, h->source, h->head_id, h->adapter, h->adapter_confidence, h->adapter_source,
            s->notifications, s->notification_source, h->mode);
    if (!strcmp(status, "dead")) fprintf(out, "R\tdead-session\t%s\t%s\texact\thydra doctor\n", h->branch, s->state_source);
    if (h->extra) fprintf(out, "R\tmalformed-state\t%s\t%s\texact\thydra state verify\n", h->branch, s->state_source);
    if (!strcmp(h->desired, "stopping") && h->head_dir[0]) {
        fprintf(out, "R\tteardown-failure\t%s\t%s/desired-state\texact\thydra worktree doctor status\n", h->branch, h->source);
        fprintf(out, "R\tinterrupted-lifecycle\t%s\t%s/desired-state\texact\thydra lifecycle %s\n", h->branch, h->source, h->branch);
    }
}

bool store_write(const struct store_snapshot *snapshot, const char *sessions, store_diff_fn *diff, void *context,
                 const char *extra_rows, FILE *out) {
    size_t i;
    fputs("HYDRA_TUI\t2\n", out);
    for (i = 0; i < snapshot->head_count; i++) write_head(snapshot, &snapshot->heads[i], sessions ? sessions : "", diff, context, out);
    if (snapshot->findings_length) (void)fwrite(snapshot->findings, 1, snapshot->findings_length, out);
    if (extra_rows) fputs(extra_rows, out);
    return !ferror(out) && fflush(out) == 0;
}
