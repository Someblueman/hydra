#define _XOPEN_SOURCE 700
#include "fixture.h"
#include <sys/utsname.h>

static void environment(void) {
    json_object *env = f_parse(
        "{\"schema_version\":1,\"scope\":\"fixed local text fixture; frozen historical "
        "outputs\",\"dependencies\":[\"bound source scripts\",\"declared inputs\",\"POSIX shell "
        "and text tools\"],\"external_observations\":false,\"effects\":\"artifact_only\"}");
    struct utsname platform;
    fx_require(uname(&platform) == 0, "uname");
    json_object *system = json_object_new_object(), *tools = json_object_new_object(),
                *variables = json_object_new_object();
    f_string_add(system, "system", platform.sysname);
    f_string_add(system, "release", platform.release);
    f_string_add(system, "machine", platform.machine);
    const char *names[] = {"sh", "cat", "cmp", "sed", "cut", "shasum", "sha256sum", "perl"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        char *args[] = {"sh", "-c", "command -v \"$1\"", "fixture", (char *)names[i], NULL};
        struct f_capture cap = {0};
        int status = f_run(args, NULL, 0, 5, &cap);
        if (!status && cap.status == 0 && cap.out) {
            cap.out[strcspn(cap.out, "\r\n")] = 0;
            char *resolved = realpath(cap.out, NULL), digest[65];
            fx_require(resolved != NULL && !f_hash(resolved, digest), "tool source hash");
            json_object *tool = json_object_new_object();
            f_string_add(tool, "path", resolved);
            f_string_add(tool, "sha256", digest);
            json_object_object_add(tools, names[i], tool);
            free(resolved);
        }
        f_capture_free(&cap);
    }
    const char *keys[] = {"PATH", "LANG", "LC_ALL", "LC_CTYPE", "TZ"};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        const char *value = !strcmp(keys[i], "LC_ALL") ? "C" : getenv(keys[i]);
        f_string_add(variables, keys[i], value ? value : "");
    }
    json_object_object_add(env, "platform", system);
    json_object_object_add(env, "tools", tools);
    json_object_object_add(env, "variables", variables);
    fx_save("environment.json", env);
    json_object_put(env);
}
static void setup(const char *path) {
    json_object *plan = fx_read(path),
                *policy =
                    f_parse("{\"schema_version\":1,\"mode\":\"sealed_artifacts\",\"steps\":{}}");
    json_object_object_add(plan, "reuse_policy", policy);
    json_object *data = fx_field(plan, "data");
    json_object_object_add(
        fx_field(data, "inputs"), "environment",
        f_parse("{\"path\":\"environment.json\",\"type\":\"object\",\"max_bytes\":4096}"));
    const char *steps[] = {"produce", "inspect"};
    for (size_t i = 0; i < 2; i++) {
        json_object_object_add(fx_field(policy, "steps"), steps[i],
                               f_parse("{\"dependencies\":\"complete\",\"effects\":\"artifact_"
                                       "only\",\"environment_input\":\"environment\"}"));
        json_object *inputs = fx_field(fx_field(fx_field(data, "steps"), steps[i]), "inputs");
        json_object_object_del(inputs, "repair");
        json_object_object_add(inputs, "environment", f_parse("{\"input\":\"environment\"}"));
    }
    fx_save(path, plan);
    json_object_put(plan);
    environment();
}
static long long scalar(const char *directory, const char *name) {
    char path[4096];
    fx_path(path, directory, name);
    char *text = f_read(path, 64), *end = NULL;
    fx_require(text && *text, name);
    long long result = strtoll(text, &end, 10);
    fx_require(end && (*end == 0 || (*end == '\n' && !end[1])) && result >= 0, name);
    free(text);
    return result;
}
static void check(const char *path, const char *run) {
    json_object *record = fx_read(path),
                *kept = fx_field(fx_field(fx_field(record, "evidence"), "reuse"), "steps");
    fx_require(json_object_object_length(kept) == 2, "two kept steps");
    const char *names[] = {"produce", "inspect", "compose", "verify"};
    for (size_t i = 0; i < 2; i++) {
        json_object *step = fx_field(kept, names[i]);
        fx_require(f_string(step, "attempt") && !strcmp(f_string(step, "attempt"), "attempt-1"),
                   "original attempt");
        fx_require(f_field(fx_field(step, "inputs"), "environment") != NULL,
                   "environment evidence");
    }
    for (size_t i = 0; i < 4; i++) {
        char steps[4096], step[4096], attempt[4096], leaf[64];
        fx_path(steps, run, "steps");
        fx_path(step, steps, names[i]);
        long long ready = scalar(step, "initial-ready-at"),
                  first = scalar(step, "initial-started-at"),
                  number = scalar(step, "authoritative-attempt");
        int n = snprintf(leaf, sizeof leaf, "attempt-%lld", number);
        fx_require(n > 0 && (size_t)n < sizeof leaf, "attempt path");
        fx_path(attempt, step, leaf);
        long long start = scalar(attempt, "started-at"), end = scalar(attempt, "completed-at");
        fx_require(ready <= first && first <= start && start <= end, "attempt timing order");
    }
    json_object_put(record);
}
int fx_reuse(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "reuse-setup")) {
        setup(argv[2]);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "reuse-check")) {
        check(argv[2], argv[3]);
        return 0;
    }
    return 1;
}
