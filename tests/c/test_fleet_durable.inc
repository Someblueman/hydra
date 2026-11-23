/* A state write that cannot be completed publishes nothing: the call fails,
 * the previous value is still there and no temporary file is left behind.
 * The failure is a file size limit of zero with SIGXFSZ ignored, so every
 * write of at least one byte to a regular file returns EFBIG, as a full
 * disk returns ENOSPC. Nothing is asserted or printed while the limit is in
 * force: the test's own output may be a regular file. */
struct durable_limit { struct rlimit saved; void (*previous)(int); };
static void durable_limit_on(struct durable_limit *limit) {
    struct rlimit none;
    assert(!getrlimit(RLIMIT_FSIZE, &limit->saved));
    none = limit->saved; none.rlim_cur = 0;
    limit->previous = signal(SIGXFSZ, SIG_IGN);
    assert(limit->previous != SIG_ERR && !setrlimit(RLIMIT_FSIZE, &none));
}
static void durable_limit_off(const struct durable_limit *limit) {
    if (setrlimit(RLIMIT_FSIZE, &limit->saved)) abort();
    (void)signal(SIGXFSZ, limit->previous);
}
static void durable_expect(const char *path, const char *expected) {
    char *text = f_read(path, 100);
    if (!text || strcmp(text, expected)) fprintf(stderr, "durable %s: \"%s\" (want \"%s\")\n", path, text ? text : "(unreadable)", expected);
    assert(text && !strcmp(text, expected));
    free(text);
}
static size_t durable_entries(const char *dir) {
    DIR *stream = opendir(dir);
    size_t count = 0;
    assert(stream);
    while (readdir(stream)) count++;
    closedir(stream);
    return count;
}
struct durable_writes_files { char dir[F_PATH], scalar[F_PATH], record[F_PATH], fresh[F_PATH], nested[F_PATH]; };
static void durable_writes_prepare(const char *home, struct durable_writes_files *w) {
    assert(!f_path(w->dir, sizeof(w->dir), home, "durable") && !mkdir(w->dir, 0700));
    assert(!f_path(w->scalar, sizeof(w->scalar), w->dir, "state") && !f_path(w->record, sizeof(w->record), w->dir, "record.json"));
    assert(!f_path(w->fresh, sizeof(w->fresh), w->dir, "fresh.json") && !f_path(w->nested, sizeof(w->nested), w->dir, "steps/build/attempts"));
    assert(!wdr_scalar(w->scalar, "running") && !wdr_scalar(w->nested, "1"));
    assert(!f_write(w->record, "{\"a\":1}", 7, true));
    durable_expect(w->scalar, "running\n"); durable_expect(w->nested, "1\n");
}
static void durable_writes_refused(const struct durable_writes_files *w, size_t before) {
    struct durable_limit limit;
    int scalar_status, replace_status, create_status;
    durable_limit_on(&limit);
    scalar_status = wdr_scalar(w->scalar, "succeeded");
    replace_status = f_write(w->record, "{\"a\":2}", 7, true);
    create_status = f_write(w->fresh, "{}", 2, false);
    durable_limit_off(&limit);

    if (!scalar_status || !replace_status || !create_status)
        fprintf(stderr, "durable: a failed write reported success (scalar %d, replace %d, create %d)\n",
                scalar_status, replace_status, create_status);
    assert(scalar_status && replace_status && create_status);
    durable_expect(w->scalar, "running\n"); durable_expect(w->record, "{\"a\":1}");
    assert(access(w->fresh, F_OK) && errno == ENOENT);
    assert(durable_entries(w->dir) == before);
}
static void durable_writes(const char *home) {
    struct durable_writes_files w;
    size_t before;
    durable_writes_prepare(home, &w);
    before = durable_entries(w.dir);
    durable_writes_refused(&w, before);

    /* The same writes succeed once the limit is lifted. */
    assert(!wdr_scalar(w.scalar, "succeeded") && !wdr_scalar_long(w.nested, 2));
    durable_expect(w.scalar, "succeeded\n"); durable_expect(w.nested, "2\n");
    assert(durable_entries(w.dir) == before);
    assert(!f_remove_tree(w.dir));
}
struct durable_helper_dir { char dir[F_PATH], path[F_PATH]; int fd; size_t before; };
static void durable_helper_write(struct durable_helper_dir *h) {
    struct stat st;
    assert(!f_write_at(h->fd, "record", "one", 3, false));
    assert(!f_path(h->path, sizeof(h->path), h->dir, "record") && !stat(h->path, &st) && (st.st_mode & 0777) == 0600 && st.st_nlink == 1);
    durable_expect(h->path, "one");
    h->before = durable_entries(h->dir);
    errno = 0;
    assert(f_write_at(h->fd, "record", "two", 3, false) && errno == EEXIST);
    durable_expect(h->path, "one");
    assert(!f_write_at(h->fd, "record", "two", 3, true));
    durable_expect(h->path, "two");
    assert(!f_write_at(h->fd, "record", "", 0, true) && !stat(h->path, &st) && st.st_size == 0);
    assert(durable_entries(h->dir) == h->before);
}
static void durable_helper_halves(const struct durable_helper_dir *h) {
    char temp[F_TEMP_NAME];
    assert(!f_stage_at(h->fd, "record", "three", 5, temp) && temp[0] && durable_entries(h->dir) == h->before + 1);
    durable_expect(h->path, "");
    assert(!f_publish_at(h->fd, temp, "record", true));
    durable_expect(h->path, "three");
    assert(!f_stage_at(h->fd, "record", "four", 4, temp));
    assert(f_publish_at(h->fd, temp, "record", false) && durable_entries(h->dir) == h->before);
    durable_expect(h->path, "three");
}
static void durable_helper_refused(const struct durable_helper_dir *h) {
    struct durable_limit limit; int status;
    durable_limit_on(&limit);
    status = f_write_at(h->fd, "record", "five", 4, true);
    durable_limit_off(&limit);
    assert(status && durable_entries(h->dir) == h->before);
    durable_expect(h->path, "three");
}
/* A directory that is not there fails before anything is staged; a bare
 * name is written in the working directory. */
static void durable_helper_paths(struct durable_helper_dir *h) {
    char missing[F_PATH];
    assert(!f_path(missing, sizeof(missing), h->dir, "absent/record") && f_write(missing, "x", 1, true));
    assert(!chdir(h->dir) && !f_write("relative", "r", 1, false));
    assert(!f_path(h->path, sizeof(h->path), h->dir, "relative"));
    durable_expect(h->path, "r");
    assert(!chdir("/"));
}
/* The helper every record writer goes through, by open directory: a new
 * file is private, an existing one is replaced only on request (EEXIST
 * otherwise) and neither leaves a temporary file; the two halves publish
 * what was staged, and a publication that fails removes it. */
static void durable_helper(const char *home) {
    struct durable_helper_dir h;
    assert(!f_path(h.dir, sizeof(h.dir), home, "durable-at") && !mkdir(h.dir, 0700));
    assert((h.fd = open(h.dir, O_RDONLY | O_DIRECTORY)) >= 0);
    durable_helper_write(&h);
    durable_helper_halves(&h);
    durable_helper_refused(&h);
    durable_helper_paths(&h);
    close(h.fd);
    assert(!f_remove_tree(h.dir));
}
/* An append that stops part way (the file size limit sits four bytes past
 * the log) is cut off again: the log holds whole lines only. */
static void durable_append(const char *home) {
    char path[F_PATH]; struct rlimit saved, limited; void (*previous)(int); int fd, torn, refused;
    assert(!f_path(path, sizeof(path), home, "durable-log") && (fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600)) >= 0);
    assert(!f_append(fd, "first\n", 6, true));
    assert(!getrlimit(RLIMIT_FSIZE, &saved));
    limited = saved; limited.rlim_cur = 10;
    previous = signal(SIGXFSZ, SIG_IGN);
    assert(previous != SIG_ERR && !setrlimit(RLIMIT_FSIZE, &limited));
    torn = f_append(fd, "second-line\n", 12, false);
    refused = f_append(fd, "second-line\n", 12, true);
    if (setrlimit(RLIMIT_FSIZE, &saved)) abort();
    (void)signal(SIGXFSZ, previous);
    assert(torn == -1 && refused == -1);
    durable_expect(path, "first\n");
    assert(!f_append(fd, "second-line\n", 12, true) && !f_append(fd, "", 0, false));
    durable_expect(path, "first\nsecond-line\n");
    assert(!close(fd) && !unlink(path));
}
static void durable_alias_refused(struct f_remote *remote, struct f_remote *fresh, const char *dir, size_t before) {
    struct f_remote loaded; struct durable_limit limit; int saved, enrolled;
    durable_limit_on(&limit);
    saved = f_remote_save(remote);
    enrolled = f_remote_enrolled(fresh, true);
    durable_limit_off(&limit);

    assert(saved && enrolled && durable_entries(dir) == before);
    assert(!f_remote_load("alias-a", &loaded) && !strcmp(loaded.target, "host-a"));
    assert(f_remote_load("alias-b", &loaded));
}
static void durable_alias_published(struct f_remote *remote, struct f_remote *fresh, const char *dir, size_t before) {
    struct f_remote loaded;
    assert(!f_remote_enrolled(fresh, true) && !f_remote_enrolled(fresh, true));
    assert(!f_remote_load("alias-b", &loaded) && !strcmp(loaded.target, "host-a"));
    assert(!f_copy(fresh->target, sizeof(fresh->target), "host-c") && f_remote_enrolled(fresh, true));
    assert(!f_remote_save(remote) && !f_remote_load("alias-a", &loaded) && !strcmp(loaded.target, "host-b"));
    assert(durable_entries(dir) == before + 1);
}
/* The alias record: hydra remote add replaces it, enrollment publishes it
 * once and never over another record. */
static void durable_remote_alias(const char *home) {
    char dir[F_PATH]; struct f_remote remote, fresh;
    size_t before;
    memset(&remote, 0, sizeof(remote));
    assert(!f_copy(remote.name, sizeof(remote.name), "alias-a") && !f_copy(remote.target, sizeof(remote.target), "host-a") &&
           !f_copy(remote.hydra, sizeof(remote.hydra), "hydra"));
    assert(!f_remote_save(&remote) && !f_path(dir, sizeof(dir), home, "fleet/remotes"));
    before = durable_entries(dir);
    fresh = remote;
    assert(!f_copy(remote.target, sizeof(remote.target), "host-b") && !f_copy(fresh.name, sizeof(fresh.name), "alias-b"));
    durable_alias_refused(&remote, &fresh, dir, before);
    durable_alias_published(&remote, &fresh, dir, before);
}
struct durable_enroll {
    char dir[F_PATH], progress[F_PATH], marker[F_PATH], command[F_PATH + 16];
    char *argv[4];
    json_object *first, *second, *request;
};
static void durable_enroll_prepare(const char *home, struct durable_enroll *e) {
    e->first = f_parse("{\"schema_version\":1,\"hosts\":[]}");
    e->second = f_parse("{\"schema_version\":1,\"hosts\":[1]}");
    e->request = f_parse("{\"protocol\":1,\"action\":\"init\",\"project\":\"/srv/p\",\"args\":[],\"enrollment_operation_id\":"
        "\"0000000000000000000000000000000000000000000000000000000000000000:alias\"}");
    assert(e->first && e->second && e->request);
    assert(!f_path(e->dir, sizeof(e->dir), home, "durable-enroll") && !mkdir(e->dir, 0700));
    assert(!f_path(e->progress, sizeof(e->progress), e->dir, "progress.json") && !f_path(e->marker, sizeof(e->marker), e->dir, "ran"));
    assert(snprintf(e->command, sizeof(e->command), ": > '%s'", e->marker) < (int)sizeof(e->command));
    e->argv[0] = (char *)"sh"; e->argv[1] = (char *)"-c"; e->argv[2] = e->command; e->argv[3] = NULL;
    assert(!enrollment_write(e->progress, e->first));
}
static json_object *durable_enroll_refused(struct durable_enroll *e) {
    struct durable_limit limit; json_object *refused; int written; size_t before = durable_entries(e->dir);
    durable_limit_on(&limit);
    written = enrollment_write(e->progress, e->second);
    refused = enrollment_apply_request(e->request, e->argv, 5);
    durable_limit_off(&limit);

    assert(written && durable_entries(e->dir) == before);
    durable_expect(e->progress, "{\"schema_version\":1,\"hosts\":[]}");
    assert(!json_object_get_boolean(f_field(refused, "ok")) && !strcmp(f_string(f_field(refused, "error"), "code"), "outcome_unknown"));
    assert(access(e->marker, F_OK) && errno == ENOENT);
    return refused;
}
static void durable_enroll_claimed(const char *home, struct durable_enroll *e) {
    char ops[F_PATH]; json_object *done, *again;
    assert(!f_path(ops, sizeof(ops), home, "fleet/enrollment-ops") && durable_entries(ops) == 2);
    done = enrollment_apply_request(e->request, e->argv, 5);
    assert(done && !access(e->marker, F_OK) && durable_entries(ops) == 3);
    assert(!unlink(e->marker));
    again = enrollment_apply_request(e->request, e->argv, 5);
    assert(json_object_equal(done, again) && access(e->marker, F_OK));
    assert(!enrollment_write(e->progress, e->second));
    durable_expect(e->progress, "{\"schema_version\":1,\"hosts\":[1]}");
    json_object_put(done); json_object_put(again);
}
/* Enrollment's records: the reviewed intent and apply progress on the
 * operator's side (enrollment_write), and on the receiving host the claim
 * that an operation ID is taken. A claim that cannot be written runs
 * nothing: the mutation would otherwise be repeatable. */
static void durable_enrollment(const char *home) {
    struct durable_enroll e; json_object *refused;
    durable_enroll_prepare(home, &e);
    refused = durable_enroll_refused(&e);
    durable_enroll_claimed(home, &e);
    json_object_put(refused);
    json_object_put(e.first); json_object_put(e.second); json_object_put(e.request);
}
struct durable_task { char dir[F_PATH], state[F_PATH], id[70]; json_object *accepted, *running, *placement; size_t before; };
static void durable_task_prepare(const char *home, struct durable_task *t) {
    t->accepted = f_parse("{\"state\":\"accepted\"}"); t->running = f_parse("{\"state\":\"running\"}");
    t->placement = f_parse("{\"schema_version\":1,\"submission_key\":\"durable-key\",\"generation\":1,\"assigned_host\":\"alpha\",\"generations\":[]}");
    assert(t->accepted && t->running && t->placement && !placement_task_id("durable-key", t->id));
    f_string_add(t->placement, "task_id", t->id);
    assert(!f_path(t->dir, sizeof(t->dir), home, "durable-task") && !mkdir(t->dir, 0700) && !f_path(t->state, sizeof(t->state), t->dir, "state.json"));
    assert(!task_write_json(t->dir, "state.json", t->accepted, false) && !placement_record_write(t->placement));
    t->before = durable_entries(t->dir);
    json_object_object_add(t->placement, "generation", json_object_new_int(2)); f_string_add(t->placement, "assigned_host", "bravo");
}
static void durable_task_refused(const struct durable_task *t) {
    char host[128]; json_object *read; struct durable_limit limit; int replaced, created, text, placed;
    durable_limit_on(&limit);
    replaced = task_write_json(t->dir, "state.json", t->running, true);
    created = task_write_json(t->dir, "cancel.json", t->running, false);
    text = task_write_file(t->dir, "tasks-sha256", "abc", 3, false);
    placed = placement_record_write(t->placement);
    durable_limit_off(&limit);

    assert(replaced && created && text && placed && durable_entries(t->dir) == t->before);
    durable_expect(t->state, "{\"state\":\"accepted\"}");
    read = placement_record_read(t->id);
    assert(read && placement_generation(t->id, host) == 1 && !strcmp(host, "alpha"));
    json_object_put(read);
}
static void durable_task_linked(const char *home, const struct durable_task *t) {
    char link[F_PATH];
    assert(!f_path(link, sizeof(link), home, "durable-task-link") && !symlink(t->dir, link));
    assert(task_write_json(link, "linked.json", t->running, false) && durable_entries(t->dir) == t->before);
    assert(!unlink(link));
}
static void durable_task_retried(const struct durable_task *t) {
    char host[128];
    assert(task_write_json(t->dir, "state.json", t->running, false));
    assert(!task_write_json(t->dir, "state.json", t->running, true) && !task_write_file(t->dir, "tasks-sha256", "abc", 3, false));
    durable_expect(t->state, "{\"state\":\"running\"}");
    assert(!placement_record_write(t->placement) && placement_generation(t->id, host) == 2 && !strcmp(host, "bravo"));
    assert(durable_entries(t->dir) == t->before + 1);
}
/* Records in a task directory (task_write_json and task_write_file: task,
 * retention, workflow-task and plan-repair state) and the placement record
 * built on them, whose stored digest must still verify after a failed
 * replacement. The directory is never reached through a symbolic link. */
static void durable_task_records(const char *home) {
    struct durable_task t;
    durable_task_prepare(home, &t);
    durable_task_refused(&t);
    durable_task_linked(home, &t);
    durable_task_retried(&t);
    json_object_put(t.accepted); json_object_put(t.running); json_object_put(t.placement);
}
/* A collected result file appears under its name whole or not at all: a
 * partial file would be taken as already collected by the next attempt and
 * then fail its digest for good. An existing file is never rewritten. */
static void durable_collection(const char *home) {
    char root[F_PATH], file[F_PATH], dir[F_PATH]; struct durable_limit limit; struct stat st; int status;
    assert(!f_path(root, sizeof(root), home, "durable-collection") && !mkdir(root, 0700));
    assert(!f_path(file, sizeof(file), root, "artifacts/out.bin") && !f_path(dir, sizeof(dir), root, "artifacts"));
    durable_limit_on(&limit);
    status = task_collection_file(root, "artifacts/out.bin", "6162");
    durable_limit_off(&limit);
    assert(status && lstat(file, &st) && errno == ENOENT && durable_entries(dir) == 2);
    assert(!task_collection_file(root, "artifacts/out.bin", "6162"));
    durable_expect(file, "ab");
    assert(!stat(file, &st) && (st.st_mode & 0777) == 0600 && durable_entries(dir) == 3);
    assert(!task_collection_file(root, "artifacts/out.bin", "6364"));
    durable_expect(file, "ab");
    assert(!task_collection_file(root, "artifacts/empty", "") && durable_entries(dir) == 4);
    assert(!f_remove_tree(root));
}
/* Records created once and never replaced: a revocation, and the marker
 * that drains a host from placement. A failed write must not leave either
 * half in force. */
static void durable_markers(const char *home) {
    char drained[F_PATH], private[F_PATH]; char *drain[] = {(char *)"drain", (char *)"alias-a"};
    struct durable_limit limit; json_object *revoked, *marked;
    /* A home of its own: other cases count the revocations in the shared one. */
    assert(!f_path(private, sizeof(private), home, "durable-home") && !mkdir(private, 0700));
    f_home = private;
    durable_limit_on(&limit);
    revoked = revocation_record("package", "0000000000000000000000000000000000000000000000000000000000000000", "durable", NULL);
    marked = placement_cli(2, drain);
    durable_limit_off(&limit);
    assert(!json_object_get_boolean(f_field(revoked, "ok")) && !strcmp(f_string(f_field(revoked, "error"), "code"), "state_unavailable"));
    assert(!revocation_blocked("package", "0000000000000000000000000000000000000000000000000000000000000000"));
    assert(!json_object_get_boolean(f_field(marked, "ok")) && !strcmp(f_string(f_field(marked, "error"), "code"), "io_failed"));
    assert(!placement_drained("alias-a"));
    assert(!f_path(drained, sizeof(drained), f_home, "fleet/placement-drained") && durable_entries(drained) == 2);
    json_object_put(revoked); json_object_put(marked);
    revoked = revocation_record("package", "0000000000000000000000000000000000000000000000000000000000000000", "durable", NULL);
    marked = placement_cli(2, drain);
    assert(json_object_get_boolean(f_field(revoked, "ok")) && json_object_get_boolean(f_field(marked, "ok")));
    assert(revocation_blocked("package", "0000000000000000000000000000000000000000000000000000000000000000") && placement_drained("alias-a"));
    json_object_put(revoked); json_object_put(marked);
    f_home = home;
}
