#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Strict, bounded read model. Dependency semantics remain recorded evidence. */
bool workflow_id(const char *s) {
    size_t i;
    if (!s[0] || strlen(s) > 79) return false;
    for (i = 0; s[i]; i++) if (!((s[i] >= 'a' && s[i] <= 'z') ||
        (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-')) return false;
    return true;
}

size_t workflow_nodes(const struct workflow_model *m, size_t run, size_t *indices) {
    size_t i, n = 0;
    for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run) {
        if (n == TV_GRAPH_MAX_NODES) return n + 1;
        indices[n++] = i;
    }
    return n;
}

bool workflow_edges(const struct workflow_model *m, const size_t *indices, size_t n,
                            struct tv_edge *edges, size_t *count) {
    size_t i;
    *count = 0;
    for (i = 0; i < n; i++) {
        char needs[1024], *token, *save = NULL;
        copy_text(needs, sizeof(needs), m->nodes[indices[i]].needs);
        if (!strcmp(needs, "-")) continue;
        if (needs[0] == ',' || needs[strlen(needs) - 1] == ',' || strstr(needs, ",,")) return false;
        token = strtok_r(needs, ",", &save);
        while (token) {
            size_t j;
            for (j = 0; j < n; j++) if (!strcmp(token, m->nodes[indices[j]].id)) break;
            if (j == n || *count == TV_GRAPH_MAX_EDGES) return false;
            {
                size_t previous;
                for (previous = 0; previous < *count; previous++)
                    if (edges[previous].from == j && edges[previous].to == i) return false;
            }
            edges[(*count)++] = (struct tv_edge){j, i};
            token = strtok_r(NULL, ",", &save);
        }
    }
    return true;
}

/* "-" is unknown (-1); otherwise a bounded non-negative decimal. */
static bool workflow_number(const char *text, long long *out) {
    char *end = NULL;
    long long value;
    if (!strcmp(text, "-")) { *out = -1; return true; }
    if (!text[0] || text[0] == '-' || text[0] == '+') return false;
    errno = 0; value = strtoll(text, &end, 10);
    if (errno || *end || value < 0) return false;
    *out = value;
    return true;
}

static bool workflow_text(char *out, size_t size, const char *text) {
    if (strlen(text) >= size) return false;
    copy_text(out, size, !strcmp(text, "-") ? "" : text);
    return true;
}

static size_t workflow_run_index(const struct workflow_model *m, const char *id) {
    size_t run;
    for (run = 0; run < m->run_count; run++) if (!strcmp(m->runs[run].id, id)) break;
    return run;
}

/* W id name state [kind planning-branch planning-head created completed digest] */
static int workflow_run_record(struct workflow_model *m, char **f, size_t count) {
    struct workflow_run *r;
    if (m->run_count == WF_RUNS || !workflow_id(f[1]) || strlen(f[2]) >= 80 || strlen(f[3]) >= 40 ||
        workflow_run_index(m, f[1]) < m->run_count) return -1;
    r = &m->runs[m->run_count];
    memset(r, 0, sizeof(*r)); r->completed = -1;
    copy_text(r->id, sizeof(r->id), f[1]); copy_text(r->name, sizeof(r->name), f[2]);
    copy_text(r->state, sizeof(r->state), f[3]);
    if (count == 10 && (!workflow_text(r->kind, sizeof(r->kind), f[4]) || !workflow_text(r->planning, sizeof(r->planning), f[5]) ||
        !workflow_text(r->planning_head, sizeof(r->planning_head), f[6]) || !workflow_text(r->created, sizeof(r->created), f[7]) ||
        !workflow_number(f[8], &r->completed) || !workflow_text(r->digest, sizeof(r->digest), f[9]))) return -1;
    m->run_count++;
    return 0;
}

static bool workflow_node_duplicate(const struct workflow_model *m, size_t run, const char *id) {
    size_t i;
    for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run && !strcmp(m->nodes[i].id, id)) return true;
    return false;
}

/* N run step kind state attempts needs [role head profile started completed] */
static int workflow_node_record(struct workflow_model *m, char **f, size_t count) {
    struct workflow_node *n;
    size_t run = workflow_run_index(m, f[1]);
    if (run == m->run_count || m->node_count == WF_NODES || !workflow_id(f[2]) ||
        strlen(f[2]) >= 65 || strlen(f[3]) >= 32 || strlen(f[4]) >= 40 ||
        strlen(f[6]) >= 1024 || !f[6][0] || workflow_node_duplicate(m, run, f[2])) return -1;
    n = &m->nodes[m->node_count];
    memset(n, 0, sizeof(*n)); n->run = run; n->started = n->completed = -1;
    if (!parse_unsigned(f[5], &n->attempts)) return -1;
    copy_text(n->id, sizeof(n->id), f[2]); copy_text(n->kind, sizeof(n->kind), f[3]);
    copy_text(n->state, sizeof(n->state), f[4]); copy_text(n->needs, sizeof(n->needs), f[6]);
    if (count == 12 && (!workflow_text(n->role, sizeof(n->role), f[7]) || !workflow_text(n->head, sizeof(n->head), f[8]) ||
        !workflow_text(n->profile, sizeof(n->profile), f[9]) || !workflow_number(f[10], &n->started) ||
        !workflow_number(f[11], &n->completed))) return -1;
    m->node_count++;
    return 0;
}

static struct workflow_node *workflow_node_named(struct workflow_model *m, const char *run_id, const char *step) {
    size_t run = workflow_run_index(m, run_id), i;
    if (run == m->run_count) return NULL;
    for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run && !strcmp(m->nodes[i].id, step)) return &m->nodes[i];
    return NULL;
}

static bool workflow_exec_numbers(struct workflow_exec *e, char **f) {
    return workflow_number(f[6], &e->exit_status) && workflow_number(f[7], &e->started) &&
        workflow_number(f[8], &e->finished) && workflow_number(f[14], &e->tokens_in) &&
        workflow_number(f[15], &e->tokens_cached) && workflow_number(f[16], &e->tokens_out);
}

static bool workflow_exec_texts(struct workflow_exec *e, char **f) {
    return workflow_text(e->profile, sizeof(e->profile), f[4]) && workflow_text(e->state, sizeof(e->state), f[5]) &&
        workflow_text(e->version, sizeof(e->version), f[9]) && workflow_text(e->model, sizeof(e->model), f[10]) &&
        workflow_text(e->model_source, sizeof(e->model_source), f[11]) && workflow_text(e->effort, sizeof(e->effort), f[12]) &&
        workflow_text(e->effort_source, sizeof(e->effort_source), f[13]) && workflow_text(e->cost, sizeof(e->cost), f[17]) &&
        workflow_text(e->config_source, sizeof(e->config_source), f[18]);
}

/* E run step attempt profile state exit started finished version model
 *   model-source effort effort-source in cached out cost config-source */
static int workflow_exec_record(struct workflow_model *m, char **f) {
    struct workflow_node *n = workflow_node_named(m, f[1], f[2]);
    struct workflow_exec e;
    memset(&e, 0, sizeof(e));
    if (!n || n->exec.present || !parse_unsigned(f[3], &e.attempt) || !workflow_exec_numbers(&e, f) || !workflow_exec_texts(&e, f)) return -1;
    e.present = true;
    n->exec = e;
    return 0;
}

/* R run branch spawn-step class retirement detail */
static int workflow_head_record(struct workflow_model *m, char **f) {
    struct workflow_head *h;
    size_t run = workflow_run_index(m, f[1]);
    if (run == m->run_count || m->head_count == WF_HEADS || !f[2][0] || !workflow_id(f[3])) return -1;
    h = &m->heads[m->head_count];
    memset(h, 0, sizeof(*h)); h->run = run;
    if (!workflow_text(h->branch, sizeof(h->branch), f[2]) || !workflow_text(h->step, sizeof(h->step), f[3]) ||
        !workflow_text(h->role, sizeof(h->role), f[4]) || !workflow_text(h->retirement, sizeof(h->retirement), f[5]) ||
        !workflow_text(h->detail, sizeof(h->detail), f[6])) return -1;
    m->head_count++;
    return 0;
}

static int workflow_record(struct workflow_model *m, char **f, size_t count, bool v2) {
    if (count == 2 && !strcmp(f[0], "X")) { copy_text(m->warning, sizeof(m->warning), f[1]); return 0; }
    if (!strcmp(f[0], "W") && count == (v2 ? 10U : 4U)) return workflow_run_record(m, f, count);
    if (!strcmp(f[0], "N") && count == (v2 ? 12U : 7U)) return workflow_node_record(m, f, count);
    if (v2 && !strcmp(f[0], "E") && count == 19U) return workflow_exec_record(m, f);
    if (v2 && !strcmp(f[0], "R") && count == 7U) return workflow_head_record(m, f);
    return -1;
}

static bool workflow_graphs_valid(const struct workflow_model *m) {
    size_t i;
    for (i = 0; i < m->run_count; i++) {
        size_t indices[TV_GRAPH_MAX_NODES], count, n = workflow_nodes(m, i, indices);
        struct tv_edge edges[TV_GRAPH_MAX_EDGES]; struct tv_graph_layout layout;
        if (n > TV_GRAPH_MAX_NODES || !workflow_edges(m, indices, n, edges, &count) ||
            !tv_graph_layout(edges, count, n, &layout)) return false;
    }
    return true;
}

/* Protocol 1 carries runs and steps; protocol 2 adds planning ownership,
 * step roles, heads, agent receipts and spawned-head retirement. */
static int load_workflows(FILE *input, struct workflow_model *m) {
    char line[4096];
    size_t bytes = 0;
    int version = 0;
    memset(m, 0, sizeof(*m));
    while (fgets(line, sizeof(line), input)) {
        char *fields[20];
        size_t length = strlen(line), count;
        if (!length || line[length - 1] != '\n' || (bytes += length) > MAX_DATA_BYTES) return -1;
        line[length - 1] = '\0';
        count = split_fields(line, fields, 20);
        if (!version) {
            if (count != 2 || strcmp(fields[0], "HYDRA_WORKFLOW_TUI") || (strcmp(fields[1], "1") && strcmp(fields[1], "2"))) return -1;
            version = fields[1][0] - '0'; continue;
        }
        if (count >= 20 || workflow_record(m, fields, count, version == 2)) return -1;
    }
    return version && !ferror(input) && workflow_graphs_valid(m) ? 0 : -1;
}

/* Consumes the completed observation stream. */
int accept_workflows(struct app *app, FILE *input) {
    struct workflow_model *next;
    next = input ? malloc(sizeof(*next)) : NULL;
    if (!input || !next || load_workflows(input, next)) {
        if (input) fclose(input);
        free(next); app->workflow_stale = true;
        copy_text(app->workflow_error, sizeof(app->workflow_error), "Workflow data unavailable or invalid; retained graph is stale");
        return -1;
    }
    fclose(input);
    if (app->workflows && app->workflow_run < app->workflows->run_count) {
        size_t i, previous[TV_GRAPH_MAX_NODES], next_indices[TV_GRAPH_MAX_NODES];
        size_t old_count = workflow_nodes(app->workflows, app->workflow_run, previous);
        char selected_id[65] = "";
        if (app->workflow_node < old_count) copy_text(selected_id, sizeof(selected_id), app->workflows->nodes[previous[app->workflow_node]].id);
        for (i = 0; i < next->run_count; i++) if (!strcmp(next->runs[i].id, app->workflows->runs[app->workflow_run].id)) break;
        app->workflow_run = i < next->run_count ? i : 0;
        if (i < next->run_count) {
            size_t next_count = workflow_nodes(next, i, next_indices);
            for (i = 0; i < next_count; i++) if (!strcmp(next->nodes[next_indices[i]].id, selected_id)) break;
            app->workflow_node = i < next_count ? i : 0;
        } else app->workflow_node = 0;
    }
    free(app->workflows); app->workflows = next;
    app->workflow_stale = false; app->workflow_error[0] = '\0';
    app->workflow_at = time(NULL);
    return 0;
}

int refresh_workflows(struct app *app, const char *fixture) {
    char notice[TEXT];
    FILE *input;
    copy_text(notice,sizeof(notice),app->notice);
    if (!fixture) native_observations_cancel(app,1);
    input=fixture ? fopen(fixture,"r") : capture_adapter(app,"workflow","tui-data",2000);
    copy_text(app->notice,sizeof(app->notice),notice);
    return accept_workflows(app,input);
}

void workflow_move(struct app *app, int direction) {
    size_t indices[TV_GRAPH_MAX_NODES], n;
    if (!app->workflows) return;
    n = workflow_nodes(app->workflows, app->workflow_run, indices);
    if (direction > 0 && app->workflow_node + 1 < n) app->workflow_node++;
    if (direction < 0 && app->workflow_node) app->workflow_node--;
    app->graph_follow = true;
}

bool workflow_key(struct app *app, char key) {
    if (key == 'j' || key == 'k') workflow_move(app, key == 'j' ? 1 : -1);
    else if (key == '[' || key == ']') {
        if (app->workflows && app->workflows->run_count) {
            size_t n = app->workflows->run_count;
            if (n > 1) app->notice[0] = '\0';
            app->workflow_run = (app->workflow_run + (key == ']' ? 1 : n - 1)) % n;
            app->workflow_node = 0; app->graph_x = 0; app->graph_y = 0; app->graph_follow = true;
        }
    } else if (key == 'h') { app->graph_x = app->graph_x > 8 ? app->graph_x - 8 : 0; app->graph_follow = false; }
    else if (key == 'l') { if (app->graph_x < 3500) app->graph_x += 8; app->graph_follow = false; }
    else if (key == 'J') { if (app->graph_y < 650) app->graph_y += 4; app->graph_follow = false; }
    else if (key == 'K') { app->graph_y = app->graph_y > 4 ? app->graph_y - 4 : 0; app->graph_follow = false; }
    else if (key == '\r' || key == '\n') app->graph_follow = true;
    else if (strchr("/:pda cAxG", key)) copy_text(app->notice, sizeof(app->notice), "Workflow view is read-only; j/k nodes, [/] runs, h/l/J/K pan");
    else return false;
    return true;
}
