/* Focused pure-projection cases for src/fleet/attention.c. */
static void attention_string(json_object *object, const char *key, const char *value) {
    f_string_add(object, key, value);
}

static json_object *attention_task(size_t number, const char *state, const char *spec) {
    char task_id[70], run_id[32];
    json_object *task = json_object_new_object(), *waiting = json_object_new_object();
    json_object *steps = json_object_new_array(), *step_a = json_object_new_object();
    json_object *step_b = json_object_new_object(), *requests = json_object_new_array();
    json_object *collection = json_object_new_object();
    snprintf(task_id, sizeof(task_id), "task_%064zu", number);
    snprintf(run_id, sizeof(run_id), "run_%zu", number);
    attention_string(task, "task_id", task_id); attention_string(task, "run_id", run_id);
    attention_string(task, "step_id", "step_a"); attention_string(task, "attempt_id", "attempt-a");
    attention_string(task, "spec_sha256", spec); attention_string(task, "execution_state", state);
    attention_string(waiting, "reason", !strcmp(state, "waiting_approval") ? "approval" : "none");
    json_object_object_add(task, "waiting", waiting);
    attention_string(step_a, "step_id", "step_a"); attention_string(step_a, "attempt_id", "attempt-a");
    attention_string(step_b, "step_id", "step_b"); attention_string(step_b, "attempt_id", "attempt-b");
    json_object_array_add(steps, step_a); json_object_array_add(steps, step_b);
    json_object_object_add(task, "steps", steps); json_object_object_add(task, "pending_requests", requests);
    attention_string(collection, "state", "ready"); json_object_object_add(task, "result_collection", collection);
    return task;
}

static json_object *attention_aggregate(json_object *tasks, bool reverse_requests, int mutation) {
    json_object *aggregate = json_object_new_object(), *data = json_object_new_object();
    json_object *hosts = json_object_new_array(), *host = json_object_new_object();
    json_object *host_data = json_object_new_object(), *freshness = json_object_new_object();
    json_object *task = attention_task(0, "waiting_approval", mutation == 2 ?
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" :
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    json_object *requests = f_field(task, "pending_requests"), *request;
    json_object *cancellation, *artifact;
    attention_string(host, "host", "builder"); json_object_object_add(host, "ok", json_object_new_boolean(true));
    attention_string(freshness, "state", "fresh"); json_object_object_add(host_data, "freshness", freshness);
    attention_string(host_data, "receiver_observed_at", reverse_requests ? "2" : "1");
    attention_string(host_data, "last_confirmed_at", reverse_requests ? "2" : "1");
    cancellation = json_object_new_object();
    if (reverse_requests) {
        attention_string(cancellation, "scope", "task"); attention_string(cancellation, "state", "none");
    } else {
        attention_string(cancellation, "state", "none"); attention_string(cancellation, "scope", "task");
    }
    json_object_object_add(task, "cancellation", cancellation);
    if (mutation == 1) attention_string(json_object_array_get_idx(f_field(task, "steps"), 0), "attempt_id", "attempt-a2");
    request = json_object_new_object(); attention_string(request, "request_id", "request_a");
    attention_string(request, "step_id", "step_a"); attention_string(request, "state", "pending");
    attention_string(request, "message", "approve A"); attention_string(request, "expires_at", "4102444800");
    if (reverse_requests) {
        json_object_array_add(requests, request);
    } else json_object_array_add(requests, request);
    request = json_object_new_object(); attention_string(request, "request_id", "request_b");
    attention_string(request, "step_id", "step_b"); attention_string(request, "state", "pending");
    attention_string(request, "message", "approve B"); attention_string(request, "expires_at", "4102444800");
    if (reverse_requests) {
        json_object_array_insert_idx(requests, 0, request);
    } else json_object_array_add(requests, request);
    /* Duplicate request exercises semantic de-duplication. */
    request = json_object_new_object(); attention_string(request, "request_id", "request_a");
    attention_string(request, "step_id", "step_a"); attention_string(request, "state", "pending");
    attention_string(request, "message", "approve A"); attention_string(request, "expires_at", "4102444800");
    json_object_array_add(requests, request);
    if (mutation == 3) {
        cancellation = json_object_new_object(); attention_string(cancellation, "state", "requested");
        attention_string(cancellation, "scope", "task"); json_object_object_add(task, "cancellation", cancellation);
    } else if (mutation == 4) {
        artifact = json_object_new_object(); attention_string(artifact, "count", "2");
        json_object_object_add(task, "artifact_inventory", artifact);
    }
    if (json_object_array_length(tasks) == 0) json_object_array_add(tasks, task); else json_object_put(task);
    json_object_object_add(host_data, "tasks", tasks); json_object_object_add(host_data, "cached", json_object_new_boolean(false));
    json_object_object_add(host, "data", host_data); json_object_array_add(hosts, host);
    json_object_object_add(data, "hosts", hosts); json_object_object_add(aggregate, "data", data);
    return aggregate;
}

static json_object *attention_items(json_object *result) { return f_field(f_field(result, "data"), "items"); }
static json_object *attention_request_item(json_object *items, const char *request_id) {
    size_t i;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        if (!strcmp(f_string(item, "request_id") ? f_string(item, "request_id") : "", request_id)) return item;
    }
    return NULL;
}
static json_object *attention_task_item(json_object *items, const char *task_id) {
    size_t i;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        if (!strcmp(f_string(item, "task_id") ? f_string(item, "task_id") : "", task_id)) return item;
    }
    return NULL;
}
static void attention_compare_revisions(json_object *left, json_object *right) {
    json_object *a = attention_items(left), *b = attention_items(right); size_t i;
    assert(json_object_array_length(a) == json_object_array_length(b));
    for (i = 0; i < json_object_array_length(a); i++)
        assert(!strcmp(f_string(json_object_array_get_idx(a, i), "revision"),
                       f_string(json_object_array_get_idx(b, i), "revision")));
}

static void test_attention(void) {
    json_object *base = attention_aggregate(json_object_new_array(), false, 0);
    json_object *reordered = attention_aggregate(json_object_new_array(), true, 0);
    json_object *left = f_attention_aggregate(base), *right = f_attention_aggregate(reordered);
    json_object *items = attention_items(left);
    size_t i;
    assert(json_object_get_boolean(f_field(left, "ok")));
    assert(json_object_array_length(items) == 3);
    assert(!strcmp(f_string(json_object_array_get_idx(items, 0), "kind"), "approval") ||
           !strcmp(f_string(json_object_array_get_idx(items, 1), "kind"), "approval"));
    assert(!strcmp(f_string(json_object_array_get_idx(items, 2), "kind"), "result"));
    assert(!strcmp(f_string(attention_request_item(items, "request_a"), "attempt_id"), "attempt-a"));
    assert(!strcmp(f_string(attention_request_item(items, "request_b"), "attempt_id"), "attempt-b"));
    attention_compare_revisions(left, right);
    for (i = 1; i <= 4; i++) {
        json_object *changed = attention_aggregate(json_object_new_array(), false, (int)i);
        json_object *changed_result = f_attention_aggregate(changed);
        assert(strcmp(f_string(attention_request_item(items, "request_a"), "revision"),
                      f_string(attention_request_item(attention_items(changed_result), "request_a"), "revision")));
        json_object_put(changed_result); json_object_put(changed);
    }
    json_object_put(left); json_object_put(right); json_object_put(base); json_object_put(reordered);
}

static void test_attention_unknowns(void) {
    json_object *aggregate = attention_aggregate(json_object_new_array(), false, 0), *data = f_field(aggregate, "data");
    json_object *tasks = f_field(f_field(json_object_array_get_idx(f_field(data, "hosts"), 0), "data"), "tasks");
    json_object *task = attention_task(20, "waiting_approval", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    json_object *steps = f_field(task, "steps"), *requests = f_field(task, "pending_requests"), *request = json_object_new_object();
    json_object *result; char missing_id[70], unrelated_id[70];
    snprintf(missing_id, sizeof(missing_id), "task_%064d", 22);
    snprintf(unrelated_id, sizeof(unrelated_id), "task_%064d", 23);
    json_object_array_add(steps, json_object_get(json_object_array_get_idx(steps, 0)));
    attention_string(request, "request_id", "request_ambiguous"); attention_string(request, "step_id", "step_a");
    attention_string(request, "state", "pending"); attention_string(request, "expires_at", "4102444800");
    json_object_array_add(requests, request); json_object_array_add(tasks, task);
    task = attention_task(24, "waiting_approval", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    request = json_object_new_object(); attention_string(request, "request_id", "request_invalid_state");
    attention_string(request, "step_id", "step_a"); attention_string(request, "state", "approved");
    json_object_array_add(f_field(task, "pending_requests"), request); json_object_array_add(tasks, task);
    task = attention_task(21, "waiting_approval", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    json_object_object_del(json_object_array_get_idx(f_field(task, "steps"), 0), "attempt_id");
    request = json_object_new_object(); attention_string(request, "request_id", "request_missing_attempt");
    attention_string(request, "step_id", "step_a"); attention_string(request, "state", "pending"); attention_string(request, "expires_at", "4102444800");
    json_object_array_add(f_field(task, "pending_requests"), request); json_object_array_add(tasks, task);
    task = attention_task(22, "succeeded", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    json_object_object_del(task, "spec_sha256");
    json_object_array_add(tasks, task);
    task = attention_task(23, "running", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    attention_string(f_field(task, "waiting"), "reason", "dependency");
    json_object_array_add(tasks, task);
    result = f_attention_aggregate(aggregate); tasks = attention_items(result);
    assert(!strcmp(f_string(attention_request_item(tasks, "request_ambiguous"), "reason"), "approval_binding_unknown"));
    assert(!strcmp(f_string(attention_request_item(tasks, "request_missing_attempt"), "reason"), "approval_binding_unknown"));
    assert(!strcmp(f_string(attention_request_item(tasks, "request_invalid_state"), "kind"), "unknown"));
    assert(!strcmp(f_string(attention_request_item(tasks, "request_invalid_state"), "reason"), "malformed_request_state"));
    assert(!json_object_get_boolean(f_field(attention_request_item(tasks, "request_invalid_state"), "accepted")));
    assert(!strcmp(f_string(attention_task_item(tasks, missing_id), "reason"), "missing_identity"));
    assert(!strcmp(f_string(attention_task_item(tasks, unrelated_id), "kind"), "result"));
    assert(json_object_array_length(tasks) >= 6); json_object_put(result); json_object_put(aggregate);
}

static void test_attention_cap(void) {
    json_object *tasks = json_object_new_array(), *aggregate, *result, *items; size_t i;
    for (i = 0; i < 512; i++) json_object_array_add(tasks, attention_task(i, "succeeded", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    json_object_array_add(tasks, json_object_get(json_object_array_get_idx(tasks, 0)));
    aggregate = attention_aggregate(tasks, false, 0);
    result = f_attention_aggregate(aggregate); items = attention_items(result);
    assert(json_object_array_length(items) == 512); assert(!json_object_get_boolean(f_field(f_field(result, "data"), "truncated")));
    json_object_array_add(tasks, attention_task(512, "succeeded", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    json_object_put(result); result = f_attention_aggregate(aggregate); items = attention_items(result);
    assert(json_object_array_length(items) == 512); assert(json_object_get_boolean(f_field(f_field(result, "data"), "truncated")));
    assert(json_object_get_boolean(f_field(f_field(result, "data"), "partial")));
    json_object_put(result); json_object_put(aggregate);
}
