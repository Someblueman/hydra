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
static void durable_writes(const char *home) {
    char dir[F_PATH], scalar[F_PATH], record[F_PATH], fresh[F_PATH], nested[F_PATH];
    struct durable_limit limit;
    int scalar_status, replace_status, create_status;
    size_t before;
    assert(!f_path(dir, sizeof(dir), home, "durable") && !mkdir(dir, 0700));
    assert(!f_path(scalar, sizeof(scalar), dir, "state") && !f_path(record, sizeof(record), dir, "record.json"));
    assert(!f_path(fresh, sizeof(fresh), dir, "fresh.json") && !f_path(nested, sizeof(nested), dir, "steps/build/attempts"));
    assert(!wdr_scalar(scalar, "running") && !wdr_scalar(nested, "1"));
    assert(!f_write(record, "{\"a\":1}", 7, true));
    durable_expect(scalar, "running\n"); durable_expect(nested, "1\n");
    before = durable_entries(dir);

    durable_limit_on(&limit);
    scalar_status = wdr_scalar(scalar, "succeeded");
    replace_status = f_write(record, "{\"a\":2}", 7, true);
    create_status = f_write(fresh, "{}", 2, false);
    durable_limit_off(&limit);

    if (!scalar_status || !replace_status || !create_status)
        fprintf(stderr, "durable: a failed write reported success (scalar %d, replace %d, create %d)\n",
                scalar_status, replace_status, create_status);
    assert(scalar_status && replace_status && create_status);
    durable_expect(scalar, "running\n"); durable_expect(record, "{\"a\":1}");
    assert(access(fresh, F_OK) && errno == ENOENT);
    assert(durable_entries(dir) == before);

    /* The same writes succeed once the limit is lifted. */
    assert(!wdr_scalar(scalar, "succeeded") && !wdr_scalar_long(nested, 2));
    durable_expect(scalar, "succeeded\n"); durable_expect(nested, "2\n");
    assert(durable_entries(dir) == before);
    assert(!f_remove_tree(dir));
}
/* The helper every record writer goes through, by open directory: a new
 * file is private, an existing one is replaced only on request (EEXIST
 * otherwise) and neither leaves a temporary file; the two halves publish
 * what was staged, and a publication that fails removes it. */
static void durable_helper(const char *home) {
    char dir[F_PATH], path[F_PATH], temp[F_TEMP_NAME], missing[F_PATH];
    struct durable_limit limit; struct stat st;
    int fd, status; size_t before;
    assert(!f_path(dir, sizeof(dir), home, "durable-at") && !mkdir(dir, 0700));
    assert((fd = open(dir, O_RDONLY | O_DIRECTORY)) >= 0);
    assert(!f_write_at(fd, "record", "one", 3, false));
    assert(!f_path(path, sizeof(path), dir, "record") && !stat(path, &st) && (st.st_mode & 0777) == 0600 && st.st_nlink == 1);
    durable_expect(path, "one");
    before = durable_entries(dir);
    errno = 0;
    assert(f_write_at(fd, "record", "two", 3, false) && errno == EEXIST);
    durable_expect(path, "one");
    assert(!f_write_at(fd, "record", "two", 3, true));
    durable_expect(path, "two");
    assert(!f_write_at(fd, "record", "", 0, true) && !stat(path, &st) && st.st_size == 0);
    assert(durable_entries(dir) == before);

    assert(!f_stage_at(fd, "record", "three", 5, temp) && temp[0] && durable_entries(dir) == before + 1);
    durable_expect(path, "");
    assert(!f_publish_at(fd, temp, "record", true));
    durable_expect(path, "three");
    assert(!f_stage_at(fd, "record", "four", 4, temp));
    assert(f_publish_at(fd, temp, "record", false) && durable_entries(dir) == before);
    durable_expect(path, "three");

    durable_limit_on(&limit);
    status = f_write_at(fd, "record", "five", 4, true);
    durable_limit_off(&limit);
    assert(status && durable_entries(dir) == before);
    durable_expect(path, "three");

    /* A directory that is not there fails before anything is staged; a bare
     * name is written in the working directory. */
    assert(!f_path(missing, sizeof(missing), dir, "absent/record") && f_write(missing, "x", 1, true));
    assert(!chdir(dir) && !f_write("relative", "r", 1, false));
    assert(!f_path(path, sizeof(path), dir, "relative"));
    durable_expect(path, "r");
    assert(!chdir("/"));
    close(fd);
    assert(!f_remove_tree(dir));
}
