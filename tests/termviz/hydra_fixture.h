#ifndef HYDRA_PTY_FIXTURE_H
#define HYDRA_PTY_FIXTURE_H
#include "pty_support.h"
struct hf_fixture {
    char root[4096], build[4096], base[4096], repo[4096], home[4096];
    char hydra[4096], tui[4096], tmux[4096], socket[4096], output[262144];
};
/* One fixture per driver. Cleanup aborts only registered PTYs and its tmux socket.
 * A guardian process kills the private tmux server and removes the fixture on
 * every exit path, including failures and signals; HYDRA_TEST_KEEP_FIXTURE=1
 * keeps the directory (never the server). */
void hf_init(struct hf_fixture *f, const char *name, const char *repo_name, bool copied_plan,
             bool tmux);
void hf_commit_init(struct hf_fixture *f);
const char *hf_run(struct hf_fixture *f, const char *input, int expected, const char *const argv[]);
void hf_open(struct hf_fixture *f, struct tv_session *s);
void hf_cleanup(void);
/* Kills the private tmux server; true once no process names its socket. */
bool hf_server_gone(const struct hf_fixture *f);
/* Successful end: checks that no server remains, then removes the fixture. */
void hf_finish(struct hf_fixture *f);
void hf_trim(char *text);
void hf_glob_one(const char *pattern, char *out, size_t capacity);
size_t hf_glob_count(const char *pattern);
size_t hf_count(const char *text, const char *needle);
void hf_file(struct hf_fixture *f, const char *relative, char *out, size_t capacity);
#endif
