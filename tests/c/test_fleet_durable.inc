/* A state write that cannot be completed publishes nothing: the call fails,
 * the previous value is still there and no temporary file is left behind.
 * The failure is a file size limit of zero with SIGXFSZ ignored, so every
 * write of at least one byte to a regular file returns EFBIG, as a full
 * disk returns ENOSPC. Nothing is asserted or printed while the limit is in
 * force: the test's own output may be a regular file. */
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
    struct rlimit saved, none;
    void (*previous)(int);
    int scalar_status, replace_status, create_status;
    size_t before;
    assert(!f_path(dir, sizeof(dir), home, "durable") && !mkdir(dir, 0700));
    assert(!f_path(scalar, sizeof(scalar), dir, "state") && !f_path(record, sizeof(record), dir, "record.json"));
    assert(!f_path(fresh, sizeof(fresh), dir, "fresh.json") && !f_path(nested, sizeof(nested), dir, "steps/build/attempts"));
    assert(!wdr_scalar(scalar, "running") && !wdr_scalar(nested, "1"));
    assert(!f_write(record, "{\"a\":1}", 7, true));
    durable_expect(scalar, "running\n"); durable_expect(nested, "1\n");
    before = durable_entries(dir);

    assert(!getrlimit(RLIMIT_FSIZE, &saved));
    none = saved; none.rlim_cur = 0;
    previous = signal(SIGXFSZ, SIG_IGN);
    assert(previous != SIG_ERR && !setrlimit(RLIMIT_FSIZE, &none));
    scalar_status = wdr_scalar(scalar, "succeeded");
    replace_status = f_write(record, "{\"a\":2}", 7, true);
    create_status = f_write(fresh, "{}", 2, false);
    if (setrlimit(RLIMIT_FSIZE, &saved)) abort();
    (void)signal(SIGXFSZ, previous);

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
