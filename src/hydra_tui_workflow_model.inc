/* Strict, bounded read model. Dependency semantics remain recorded evidence. */
static bool workflow_id(const char *s) {
    size_t i;
    if (!s[0] || strlen(s) > 79) return false;
    for (i = 0; s[i]; i++) if (!((s[i] >= 'a' && s[i] <= 'z') ||
        (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-')) return false;
    return true;
}

static size_t workflow_nodes(const struct workflow_model *m, size_t run, size_t *indices) {
    size_t i, n = 0;
    for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run) {
        if (n == TV_GRAPH_MAX_NODES) return n + 1;
        indices[n++] = i;
    }
    return n;
}

static bool workflow_edges(const struct workflow_model *m, const size_t *indices, size_t n,
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

static int load_workflows(FILE *input, struct workflow_model *m) {
    char line[2048];
    size_t bytes = 0, i;
    bool handshake = false;
    memset(m, 0, sizeof(*m));
    while (fgets(line, sizeof(line), input)) {
        char *fields[8];
        size_t length = strlen(line), count;
        if (!length || line[length - 1] != '\n' || (bytes += length) > MAX_DATA_BYTES) return -1;
        line[length - 1] = '\0';
        count = split_fields(line, fields, 8);
        if (!handshake) {
            if (count != 2 || strcmp(fields[0], "HYDRA_WORKFLOW_TUI") || strcmp(fields[1], "1")) return -1;
            handshake = true; continue;
        }
        if (count == 2 && !strcmp(fields[0], "X")) copy_text(m->warning, sizeof(m->warning), fields[1]);
        else if (count == 4 && !strcmp(fields[0], "W")) {
            struct workflow_run *r;
            if (m->run_count == WF_RUNS || !workflow_id(fields[1]) || strlen(fields[2]) >= 80 || strlen(fields[3]) >= 40) return -1;
            for (i = 0; i < m->run_count; i++) if (!strcmp(m->runs[i].id, fields[1])) return -1;
            r = &m->runs[m->run_count++];
            copy_text(r->id, sizeof(r->id), fields[1]); copy_text(r->name, sizeof(r->name), fields[2]);
            copy_text(r->state, sizeof(r->state), fields[3]);
        } else if (count == 7 && !strcmp(fields[0], "N")) {
            struct workflow_node *n;
            size_t run;
            for (run = 0; run < m->run_count; run++) if (!strcmp(m->runs[run].id, fields[1])) break;
            if (run == m->run_count || m->node_count == WF_NODES || !workflow_id(fields[2]) ||
                strlen(fields[2]) >= 65 || strlen(fields[3]) >= 32 || strlen(fields[4]) >= 40 ||
                strlen(fields[6]) >= 1024 || !fields[6][0]) return -1;
            for (i = 0; i < m->node_count; i++) if (m->nodes[i].run == run && !strcmp(m->nodes[i].id, fields[2])) return -1;
            n = &m->nodes[m->node_count++]; n->run = run;
            if (!parse_unsigned(fields[5], &n->attempts)) return -1;
            copy_text(n->id, sizeof(n->id), fields[2]); copy_text(n->kind, sizeof(n->kind), fields[3]);
            copy_text(n->state, sizeof(n->state), fields[4]); copy_text(n->needs, sizeof(n->needs), fields[6]);
        } else return -1;
    }
    for (i = 0; i < m->run_count; i++) {
        size_t indices[TV_GRAPH_MAX_NODES], count, n = workflow_nodes(m, i, indices);
        struct tv_edge edges[TV_GRAPH_MAX_EDGES]; struct tv_graph_layout layout;
        if (n > TV_GRAPH_MAX_NODES || !workflow_edges(m, indices, n, edges, &count) ||
            !tv_graph_layout(edges, count, n, &layout)) return -1;
    }
    return handshake && !ferror(input) ? 0 : -1;
}

static int refresh_workflows(struct app *app, const char *fixture) {
    FILE *input;
    struct workflow_model *next;
    char notice[TEXT];
    copy_text(notice, sizeof(notice), app->notice);
    input = fixture ? fopen(fixture, "r") : capture_adapter(app, "workflow", "tui-data", 2000);
    copy_text(app->notice, sizeof(app->notice), notice);
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

static void workflow_move(struct app *app, int direction) {
    size_t indices[TV_GRAPH_MAX_NODES], n;
    if (!app->workflows) return;
    n = workflow_nodes(app->workflows, app->workflow_run, indices);
    if (direction > 0 && app->workflow_node + 1 < n) app->workflow_node++;
    if (direction < 0 && app->workflow_node) app->workflow_node--;
    app->graph_follow = true;
}

static bool workflow_key(struct app *app, char key) {
    if (key == 'j' || key == 'k') workflow_move(app, key == 'j' ? 1 : -1);
    else if (key == '[' || key == ']') {
        if (app->workflows && app->workflows->run_count) {
            size_t n = app->workflows->run_count;
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
