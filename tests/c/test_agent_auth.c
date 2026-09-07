#include "fleet/agent_auth.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
const char *f_home, *f_hydra = "hydra";

int main(void) {
    char temp[] = "/tmp/hydra-auth-test.XXXXXX", base[F_PATH], path[F_PATH], digest[65], before[65];
    json_object *a = f_parse("{\"OPENAI_API_KEY\":\"synthetic-a\"}"), *b = f_parse("{\"OPENAI_API_KEY\":\"synthetic-b\"}"), *value = NULL;
    struct stat st; int pipefd[2], status, successes = 0, stale = 0, i; pid_t child[2];
    assert(mkdtemp(temp) && realpath(temp, base)); f_home = base;
    assert(!f_path(path, sizeof(path), base, "new/sub/auth.json"));
    assert(!auth_read(path, &value, digest) && !value && !strcmp(digest, "absent"));
    assert(!auth_store(path, "absent", a));
    assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    assert(auth_store(path, "absent", b) == -2);
    assert(!auth_read(path, &value, digest));
    assert(!strcmp(f_string(value, "OPENAI_API_KEY"), "synthetic-a")); json_object_put(value); value = NULL;
    assert(!f_copy(before, sizeof(before), digest));
    assert(!pipe(pipefd));
    for (i = 0; i < 2; i++) {
        child[i] = fork(); assert(child[i] >= 0);
        if (!child[i]) {
            char go; int rc;
            close(pipefd[1]); assert(value == NULL);
            assert(read(pipefd[0], &go, 1) == 1); close(pipefd[0]);
            rc = auth_store(path, before, b);
            _exit(rc == 0 ? 0 : rc == -2 ? 2 : 3);
        }
    }
    close(pipefd[0]); assert(write(pipefd[1], "xx", 2) == 2); close(pipefd[1]);
    for (i = 0; i < 2; i++) {
        assert(waitpid(child[i], &status, 0) == child[i] && WIFEXITED(status));
        if (WEXITSTATUS(status) == 0) successes++;
        else if (WEXITSTATUS(status) == 2 || WEXITSTATUS(status) == 3) stale++;
    }
    assert(successes == 1 && stale == 1);
    assert(!auth_read(path, &value, digest));
    assert(!strcmp(f_string(value, "OPENAI_API_KEY"), "synthetic-b")); json_object_put(value); value = NULL;
    assert(auth_store(path, before, a) == -2);
    assert(!chmod(path, 0644) && auth_read(path, &value, digest));
    assert(!chmod(path, 0600));
    {
        int fd = open(path, O_WRONLY | O_TRUNC); char bytes[1024];
        assert(fd >= 0); memset(bytes, 'x', sizeof(bytes));
        for (i = 0; i < 65; i++) assert(write(fd, bytes, sizeof(bytes)) == sizeof(bytes));
        close(fd); assert(auth_read(path, &value, digest));
    }
    json_object_put(a); json_object_put(b);
    assert(!f_remove_tree(base));
    puts("Agent credential storage tests passed"); return 0;
}
