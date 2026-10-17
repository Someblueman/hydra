#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/transport/remote.h"
#include "fleet/transport/server.h"
#include "fleet/transport/bundle.h"
#include "fleet/cli.h"
#include "fleet/support/process.h"
#include "fleet/fleet.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/agent/agent.h"
#include "fleet/plan/plan.h"
#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_usage.h"
#include "fleet/review.h"
#include "fleet/review_task.h"
#include "fleet/setup/setup.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
const char *f_home, *f_hydra;
static void stopped(int signal_number) { f_stopped = signal_number; }
/* Guided remote setup commands own their human output and exit statuses. */
static bool setup_invocation(int argc, char **argv) {
    return argc >= 3 && !strcmp(argv[1], "remote") && setup_command(argv[2]);
}
static bool json_requested(int argc, char **argv) {
    int i;
    for (i = 3; i < argc; i++) if (!strcmp(argv[i], "--json")) return true;
    return false;
}
static int emit(int argc, char **argv, json_object *result) {
    if (setup_invocation(argc, argv)) return setup_emit(result, json_requested(argc, argv));
    return f_emit(result);
}
static int command_status(int argc, char **argv, json_object *result, int status) {
    if (setup_invocation(argc, argv)) return setup_exit_status(result, status);
    if (argc >= 3 && !strcmp(argv[1], "agent-run") && !strcmp(argv[2], "run") && json_object_get_boolean(f_field(result, "ok")))
        status = json_object_get_int(f_field(f_field(result, "data"), "exit_status"));
    if (argc >= 2 && !strcmp(argv[1], "workflow-task")) {
        const char *code = f_string(f_field(result, "error"), "code");
        if (code && !strcmp(code, "waiting_remote")) status = 75;
        else if (code && (!strcmp(code, "binding_invalid") || !strcmp(code, "invalid_binding") || !strcmp(code, "result_unavailable"))) status = 76;
    }
    return status;
}
/* Workflow-task exports that write TSV directly instead of a JSON envelope. */
static bool raw_workflow_task(int argc, char **argv, int *status) {
    if (argc != 4 || strcmp(argv[1], "workflow-task")) return false;
    if (!strcmp(argv[2], "metrics-tsv")) *status = wt_metrics_tsv(argv[3]);
    else if (!strcmp(argv[2], "usage-tsv")) *status = wu_usage_tsv(argv[3]);
    else return false;
    return true;
}
int main(int argc, char **argv) {
    static char home[F_PATH]; json_object *result = NULL; int status;
    f_home = getenv("HYDRA_HOME"); f_hydra = getenv("HYDRA_BIN_CMD");
    if (!f_home) { if (!getenv("HOME") || f_path(home, sizeof(home), getenv("HOME"), ".hydra")) return 1; f_home = home; }
    if (!f_hydra) f_hydra = "hydra";
    signal(SIGINT, stopped); signal(SIGTERM, stopped); signal(SIGHUP, stopped); signal(SIGPIPE, SIG_IGN);
    setenv("LC_ALL", "C", 1);
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("Hydra fleet protocol 1"); return 0; }
    if (raw_workflow_task(argc, argv, &status)) return status;
    if (argc >= 2 && !strcmp(argv[1], "workflow-plan")) result = plan_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "workflow-review")) result = review_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "workflow-review-data")) return review_workflow_data_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "workflow-task")) result = wt_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "workflow-data")) result = wd_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "agent-profile")) result = agent_profile_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "agent-run")) result = agent_run_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "agent-view")) result = agent_view_cli(argc - 2, argv + 2);
    else if (argc >= 2 && !strcmp(argv[1], "remote")) result = f_remote_cli(argc - 2, argv + 2);
    else if (argc >= 2 && (!strcmp(argv[1], "install") || !strcmp(argv[1], "install-check"))) result = f_install_cli(argc - 1, argv + 1);
    else if (argc == 3 && !strcmp(argv[1], "fleet") && !strcmp(argv[2], "serve")) {
        json_object *request = f_read_json(NULL, F_LIMIT);
        result = request ? f_serve(request) : f_error("fleet", "invalid_request", "expected one bounded JSON object");
        json_object_put(request);
    } else if (argc >= 2 && !strcmp(argv[1], "fleet")) result = f_cli(argc - 2, argv + 2);
    else result = f_error("fleet", "invalid_input", "invoke through hydra fleet or hydra remote");
    if (!result) return f_stopped ? 128 + f_stopped : 0;
    status = emit(argc, argv, result);
    status = command_status(argc, argv, result, status);
    json_object_put(result); return status;
}
