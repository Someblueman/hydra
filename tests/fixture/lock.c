#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int path_join(char *out, size_t size, const char *base, const char *tail) {
    int n = snprintf(out, size, "%s/%s", base, tail);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
static void delay(long nanoseconds) {
    struct timespec pause = {0, nanoseconds};
    while (nanosleep(&pause, &pause) && errno == EINTR) {
    }
}
static int quiesce(const char *home) {
    char pattern[4096];
    if (path_join(pattern, sizeof pattern, home, "fleet/tasks/task_*/owner.lock"))
        return 1;
    for (int attempt = 0; attempt < 100; attempt++) {
        glob_t paths = {0};
        int found = glob(pattern, 0, NULL, &paths), busy = 0, failed = 0;
        if (found != 0 && found != GLOB_NOMATCH) {
            globfree(&paths);
            return 1;
        }
        for (size_t i = 0; i < paths.gl_pathc; i++) {
            int fd = open(paths.gl_pathv[i], O_RDWR | O_NOFOLLOW);
            if (fd < 0) {
                if (errno != ENOENT)
                    failed = 1;
                continue;
            }
            if (flock(fd, LOCK_EX | LOCK_NB)) {
                if (errno == EWOULDBLOCK || errno == EAGAIN)
                    busy = 1;
                else
                    failed = 1;
            }
            close(fd);
        }
        globfree(&paths);
        if (failed)
            return 1;
        if (!busy)
            return 0;
        delay(200000000);
    }
    return 1;
}
static int regression(const char *root, const char *fixture) {
    char home[4096], directory[4096], path[4096];
    int fd = -1, result = 1, status = 0;
    pid_t child = -1;
    if (path_join(home, sizeof home, fixture, "home"))
        return 1;
    const char *parts[] = {"home", "home/fleet", "home/fleet/tasks", "home/fleet/tasks/task_owner"};
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
        if (path_join(directory, sizeof directory, fixture, parts[i]) ||
            (mkdir(directory, 0700) && errno != EEXIST))
            goto cleanup;
    }
    if (path_join(path, sizeof path, directory, "state.json"))
        goto cleanup;
    FILE *state = fopen(path, "w");
    if (!state)
        goto cleanup;
    int written = fprintf(state, "{\"state\":\"running\",\"owner_pid\":%ld}\n", (long)getpid());
    int closed = fclose(state);
    if (written < 0 || closed)
        goto cleanup;
    if (path_join(path, sizeof path, directory, "owner.lock"))
        goto cleanup;
    fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0 || flock(fd, LOCK_EX) || fcntl(fd, F_SETFD, FD_CLOEXEC))
        goto cleanup;
    child = fork();
    if (child < 0)
        goto cleanup;
    if (child == 0) {
        close(fd);
        if (setenv("root", root, 1) || setenv("fixture", fixture, 1))
            _exit(125);
        execlp("sh", "sh", "-c",
               ". \"$1/tests/workflow_task_cleanup.sh\"; workflow_task_fixture_quiesce \"$2\"",
               "fixture-owner", root, home, (char *)NULL);
        _exit(127);
    }
    delay(500000000);
    if (waitpid(child, &status, WNOHANG) != 0) {
        child = -1;
        fprintf(stderr, "cleanup did not wait for the held owner lock\n");
        goto cleanup;
    }
    if (flock(fd, LOCK_UN))
        goto cleanup;
    for (int attempt = 0; attempt < 50; attempt++) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            child = -1;
            result = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
            break;
        }
        if (done < 0)
            goto cleanup;
        delay(100000000);
    }
cleanup:
    if (child > 0) {
        kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    if (fd >= 0)
        close(fd);
    if (!result)
        puts("PASS receiver cleanup: real owner lock blocks; reused PID does not");
    return result;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "wait"))
        return quiesce(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "check"))
        return regression(argv[2], argv[3]);
    fprintf(stderr, "usage: fixture-lock wait HOME | check ROOT FIXTURE\n");
    return 2;
}
