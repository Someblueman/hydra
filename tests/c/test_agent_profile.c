#include "fleet/agent.h"
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *f_home, *f_hydra;
static void retention(const char *root) {
    struct f_capture capture = {0}; char run[32], path[F_PATH]; size_t i, count = 0; struct stat st;
    capture.out = calloc(AGENT_OUTPUT_LIMIT + 7, 1); assert(capture.out); capture.out_bytes = AGENT_OUTPUT_LIMIT + 7;
    for (i = 0; i < 11; i++) { snprintf(run, sizeof(run), "run_%zu", i); assert(!agent_retain(root, run, &capture)); }
    assert(!f_path(path, sizeof(path), root, "agent-payloads")); DIR *dir = opendir(path); assert(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, "run_", 4)) count++;
    closedir(dir); assert(count == 10);
    assert(!f_path(path, sizeof(path), root, "agent-payloads/run_10/stdout")); assert(!stat(path, &st)); assert(st.st_size == AGENT_OUTPUT_LIMIT);
    f_capture_free(&capture);
}
static void decoders(void) {
    const char *names[] = {"canonical-jsonl", "codex-jsonl", "claude-jsonl", "pi-jsonl", "opencode-jsonl", "agy-jsonl", "cursor-jsonl", NULL}; size_t i;
    for (i = 0; names[i]; i++) {
        char path[F_PATH], line[32770]; size_t sessions = 0, answers = 0, usages = 0;
        assert(snprintf(path, sizeof(path), "tests/fixtures/agents/%s.jsonl", names[i]) < (int)sizeof(path));
        FILE *input = fopen(path, "r"); assert(input);
        while (fgets(line, sizeof(line), input)) {
            json_object *value = f_parse(line); struct agent_event event;
            assert(agent_decode(names[i], value, &event) == 1);
            if (event.session) { assert(!strcmp(event.session, "fixture-session")); sessions++; }
            if (event.text) { assert(!strcmp(event.text, "fixture artifact")); answers++; }
            if (event.usage) { assert(f_number_is(event.usage, "input_tokens", 17)); assert(f_number_is(event.usage, "output_tokens", 3)); usages++; }
            json_object_put(event.usage); json_object_put(value);
        }
        assert(!ferror(input)); fclose(input); assert(sessions && answers == 1 && usages == (strcmp(names[i], "cursor-jsonl") ? 1u : 0u));
    }
    const char *invalid[] = {"{}", "{\"schema_version\":1,\"type\":\"outcome\",\"status\":\"done\"}",
        "{\"schema_version\":1,\"type\":\"usage\",\"usage\":{\"input_tokens\":-1}}",
        "{\"schema_version\":1,\"type\":\"observation\",\"status\":\"verified\"}", NULL};
    for (i = 0; invalid[i]; i++) {
        json_object *value = f_parse(invalid[i]); struct agent_event event;
        assert(agent_decode("canonical-jsonl", value, &event) == -1); json_object_put(event.usage); json_object_put(value);
    }
}
static void provider_failures(void) {
    const struct { const char *adapter, *json; int decoded; } cases[] = {
        {"agy-jsonl", "{\"event\":\"step_update\",\"step_update\":{\"text_delta\":\"partial\"}}", 0},
        {"agy-jsonl", "{\"event\":\"result\",\"result\":{\"status\":\"SUCCESS\"}}", -1},
        {"agy-jsonl", "{\"event\":\"result\",\"result\":{\"status\":\"unexpected\"}}", -1},
        {"agy-jsonl", "{\"event\":\"init\",\"conversation_id\":\"\"}", -1},
        {"cursor-jsonl", "{\"type\":\"assistant\",\"message\":{\"content\":[]}}", 0},
        {"cursor-jsonl", "{\"type\":\"result\",\"is_error\":\"false\"}", -1},
        {"cursor-jsonl", "{\"type\":\"result\",\"is_error\":false,\"session_id\":\"recorded\"}", -1},
        {"claude-jsonl", "{\"type\":\"result\"}", -1},
        {"claude-jsonl", "{\"type\":\"result\",\"is_error\":false}", -1},
        {"claude-jsonl", "{\"type\":\"result\",\"is_error\":\"false\"}", -1},
        {NULL, NULL, 0}
    };
    const char *statuses[] = {"ERROR", "CANCELED", "INTERRUPTED", "INVALID", "WAITING", "RUNNING", NULL};
    size_t i; struct agent_event event;
    for (i = 0; cases[i].adapter; i++) {
        json_object *input = f_parse(cases[i].json);
        assert(agent_decode(cases[i].adapter, input, &event) == cases[i].decoded);
        assert(!event.text); json_object_put(event.usage); json_object_put(input);
    }
    for (i = 0; statuses[i]; i++) {
        json_object *input = f_parse("{\"event\":\"result\",\"result\":{}}");
        f_string_add(f_field(input, "result"), "status", statuses[i]);
        assert(agent_decode("agy-jsonl", input, &event) == 1);
        assert(!strcmp(event.status, "failed")); assert(!event.session);
        assert(!f_field(event.usage, "input_tokens"));
        json_object_put(event.usage); json_object_put(input);
    }
    json_object *input = f_parse("{\"type\":\"result\",\"is_error\":true,\"session_id\":\"\"}");
    assert(agent_decode("cursor-jsonl", input, &event) == 1);
    assert(!strcmp(event.status, "failed") && !event.usage && !event.session);
    json_object_put(input);
    input = f_parse("{\"type\":\"result\",\"is_error\":true}");
    assert(agent_decode("claude-jsonl", input, &event) == 1);
    assert(!strcmp(event.status, "failed") && !event.text);
    json_object_put(event.usage); json_object_put(input);
}
int main(void) {
    char root[] = "/tmp/hydra-agent-profile-test.XXXXXX", script[F_PATH], link[F_PATH], definition[F_PATH];
    json_object *profile, *input, *args, *values, *evidence, *response; size_t i;
    const char *names[] = {"claude", "codex", "pi", "opencode", "agy", "cursor", NULL};
    const char *bad[] = {
        "{\"input\":\"unknown\"}", "{\"input\":\"prompt\",\"expression\":\"x\"}", "1", "null", NULL
    };
    assert(mkdtemp(root)); f_home = root; f_hydra = "hydra";
    decoders();
    provider_failures();
    retention(root);
    for (i = 0; names[i]; i++) {
        profile = agent_profile(names[i]); assert(profile);
        assert(agent_capability(profile, "usage") == (strcmp(names[i], "cursor") != 0));
        assert(agent_capability(profile, "resume")); assert(!agent_capability(profile, "cost-limit"));
        json_object_put(profile);
    }
    assert(!agent_profile("../escape")); assert(!agent_profile("absent"));
    input = f_parse("{\"schema_version\":1,\"executable\":\"script\",\"argv\":[{\"input\":\"prompt\"}],\"prompt\":\"argument\",\"session\":\"none\",\"adapter\":\"none\",\"probe_argv\":[\"--help\"],\"probe_tokens\":[]}");
    profile = agent_profile_validate(input); assert(profile);
    assert(!agent_capability(profile, "resume")); assert(!agent_capability(profile, "usage"));
    values = f_parse("{\"prompt\":\"a; $(touch /tmp/never) ' \\n b\"}");
    args = agent_arguments(profile, "argv", values); assert(args && json_object_array_length(args) == 2);
    assert(!strcmp(task_text(json_object_array_get_idx(args, 1)), f_string(values, "prompt")));
    json_object_put(args); json_object_put(values); json_object_put(profile);
    for (i = 0; bad[i]; i++) {
        json_object *list = json_object_new_array(); json_object_array_add(list, f_parse_value(bad[i]));
        json_object_object_add(input, "argv", list); assert(!agent_profile_validate(input));
    }
    json_object_object_add(input, "argv", json_object_new_array());
    f_string_add(input, "prompt", "stdin");
    profile = agent_profile_validate(input); assert(profile); json_object_put(profile);
    json_object_object_add(input, "resume_argv", f_parse_value("[\"--last\"]")); assert(!agent_profile_validate(input));
    json_object_object_del(input, "resume_argv");
    assert(!f_path(script, sizeof(script), root, "dispatcher")); assert(!f_path(link, sizeof(link), root, "provider"));
    const char *body = "#!/bin/sh\ncase \"$0\" in */provider) echo provider-1.0;; *) exit 9;; esac\n";
    assert(!f_write(script, body, strlen(body), false)); assert(!chmod(script, 0700)); assert(!symlink(script, link));
    f_string_add(input, "executable", link);
    profile = agent_profile_validate(input); assert(profile);
    evidence = agent_probe(profile); assert(f_string(evidence, "executable_version"));
    assert(!strcmp(f_string(evidence, "executable_version"), "provider-1.0"));
    assert(json_object_get_boolean(f_field(f_field(evidence, "probed"), "invocation_help")));
    assert(!f_field(evidence, "observed")); json_object_put(evidence); json_object_put(profile);
    assert(!f_path(definition, sizeof(definition), root, "definition.json"));
    const char *encoded = json_object_to_json_string_ext(input, JSON_C_TO_STRING_PLAIN);
    assert(!f_write(definition, encoded, strlen(encoded), false));
    char *import[] = {"import", "fixture", definition};
    response = agent_profile_cli(3, import); assert(json_object_get_boolean(f_field(response, "ok"))); json_object_put(response);
    response = agent_profile_cli(3, import); assert(!json_object_get_boolean(f_field(response, "ok"))); json_object_put(response);
    import[1] = "codex";
    response = agent_profile_cli(3, import); assert(!json_object_get_boolean(f_field(response, "ok"))); json_object_put(response);
    profile = agent_profile("fixture"); assert(profile); json_object_put(profile);
    /* A name registered before this builtin existed retains its exact contract. */
    char old_profile[F_PATH], new_profile[F_PATH];
    assert(!f_path(old_profile, sizeof(old_profile), root, "profiles/fixture"));
    assert(!f_path(new_profile, sizeof(new_profile), root, "profiles/agy"));
    assert(!rename(old_profile, new_profile));
    profile = agent_profile("agy"); assert(profile);
    assert(!strcmp(f_string(profile, "executable"), link)); json_object_put(profile);
    assert(!f_path(definition, sizeof(definition), new_profile, "adapter.json"));
    assert(!unlink(definition));
    assert(!f_path(definition, sizeof(definition), new_profile, "executable"));
    assert(!f_write(definition, link, strlen(link), false));
    assert(!agent_profile("agy")); /* A legacy launch script is not this provider. */
    json_object_put(input); f_remove_tree(root);
    puts("Agent profiles: literal argv, transport validation, explicit resume, import isolation and shim-preserving probes passed");
    return 0;
}
