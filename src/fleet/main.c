#include "fleet.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
const char *f_home, *f_hydra;
static void stopped(int signal_number) { f_stopped = signal_number; }
int main(int argc, char **argv) {
    char home[F_PATH]; json_object *result = NULL; int status;
    f_home = getenv("HYDRA_HOME"); f_hydra = getenv("HYDRA_BIN_CMD");
    if (!f_home) { if (!getenv("HOME") || f_path(home, sizeof(home), getenv("HOME"), ".hydra")) return 1; f_home = home; }
    if (!f_hydra) f_hydra = "hydra";
    signal(SIGINT, stopped); signal(SIGTERM, stopped); signal(SIGHUP, stopped); signal(SIGPIPE, SIG_IGN);
    setenv("LC_ALL", "C", 1);
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("Hydra fleet protocol 1"); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "remote")) result = f_remote_cli(argc - 2, argv + 2);
    else if (argc == 3 && !strcmp(argv[1], "install")) {
        char *text = f_read(NULL, F_LIMIT), temp[] = "/tmp/hydra-package.XXXXXX", hash[65]; int fd = mkstemp(temp);
        json_object *package = text ? f_parse(text) : NULL;
        if (fd >= 0) close(fd);
        if (fd < 0 || !package || f_write(temp, text, strlen(text), true) || f_hash(temp, hash) || strcmp(hash, argv[2])) result = f_error("fleet-bootstrap", "hash_mismatch", "package digest failed");
        else result = f_install(package, hash);
        if (fd >= 0) unlink(temp);
        json_object_put(package); free(text);
    } else if (argc == 3 && !strcmp(argv[1], "fleet") && !strcmp(argv[2], "serve")) {
        json_object *request = f_read_json(NULL, F_LIMIT);
        result = request ? f_serve(request) : f_error("fleet", "invalid_request", "expected one bounded JSON object");
        json_object_put(request);
    } else if (argc >= 2 && !strcmp(argv[1], "fleet")) result = f_cli(argc - 2, argv + 2);
    else result = f_error("fleet", "invalid_input", "invoke through hydra fleet or hydra remote");
    if (!result) return f_stopped ? 128 + f_stopped : 0;
    status = f_emit(result); json_object_put(result); return status;
}
