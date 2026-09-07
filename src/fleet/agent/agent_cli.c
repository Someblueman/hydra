#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/agent/agent.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

json_object *agent_profile_cli(int argc, char **argv) {
    json_object *profile = NULL, *result = NULL, *data = NULL; char digest[65];
    if (argc == 2 && !strcmp(argv[0], "run-id")) {
        json_object *receipt = f_read_json(argv[1], AGENT_OUTPUT_LIMIT);
        const char *command = f_string(receipt, "command"), *run = f_string(f_field(receipt, "data"), "run_id");
        json_object *results = f_field(f_field(receipt, "data"), "results");
        bool valid = f_number_is(receipt, "schema_version", 1) && json_object_get_boolean(f_field(receipt, "ok")) && command && !strcmp(command, "exec") &&
            run && !strncmp(run, "run_", 4) && f_name(run) && json_object_is_type(results, json_type_array) && json_object_array_length(results) == 1 &&
            f_number_is(json_object_array_get_idx(results, 0), "exit_code", 0);
        if (valid) puts(run);
        json_object_put(receipt);
        if (valid) return NULL;
        goto invalid;
    }
    if (argc == 3 && !strcmp(argv[0], "import")) {
        char root[F_PATH], directory[F_PATH], path[F_PATH]; json_object *input = f_read_json(argv[2], AGENT_PROMPT_LIMIT);
        profile = agent_profile_validate(input); json_object_put(input);
        if (!profile || !agent_name(argv[1]) || agent_builtin(argv[1])) goto invalid;
        if (f_path(root, sizeof(root), f_home, "profiles") || f_mkdirs(root) || f_path(directory, sizeof(directory), root, argv[1]) || mkdir(directory, 0700)) goto io;
        if (f_path(path, sizeof(path), directory, "adapter.json") || f_write(path, json_object_to_json_string_ext(profile, JSON_C_TO_STRING_PLAIN),
            strlen(json_object_to_json_string_ext(profile, JSON_C_TO_STRING_PLAIN)), false)) { rmdir(directory); goto io; }
        data = json_object_new_object(); f_string_add(data, "profile", argv[1]); f_string_add(data, "file", path);
        result = f_success("agent-import", data); goto done;
    }
    if (argc != 2 || (strcmp(argv[0], "contract") && strcmp(argv[0], "probe"))) goto invalid;
    profile = agent_profile(argv[1]);
    if (!profile || agent_profile_hash(profile, digest)) goto invalid;
    data = json_object_new_object(); f_string_add(data, "profile", argv[1]); f_string_add(data, "profile_sha256", digest);
    json_object_object_add(data, "contract", json_object_get(profile));
    if (!strcmp(argv[0], "probe")) json_object_object_add(data, "evidence", agent_probe(profile));
    else json_object_object_add(data, "declared", agent_capabilities(profile));
    result = f_success("agent-contract", data); goto done;
invalid:
    result = f_error("agent-contract", "invalid_profile", "use agent contract|probe PROFILE or agent import NAME FILE; legacy launch profiles do not imply headless capabilities"); goto done;
io:
    result = f_error("agent-import", "io_failed", "cannot create a new private profile; existing profiles and built-in names are never replaced");
done:
    json_object_put(profile); return result;
}
