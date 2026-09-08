#include "fleet/workflow/workflow_schedule.h"
#include "fleet/support/json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const char *f_home, *f_hydra;
static void chosen(json_object *graph, json_object *observed, int parallelism, const char *expected) {
    char first[65], second[65];
    assert(!ws_decide(graph, observed, parallelism, first));
    assert(!ws_decide(graph, observed, parallelism, second));
    assert(!strcmp(first, expected) && !strcmp(first, second));
}
int main(void) {
    /* Deliberately reverse observation member order: graph order is authority. */
    json_object *graph = f_parse("{\"left\":{\"needs\":\"-\"},\"right\":{\"needs\":\"-\"},\"inspect\":{\"needs\":\"left,right\"},\"compose\":{\"needs\":\"left,right,inspect\"},\"independent\":{\"needs\":\"-\"}}");
    json_object *observed = f_parse("{\"states\":{\"compose\":\"queued\",\"inspect\":\"queued\",\"right\":\"ready\",\"left\":\"ready\",\"independent\":\"ready\"},\"cancelled\":false,\"observed_at\":100,\"deadline\":0}");
    json_object *states = f_field(observed, "states"); char ignored[65];
    chosen(graph, observed, 2, "left");
    f_string_add(states, "left", "running"); chosen(graph, observed, 2, "right"); chosen(graph, observed, 1, "");
    f_string_add(states, "right", "waiting-remote"); chosen(graph, observed, 2, "");
    f_string_add(states, "right", "succeeded"); f_string_add(states, "left", "succeeded"); chosen(graph, observed, 2, "independent");
    f_string_add(states, "independent", "succeeded"); f_string_add(states, "inspect", "ready"); chosen(graph, observed, 2, "inspect");
    f_string_add(states, "compose", "ready"); chosen(graph, observed, 2, "inspect");
    f_string_add(states, "inspect", "failed"); chosen(graph, observed, 2, "");
    f_string_add(states, "inspect", "succeeded"); chosen(graph, observed, 2, "compose");
    json_object_object_add(observed, "cancelled", json_object_new_boolean(true)); chosen(graph, observed, 2, "");
    json_object_object_add(observed, "cancelled", json_object_new_boolean(false));
    json_object_object_add(observed, "deadline", json_object_new_int(100)); chosen(graph, observed, 2, "");
    json_object_object_add(observed, "observed_at", json_object_new_int(99)); chosen(graph, observed, 2, "compose");
    assert(ws_decide(graph, observed, 17, ignored));
    f_string_add(states, "left", "unknown-state"); assert(ws_decide(graph, observed, 2, ignored));
    json_object_object_del(states, "left"); assert(ws_decide(graph, observed, 2, ignored));
    json_object_put(observed); json_object_put(graph);
    puts("scheduling replay: stable graph-order ties, capacity, evidence dependencies, unknown work and cancellation passed"); return 0;
}
