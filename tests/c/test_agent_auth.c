#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/auth/agent_auth.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
const char *f_home, *f_hydra = "hydra";

struct fixture {
    char base[F_PATH], path[F_PATH], digest[65], before[65];
    json_object *a, *b;
};

/* The stored credential reads back as key holding expected, with its digest. */
static void expect_stored(struct fixture *f, const char *expected) {
    json_object *value = NULL;
    assert(!auth_read(f->path, &value, f->digest));
    assert(!strcmp(f_string(value, "OPENAI_API_KEY"), expected));
    json_object_put(value);
}
static void expect_private_file(const char *path, bool single_link) {
    struct stat st;
    assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    assert(!single_link || st.st_nlink == 1);
}

static void test_create(struct fixture *f) {
    json_object *value = NULL;
    assert(!f_path(f->path, sizeof(f->path), f->base, "new/sub/auth.json"));
    assert(!auth_read(f->path, &value, f->digest) && !value && !strcmp(f->digest, "absent"));
    assert(!auth_store(f->path, "absent", f->a));
    expect_private_file(f->path, false);
    assert(auth_store(f->path, "absent", f->b) == -2);
    expect_stored(f, "synthetic-a");
    assert(!f_copy(f->before, sizeof(f->before), f->digest));
}

/* A writer that waits for the go byte, then stores b over the digest it holds. */
static void writer_child(const struct fixture *f, int pipefd[2]) {
    char go; int rc;
    close(pipefd[1]);
    assert(read(pipefd[0], &go, 1) == 1); close(pipefd[0]);
    rc = auth_store(f->path, f->before, f->b);
    _exit(rc == 0 ? 0 : rc == -2 ? 2 : 3);
}
/* How a writer ended: 1 stored, 0 stale, -1 anything else. */
static int writer_outcome(pid_t child) {
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
    if (WEXITSTATUS(status) == 0) return 1;
    return WEXITSTATUS(status) == 2 || WEXITSTATUS(status) == 3 ? 0 : -1;
}
/* Two writers holding the same digest: one wins, the other is stale. */
static void test_concurrent_writers(struct fixture *f) {
    int pipefd[2], successes = 0, stale = 0, i; pid_t child[2];
    assert(!pipe(pipefd));
    for (i = 0; i < 2; i++) {
        child[i] = fork(); assert(child[i] >= 0);
        if (!child[i]) writer_child(f, pipefd);
    }
    close(pipefd[0]); assert(write(pipefd[1], "xx", 2) == 2); close(pipefd[1]);
    for (i = 0; i < 2; i++) {
        int outcome = writer_outcome(child[i]);
        successes += outcome == 1;
        stale += outcome == 0;
    }
    assert(successes == 1 && stale == 1);
    expect_stored(f, "synthetic-b");
    assert(auth_store(f->path, f->before, f->a) == -2);
}

static size_t count_entries(const char *dir, bool check_names) {
    DIR *stream; struct dirent *entry; size_t entries = 0;
    assert((stream = opendir(dir)));
    while ((entry = readdir(stream))) {
        entries++;
        assert(!check_names || (!strstr(entry->d_name, "auth.json.") && !strstr(entry->d_name, "other.json")));
    }
    closedir(stream);
    return entries;
}

/* A credential that cannot be written (every write fails as on a full
 * disk: a zero file size limit, SIGXFSZ ignored) is reported, and the
 * stored one stays as it was with nothing left beside it. */
static void test_write_failure(struct fixture *f) {
    struct rlimit saved, none; void (*previous)(int); char dir[F_PATH], fresh[F_PATH];
    int replaced, created; size_t entries;
    assert(!f_path(dir, sizeof(dir), f->base, "new/sub") && !f_path(fresh, sizeof(fresh), f->base, "new/sub/other.json"));
    entries = count_entries(dir, false);
    assert(!getrlimit(RLIMIT_FSIZE, &saved));
    none = saved; none.rlim_cur = 0;
    previous = signal(SIGXFSZ, SIG_IGN);
    assert(previous != SIG_ERR && !setrlimit(RLIMIT_FSIZE, &none));
    replaced = auth_store(f->path, f->digest, f->a);
    created = auth_store(fresh, "absent", f->a);
    if (setrlimit(RLIMIT_FSIZE, &saved)) abort();
    (void)signal(SIGXFSZ, previous);
    assert(replaced == -1 && created == -1 && access(fresh, F_OK));
    assert(count_entries(dir, true) == entries);
    assert(!f_copy(f->before, sizeof(f->before), f->digest));
    expect_stored(f, "synthetic-b");
    assert(!strcmp(f->before, f->digest));
    expect_private_file(f->path, true);
    assert(!auth_store(f->path, f->digest, f->a));
    expect_stored(f, "synthetic-a");
    expect_private_file(f->path, true);
}

/* A store that is too open or too large is refused on read. */
static void test_unsafe_file(struct fixture *f) {
    json_object *value = NULL; int fd, i; char bytes[1024];
    assert(!chmod(f->path, 0644) && auth_read(f->path, &value, f->digest));
    assert(!chmod(f->path, 0600));
    fd = open(f->path, O_WRONLY | O_TRUNC);
    assert(fd >= 0); memset(bytes, 'x', sizeof(bytes));
    for (i = 0; i < 65; i++) assert(write(fd, bytes, sizeof(bytes)) == sizeof(bytes));
    close(fd); assert(auth_read(f->path, &value, f->digest));
}

int main(void) {
    char temp[] = "/tmp/hydra-auth-test.XXXXXX";
    struct fixture f;
    memset(&f, 0, sizeof(f));
    f.a = f_parse("{\"OPENAI_API_KEY\":\"synthetic-a\"}"); f.b = f_parse("{\"OPENAI_API_KEY\":\"synthetic-b\"}");
    assert(mkdtemp(temp) && realpath(temp, f.base)); f_home = f.base;
    test_create(&f);
    test_concurrent_writers(&f);
    test_write_failure(&f);
    test_unsafe_file(&f);
    json_object_put(f.a); json_object_put(f.b);
    assert(!f_remove_tree(f.base));
    puts("Agent credential storage tests passed"); return 0;
}
