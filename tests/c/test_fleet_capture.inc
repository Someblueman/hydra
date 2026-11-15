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
static void capture_growth(void) {
    char script[256];
    capture_expect("exit 0", 0, 0, 0);
    capture_expect("head -c 1048579 /dev/zero | tr '\\000' a; head -c 100003 /dev/zero | tr '\\000' a >&2", 0, 1048579, 100003);
    (void)snprintf(script, sizeof(script), "head -c %u /dev/zero | tr '\\000' a", F_LIMIT - 1U);
    capture_expect(script, 0, F_LIMIT - 1U, 0);
    (void)snprintf(script, sizeof(script), "head -c %u /dev/zero | tr '\\000' a", F_LIMIT);
    capture_expect(script, 125, F_LIMIT, 0);
    capture_expect("while :; do printf aaaaaaaaaaaaaaaa; done", 125, F_LIMIT, 0);
    capture_expect("while :; do printf aaaaaaaaaaaaaaaa >&2; done", 125, 0, F_LIMIT);
}
