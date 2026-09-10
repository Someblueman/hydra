#include "fixture.h"

static void headless(json_object *plan) {
    json_object *steps = fx_field(plan, "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (!strcmp(f_string(step, "kind"), "spawn"))
            f_string_add(fx_field(step, "args"), "terminal_mode", "headless");
    }
}
static void adapter(const char *root, const char *fixture) {
    char path[4096];
    fx_path(path, root, "tests/fixtures/plan/plan.json");
    json_object *plan = fx_read(path);
    fx_path(path, root, "tests/fixtures/plan/policy.json");
    json_object *policy = fx_read(path);
    json_object *objects[] = {plan, policy};
    for (size_t i = 0; i < 2; i++)
        json_object_array_add(fx_field(fx_field(objects[i], "envelope"), "tools"),
                              json_object_new_string("profile:fixture"));
    headless(plan);
    json_object *steps = fx_field(plan, "steps");
    json_object_object_add(
        json_object_array_get_idx(steps, 1), "args",
        f_parse("{\"head\":\"plan-smoke\",\"profile\":\"fixture\",\"prompt_input\":\"prompt\","
                "\"result_file\":\"report\",\"timeout\":30}"));
    json_object *data = fx_field(plan, "data");
    json_object_object_add(
        fx_field(data, "inputs"), "prompt",
        f_parse("{\"path\":\"prompt.txt\",\"type\":\"file\",\"max_bytes\":128}"));
    json_object *compose = fx_field(fx_field(data, "steps"), "compose");
    json_object_object_add(compose, "inputs", f_parse("{\"prompt\":{\"input\":\"prompt\"}}"));
    f_string_add(fx_field(fx_field(compose, "outputs"), "report"), "path", "report");
    fx_path(path, fixture, "plan.json");
    fx_save(path, plan);
    fx_path(path, fixture, "policy.json");
    fx_save(path, policy);
    json_object_put(plan);
    json_object_put(policy);
}
int fx_plan(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "adapter-plan")) {
        adapter(argv[2], argv[3]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "headless-plan")) {
        json_object *plan = fx_read(argv[2]);
        headless(plan);
        /* The shell's negative cases mutate the original indented fixture. */
        const char *text = json_object_to_json_string_ext(
            plan, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
        fx_require(text != NULL, argv[2]);
        fx_require(!f_write(argv[2], text, strlen(text), true), argv[2]);
        json_object_put(plan);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "merge-obligations")) {
        json_object *plan = fx_read(argv[2]);
        json_object_object_add(plan, "obligations", fx_read(argv[3]));
        if (!strcmp(argv[5], "performance"))
            f_string_add(plan, "objective", "Improve performance while preserving text content");
        if (!strcmp(argv[5], "research"))
            f_string_add(plan, "objective", "Report the observed result and its limitations");
        fx_save(argv[4], plan);
        json_object_put(plan);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "terminal-request")) {
        json_object *request = f_parse("{\"protocol\":1,\"action\":\"task\",\"operation\":"
                                       "\"submit\",\"submission_key\":\"terminal-direct\"}");
        json_object_object_add(request, "package", fx_read(argv[2]));
        f_emit(request);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "terminal-spec")) {
        json_object *spec = fx_read(argv[2]);
        json_object_object_add(spec, "capabilities", f_parse_value("[\"exec\",\"tmux\"]"));
        json_object *args = f_parse_value("[\"sh\",\"-c\"]");
        json_object_array_add(args,
                              json_object_new_string(
                                  "printf \"executed\\n\" >> \"$HYDRA_HOME/terminal-executions\""));
        json_object_object_add(fx_field(spec, "work"), "argv", args);
        fx_save(argv[3], spec);
        json_object_put(spec);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "verdict")) {
        json_object *value = fx_read(argv[2]);
        const char *verdict = f_string(value, "verdict");
        fx_require(verdict != NULL, "verdict");
        puts(verdict);
        json_object_put(value);
        return 0;
    }
    return 1;
}
