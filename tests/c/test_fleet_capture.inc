/* Capture buffers grow with the output: large output arrives whole and NUL
 * terminated on both streams, output that reaches F_LIMIT is reported as
 * truncated with status 125, and an endless writer is stopped there. */
static void capture_expect(const char *script, int status, size_t out_bytes, size_t err_bytes) {
    struct f_capture cap = {0};
    char *command[] = {(char *)"sh", (char *)"-c", (char *)script, NULL};
    assert(!f_run(command, NULL, 0, 30, &cap));
    assert(cap.status == status);
    assert(cap.out_bytes == out_bytes && cap.err_bytes == err_bytes);
    assert(cap.out && strlen(cap.out) == out_bytes && cap.err && strlen(cap.err) == err_bytes);
    assert(!out_bytes || cap.out[out_bytes - 1] == 'a');
    assert(cap.measurement_complete == (status != 125));
    f_capture_free(&cap);
}
/* A file of n 'a' bytes. The output is written here rather than made with
 * head and tr, whose handling of /dev/zero and octal escapes differs between
 * coreutils implementations. */
static void write_as(const char *path, size_t n) {
    char block[4096];
    FILE *file = fopen(path, "wb");
    assert(file);
    memset(block, 'a', sizeof(block));
    while (n) {
        size_t chunk = n < sizeof(block) ? n : sizeof(block);
        assert(fwrite(block, 1, chunk, file) == chunk);
        n -= chunk;
    }
    assert(!fclose(file));
}
static void capture_growth(const char *dir) {
    char script[3 * F_PATH], out[F_PATH], err[F_PATH], under[F_PATH], at[F_PATH];
    assert(snprintf(out, sizeof(out), "%s/capture-out", dir) < (int)sizeof(out));
    assert(snprintf(err, sizeof(err), "%s/capture-err", dir) < (int)sizeof(err));
    assert(snprintf(under, sizeof(under), "%s/capture-under", dir) < (int)sizeof(under));
    assert(snprintf(at, sizeof(at), "%s/capture-at", dir) < (int)sizeof(at));
    write_as(out, 1048579U); write_as(err, 100003U); write_as(under, F_LIMIT - 1U); write_as(at, F_LIMIT);
    capture_expect("exit 0", 0, 0, 0);
    (void)snprintf(script, sizeof(script), "cat '%s'; cat '%s' >&2", out, err);
    capture_expect(script, 0, 1048579, 100003);
    (void)snprintf(script, sizeof(script), "cat '%s'", under);
    capture_expect(script, 0, F_LIMIT - 1U, 0);
    (void)snprintf(script, sizeof(script), "cat '%s'", at);
    capture_expect(script, 125, F_LIMIT, 0);
    capture_expect("while :; do printf aaaaaaaaaaaaaaaa; done", 125, F_LIMIT, 0);
    capture_expect("while :; do printf aaaaaaaaaaaaaaaa >&2; done", 125, 0, F_LIMIT);
    unlink(out); unlink(err); unlink(under); unlink(at);
}
