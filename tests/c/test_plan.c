#include "fleet/plan.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *f_home, *f_hydra;
static json_object *fixture(void) { return plan_read("tests/fixtures/plan/plan.json"); }
static void expect(json_object *plan, json_object *policy, const char *code) {
    json_object *errors = json_object_new_array(); size_t i; bool found = false;
    int status = plan_validate(plan, policy, errors);
    if (!code) assert(status == 0 && json_object_array_length(errors) == 0);
    else {
        assert(status != 0);
        for (i = 0; i < json_object_array_length(errors); i++) if (!strcmp(f_string(json_object_array_get_idx(errors, i), "code"), code)) found = true;
        if (!found) fprintf(stderr, "%s\n", json_object_to_json_string(errors));
        assert(found);
    }
    json_object_put(errors);
}
static void graph_cases(void) {
    json_object *plan = fixture(), *policy = plan_read("tests/fixtures/plan/policy.json"), *steps, *compose, *worker;
    assert(plan && policy); expect(plan, policy, NULL);
    json_object_object_add(f_field(policy, "envelope"), "disk_mb", json_object_new_int(2));
    expect(plan, policy, "unauthorized");
    json_object_object_add(f_field(plan, "envelope"), "disk_mb", json_object_new_int(3));
    expect(plan, policy, NULL);
    json_object_object_add(f_field(policy, "envelope"), "disk_mb", json_object_new_int(1));
    steps = f_field(plan, "steps");
    json_object_object_add(json_object_array_get_idx(steps, 0), "needs", f_parse_value("[\"verify\"]"));
    expect(plan, policy, "cycle"); json_object_put(plan); plan = fixture(); steps = f_field(plan, "steps");
    compose = json_object_array_get_idx(steps, 1); worker = plan_canonical(compose);
    f_string_add(worker, "id", "parallel"); f_string_add(worker, "role", "work");
    json_object_object_add(compose, "writes", f_parse_value("[\"plan-smoke:shared\"]"));
    json_object_object_add(worker, "writes", f_parse_value("[\"plan-smoke:shared/subdir\"]"));
    json_object_object_add(f_field(plan, "envelope"), "writes", f_parse_value("[\"plan-smoke:*\"]"));
    json_object_object_add(f_field(policy, "envelope"), "writes", f_parse_value("[\"plan-smoke:*\"]"));
    json_object_array_add(steps, worker); expect(plan, policy, "write_conflict");
    json_object_object_add(compose, "needs", f_parse_value("[\"spawn\",\"parallel\"]")); expect(plan, policy, NULL);
    json_object_object_add(f_field(policy, "envelope"), "writes", f_parse_value("[\"other-head:*\"]")); expect(plan, policy, "unauthorized");
    json_object_put(plan); json_object_put(policy);
}
static void parse_cases(const char *root) {
    const char *bad[] = {"{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}", "{\"nested\":{\"a\":0,\"a\":0}}", "{", "{\"a\"", "{\"a\":", "{\"a\":[]", "{\"a\":1} trailing", "{\"a\":NaN}", "{\"a\":1e999}", "{\"a\":\"\\u0000\"}", NULL};
    char path[F_PATH], a[65], b[65]; json_object *first, *second; size_t i;
    assert(!f_path(path, sizeof(path), root, "input.json"));
    for (i = 0; bad[i]; i++) {
        assert(!f_write(path, bad[i], strlen(bad[i]), true)); first = plan_read(path);
        /* NUL is valid JSON string data but never a valid contract string. */
        if (i == 10) { assert(first && !plan_text(f_field(first, "a"))); json_object_put(first); }
        else assert(!first);
    }
    assert(!f_write(path, "{}\0junk", 7, true)); assert(!plan_read(path));
    first = f_parse("{\"b\":{\"z\":1,\"a\":2},\"a\":[1,2]}"); second = f_parse("{\"a\":[1,2],\"b\":{\"a\":2,\"z\":1}}");
    assert(!plan_digest(first, a) && !plan_digest(second, b) && !strcmp(a, b));
    json_object_put(first); json_object_put(second);
    {
        char *large = malloc(PLAN_LIMIT + 2); assert(large); memset(large, ' ', PLAN_LIMIT + 1); large[0] = '{'; large[1] = '}';
        assert(!f_write(path, large, PLAN_LIMIT + 1, true)); assert(!plan_read(path)); free(large);
    }
}
int main(void) {
    char root[] = "/tmp/hydra-plan-unit.XXXXXX";
    assert(mkdtemp(root)); f_home = root; f_hydra = "hydra";
    graph_cases(); parse_cases(root); assert(!f_remove_tree(root));
    puts("planning graph, policy, canonical JSON and parser checks passed"); return 0;
}
