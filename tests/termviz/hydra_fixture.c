#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct hf_fixture *owned;
static char original_path[32768];
void hf_trim(char *text) {
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1]))
        text[--n] = 0;
}
static void quote(const char *value, char *out, size_t cap) {
    size_t at = 0;
    CHECK(cap > 2, "quote capacity");
    out[at++] = '\'';
    while (*value) {
        if (*value == '\'') {
            CHECK(at + 4 < cap, "quote capacity");
            memcpy(out + at, "'\\''", 4);
            at += 4;
        } else {
            CHECK(at + 1 < cap, "quote capacity");
            out[at++] = *value;
        }
        value++;
    }
    CHECK(at + 2 <= cap, "quote capacity");
    out[at++] = '\'';
    out[at] = 0;
}
void hf_cleanup(void) {
    char output[4096];
    if (!owned)
        return;
    if (owned->socket[0]) {
        const char *args[] = {owned->tmux, "-S", owned->socket, "kill-server", NULL};
        (void)tv_command(NULL, NULL, output, sizeof(output), 10, args);
    } /* Keep failed-run evidence; clean successful fixtures explicitly below. */
}
void hf_init(struct hf_fixture *f, const char *name, const char *repo_name, bool copied_plan,
             bool use_tmux) {
    char source[4096], path[4096], script[32768], quoted_tmux[8192], quoted_socket[8192],
        new_path[32768];
    memset(f, 0, sizeof(*f));
    tv_paths(f->root, sizeof(f->root), f->build, sizeof(f->build));
    tv_temp(f->base, sizeof(f->base), name);
    tv_format(f->repo, sizeof(f->repo), "%s/%s", f->base, repo_name);
    tv_format(f->home, sizeof(f->home), "%s/home", f->base);
    tv_format(f->hydra, sizeof(f->hydra), "%s/bin/hydra", f->root);
    tv_format(f->tui, sizeof(f->tui), "%s/hydra-tui", f->build);
    if (copied_plan) {
        tv_format(source, sizeof(source), "%s/tests/fixtures/plan/repo", f->root);
        tv_copy(source, f->repo, true);
    } else
        tv_mkdir(f->repo);
    tv_format(original_path, sizeof(original_path), "%s", getenv("PATH") ? getenv("PATH") : "");
    if (use_tmux) {
        const char *find[] = {"sh", "-c", "command -v tmux", NULL};
        CHECK(tv_command(NULL, NULL, f->tmux, sizeof(f->tmux), 5, find) == 0, "tmux available");
        hf_trim(f->tmux);
        tv_format(f->socket, sizeof(f->socket), "%s/tmux.sock", f->base);
        tv_format(path, sizeof(path), "%s/bin", f->base);
        tv_mkdir(path);
        tv_format(path, sizeof(path), "%s/bin/tmux", f->base);
        quote(f->tmux, quoted_tmux, sizeof(quoted_tmux));
        quote(f->socket, quoted_socket, sizeof(quoted_socket));
        tv_format(script, sizeof(script), "#!/bin/sh\nexec %s -S %s -f /dev/null \"$@\"\n",
                  quoted_tmux, quoted_socket);
        tv_write(path, script);
        CHECK(!chmod(path, 0755), "tmux wrapper executable");
        tv_format(new_path, sizeof(new_path), "%s/bin:%s", f->base, original_path);
        CHECK(!setenv("PATH", new_path, 1), "fixture PATH");
    }
    CHECK(!setenv("HYDRA_HOME", f->home, 1), "fixture home");
    tv_format(path, sizeof(path), "%s/hydra-fleet", f->build);
    setenv("HYDRA_FLEET_BIN", path, 1);
    setenv("HYDRA_NONINTERACTIVE", "1", 1);
    setenv("HYDRA_SKIP_AI", "1", 1);
    setenv("HYDRA_NO_SWITCH", "1", 1);
    setenv("TERM", "xterm-256color", 1);
    owned = f;
    CHECK(!atexit(hf_cleanup), "fixture cleanup registration");
}
const char *hf_run(struct hf_fixture *f, const char *input, int expected,
                   const char *const argv[]) {
    int status = tv_command(f->repo, input, f->output, sizeof(f->output), 60, argv);
    if (expected >= 0 && status != expected) {
        fprintf(stderr, "Fixture %s command %s: status=%d expected=%d\n%s\n", f->base, argv[0],
                status, expected, f->output);
        CHECK(false, "fixture command status");
    }
    if (expected == -2)
        CHECK(status != 0, "command must refuse");
    return f->output;
}
void hf_commit_init(struct hf_fixture *f) {
    const char *init[] = {"git", "init", "-q", NULL};
    const char *name[] = {"git", "config", "user.name", "Test", NULL};
    const char *email[] = {"git", "config", "user.email", "test@example.com", NULL};
    const char *add[] = {"git", "add", ".", NULL};
    const char *commit[] = {"git", "commit", "-qm", "fixture", NULL};
    const char *hydra[] = {f->hydra, "init", "--no-agent", "--trust", NULL};
    hf_run(f, NULL, 0, init);
    hf_run(f, NULL, 0, name);
    hf_run(f, NULL, 0, email);
    hf_run(f, NULL, 0, add);
    hf_run(f, NULL, 0, commit);
    hf_run(f, NULL, 0, hydra);
}
void hf_open(struct hf_fixture *f, struct tv_session *s) {
    const char *argv[] = {f->tui, "--hydra", f->hydra, NULL};
    tv_open(s, argv, 140, 40, f->repo);
}
void hf_glob_one(const char *pattern, char *out, size_t capacity) {
    glob_t g;
    memset(&g, 0, sizeof(g));
    CHECK(!glob(pattern, 0, NULL, &g), "matching fixture file");
    CHECK(g.gl_pathc == 1, "unique fixture file");
    tv_format(out, capacity, "%s", g.gl_pathv[0]);
    globfree(&g);
}
size_t hf_glob_count(const char *pattern) {
    glob_t g;
    int status;
    size_t count;
    memset(&g, 0, sizeof(g));
    status = glob(pattern, 0, NULL, &g);
    CHECK(status == 0 || status == GLOB_NOMATCH, "fixture glob");
    count = g.gl_pathc;
    globfree(&g);
    return count;
}
size_t hf_count(const char *text, const char *needle) {
    size_t n = 0, len = strlen(needle);
    CHECK(len, "nonempty needle");
    while ((text = strstr(text, needle)) != NULL) {
        n++;
        text += len;
    }
    return n;
}
void hf_file(struct hf_fixture *f, const char *relative, char *out, size_t capacity) {
    tv_format(out, capacity, "%s/%s", f->repo, relative);
}
