#include "fleet/workflow/workflow_schedule.h"
#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct history {
    int count;
    char digest[65];
    json_object *last, *decisions;
};
static FILE *journal_open(const char *path, bool writable) {
    int flags = writable ? O_RDWR | O_CREAT | O_APPEND : O_RDONLY;
    int fd = open(path, flags | O_NOFOLLOW | O_NONBLOCK, 0600); struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) { close(fd); return NULL; }
    FILE *stream = fdopen(fd, writable ? "a+" : "r");
    if (!stream) close(fd);
    return stream;
}
static json_object *graph_read(const char *run, int *parallelism) {
    char path[F_PATH], id[65], *text = NULL; json_object *graph = NULL;
    if (f_path(path, sizeof(path), run, "graph.tsv") || !(text = f_read(path, F_LIMIT))) goto done;
    if (sscanf(text, "workflow\t%64[^\t]\t%d", id, parallelism) != 2 || *parallelism < 1 || *parallelism > 16) goto done;
    graph = wd_graph_read(path);
done:
    free(text); return graph;
}
static int verify_entry(json_object *entry, json_object *graph, int parallelism, const char *graph_sha, struct history *history) {
    const char *const keys[] = {"schema_version", "sequence", "graph_sha256", "parallelism", "previous_sha256", "observations", "decision", "sha256", NULL};
    const char *digest = f_string(entry, "sha256"), *previous = f_string(entry, "previous_sha256"), *bound = f_string(entry, "graph_sha256");
    const char *decision = f_string(entry, "decision"); char computed[65], choice[65]; json_object *payload; bool valid;
    if (!task_keys(entry, keys) || !f_number_is(entry, "schema_version", 1) || !f_number_is(entry, "sequence", history->count + 1) ||
        !f_number_is(entry, "parallelism", parallelism) || !digest || !previous || !bound || !decision ||
        strcmp(previous, history->digest) || strcmp(bound, graph_sha) || ws_decide(graph, f_field(entry, "observations"), parallelism, choice) || strcmp(choice, decision)) return -1;
    payload = plan_canonical(entry); json_object_object_del(payload, "sha256");
    valid = !plan_digest(payload, computed) && !strcmp(computed, digest); json_object_put(payload);
    if (!valid) return -1;
    strcpy(history->digest, computed); history->count++;
    json_object_put(history->last); history->last = json_object_get(entry);
    if (history->decisions) json_object_array_add(history->decisions, json_object_new_string(decision));
    return 0;
}
static int history_read(FILE *stream, json_object *graph, int parallelism, const char *graph_sha, struct history *history) {
    char line[16384];
    memset(history->digest, '0', 64); history->digest[64] = '\0';
    while (fgets(line, sizeof(line), stream)) {
        json_object *entry;
        if (!strchr(line, '\n') || history->count >= 1000000 || !(entry = f_parse(line))) return -1;
        int status = verify_entry(entry, graph, parallelism, graph_sha, history); json_object_put(entry);
        if (status) return -1;
    }
    return ferror(stream) ? -1 : 0;
}
static int append(FILE *stream, json_object *observations, const char *choice, int parallelism, const char *graph_sha, struct history *history) {
    json_object *entry = json_object_new_object(); char digest[65]; int status = -1;
    json_object_object_add(entry, "schema_version", json_object_new_int(1));
    json_object_object_add(entry, "sequence", json_object_new_int(history->count + 1));
    json_object_object_add(entry, "parallelism", json_object_new_int(parallelism));
    f_string_add(entry, "graph_sha256", graph_sha); f_string_add(entry, "previous_sha256", history->digest);
    json_object_object_add(entry, "observations", json_object_get(observations)); f_string_add(entry, "decision", choice);
    if (plan_digest(entry, digest)) goto done;
    f_string_add(entry, "sha256", digest);
    if (fseek(stream, 0, SEEK_END) || fprintf(stream, "%s\n", json_object_to_json_string_ext(entry, JSON_C_TO_STRING_PLAIN)) < 0 ||
        fflush(stream) || fsync(fileno(stream))) goto done;
    status = 0;
done:
    json_object_put(entry); return status;
}
static bool unchanged(json_object *last, json_object *observations, const char *choice) {
    json_object *prior = f_field(last, "observations");
    return last && !strcmp(choice, f_string(last, "decision")) &&
        json_object_equal(f_field(prior, "states"), f_field(observations, "states")) &&
        json_object_equal(f_field(prior, "cancelled"), f_field(observations, "cancelled")) &&
        json_object_equal(f_field(prior, "deadline"), f_field(observations, "deadline"));
}
int ws_next(const char *run) {
    const char *owner = getenv("HYDRA_WORKFLOW_LOCKED_RUN");
    json_object *bindings = wt_bindings(run), *graph = NULL, *observations = NULL; struct history history = {0};
    char path[F_PATH], choice[65]; int parallelism = 0, status = -1; FILE *stream = NULL;
    if (!owner || strcmp(owner, run) || !bindings || !(graph = graph_read(run, &parallelism)) ||
        !(observations = ws_observe(run, graph)) || ws_decide(graph, observations, parallelism, choice) ||
        f_path(path, sizeof(path), run, "schedule.jsonl") || !(stream = journal_open(path, true))) goto done;
    rewind(stream);
    const char *graph_sha = f_string(bindings, "graph.tsv");
    if (history_read(stream, graph, parallelism, graph_sha, &history)) goto done;
    if (!unchanged(history.last, observations, choice)) {
        if (append(stream, observations, choice, parallelism, graph_sha, &history) || task_sync_dir(run)) goto done;
    }
    if (*choice) puts(choice);
    status = 0;
done:
    if (stream) fclose(stream);
    json_object_put(history.last); json_object_put(observations); json_object_put(graph); json_object_put(bindings); return status;
}
json_object *ws_replay(const char *run) {
    json_object *bindings = wt_bindings(run), *graph = NULL, *result = NULL; struct history history = {0};
    char path[F_PATH]; int parallelism = 0; FILE *stream = NULL;
    history.decisions = json_object_new_array();
    if (!bindings || !(graph = graph_read(run, &parallelism)) || f_path(path, sizeof(path), run, "schedule.jsonl") ||
        !(stream = journal_open(path, false)) || history_read(stream, graph, parallelism, f_string(bindings, "graph.tsv"), &history)) goto done;
    json_object *data = json_object_new_object();
    json_object_object_add(data, "decisions", json_object_get(history.decisions));
    json_object_object_add(data, "observations", json_object_new_int(history.count)); f_string_add(data, "journal_sha256", history.digest);
    result = f_success("workflow replay", data);
done:
    if (stream) fclose(stream);
    json_object_put(history.last); json_object_put(history.decisions); json_object_put(graph); json_object_put(bindings);
    return result ? result : f_error("workflow replay", "invalid_journal", "recorded observations, decision, graph binding or journal chain is invalid");
}
