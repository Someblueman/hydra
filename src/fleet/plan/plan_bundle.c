#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Plans carry their own inputs. An exec step's inline prompt and the proposal
 * assets named by data inputs are lowered to generated bundle inputs whose
 * bytes are embedded in the compiled artifact and bound by its digest. Nothing
 * here reads or writes the source checkout. */

static size_t utf8_length(unsigned char lead, unsigned char *low, unsigned char *high) {
    *low = 0x80; *high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) {
        if (lead == 0xE0) *low = 0xA0;
        if (lead == 0xED) *high = 0x9F;
        return 3;
    }
    if (lead >= 0xF0 && lead <= 0xF4) {
        if (lead == 0xF0) *low = 0x90;
        if (lead == 0xF4) *high = 0x8F;
        return 4;
    }
    return 0;
}
static size_t utf8_sequence(const unsigned char *text, size_t available) {
    unsigned char low, high; size_t length, i;
    if (text[0] && text[0] < 0x80) return 1;
    length = utf8_length(text[0], &low, &high);
    if (!length || length > available || text[1] < low || text[1] > high) return 0;
    for (i = 2; i < length; i++) if (text[i] < 0x80 || text[i] > 0xBF) return 0;
    return length;
}
bool plan_utf8(const char *text, size_t length) {
    size_t at = 0;
    while (at < length) {
        size_t step = utf8_sequence((const unsigned char *)text + at, length - at);
        if (!step) return false;
        at += step;
    }
    return true;
}
/* Reads an already opened descriptor as a bounded regular UTF-8 text file. */
static json_object *text_file(int fd) {
    struct stat st; char *bytes = NULL; size_t length = 0; json_object *out = NULL;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > (off_t)PLAN_ASSET_LIMIT ||
        !(bytes = malloc(PLAN_ASSET_LIMIT + 1))) goto done;
    while (length <= PLAN_ASSET_LIMIT) {
        ssize_t n = read(fd, bytes + length, PLAN_ASSET_LIMIT + 1 - length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        length += (size_t)n;
    }
    if (length <= PLAN_ASSET_LIMIT && plan_utf8(bytes, length)) out = json_object_new_string_len(bytes, (int)length);
done:
    if (fd >= 0) close(fd);
    free(bytes); return out;
}
static json_object *asset_read(const char *directory, const char *name, bool *absent) {
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); json_object *text = NULL; struct stat st;
    *absent = dir < 0 && errno == ENOENT;
    if (dir < 0) return NULL;
    if (fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) && errno == ENOENT) *absent = true;
    else text = text_file(openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK));
    close(dir); return text;
}
static void asset_error(json_object *errors, const char *code, const char *message, const char *name) {
    size_t before = json_object_array_length(errors);
    plan_error(errors, "data.inputs", code, message);
    if (json_object_array_length(errors) > before) f_string_add(json_object_array_get_idx(errors, before), "asset", name);
}
json_object *plan_assets_load(json_object *plan, const char *directory, json_object *errors) {
    json_object *inputs = f_field(f_field(plan, "data"), "inputs"), *assets = json_object_new_object();
    if (!directory || !json_object_is_type(inputs, json_type_object)) return assets;
    json_object_object_foreach(inputs, input, declaration) {
        const char *name = f_string(declaration, "asset"); json_object *text; bool absent = false;
        (void)input;
        if (!plan_id(name) || f_field(assets, name)) continue;
        if ((text = asset_read(directory, name, &absent))) json_object_object_add(assets, name, text);
        else if (!absent) asset_error(errors, "invalid_asset", "assets must be regular UTF-8 text files of at most 64 KiB without NUL bytes; symlinks are refused", name);
    }
    return assets;
}
json_object *plan_proposal_asset(const char *name, const char *file, const char *directory) {
    char path[F_PATH]; json_object *text = NULL; const char *bytes; int length;
    if (!plan_id(name))
        return f_error("workflow plan propose", "invalid_asset", "asset names are plan IDs: lowercase letters, digits and single - or _ separators");
    text = text_file(open(file, O_RDONLY | O_NOFOLLOW | O_NONBLOCK));
    if (!text) return f_error("workflow plan propose", "invalid_asset", "each --asset must name a regular UTF-8 text file of at most 64 KiB without NUL bytes; symlinks are refused");
    bytes = json_object_get_string(text); length = json_object_get_string_len(text);
    if (f_path(path, sizeof(path), directory, name) || f_write(path, bytes, (size_t)length, false)) {
        json_object_put(text);
        return f_error("workflow plan propose", "invalid_asset", "asset names must be unique and the proposal must be writable");
    }
    json_object_put(text); return f_success("workflow plan propose", json_object_new_object());
}

static json_object *step_inputs(json_object *plan, const char *id) {
    json_object *steps = f_field(f_field(plan, "data"), "steps"), *step, *inputs;
    if (!json_object_is_type(steps, json_type_object)) return NULL;
    if (!(step = f_field(steps, id))) { step = json_object_new_object(); json_object_object_add(steps, id, step); }
    if (!json_object_is_type(step, json_type_object)) return NULL;
    if (!(inputs = f_field(step, "inputs"))) { inputs = json_object_new_object(); json_object_object_add(step, "inputs", inputs); }
    return json_object_is_type(inputs, json_type_object) ? inputs : NULL;
}
static void asset_input(json_object *plan, json_object *declaration, json_object *assets, json_object *files, json_object *errors) {
    const char *const keys[] = {"asset", "type", "max_bytes", NULL};
    const char *name = f_string(declaration, "asset"); json_object *text = f_field(assets, name); char path[80];
    if (!f_number_is(plan, "schema_version", 1)) {
        plan_error(errors, "data.inputs", "unsupported_asset", "asset inputs are available to schema 1 local plans"); return;
    }
    if (!task_keys(declaration, keys) || !plan_id(name)) {
        plan_error(errors, "data.inputs", "invalid_asset", "an asset input declares exactly asset (a plan ID), type and max_bytes"); return;
    }
    if (!json_object_is_type(text, json_type_string)) {
        asset_error(errors, "missing_asset", "the plan names an asset that was not supplied; publish it with hydra workflow plan propose <draft.json> --asset NAME=FILE or pass --assets-dir", name);
        return;
    }
    snprintf(path, sizeof(path), "assets/%s", name);
    json_object_object_del(declaration, "asset");
    f_string_add(declaration, "path", path); f_string_add(declaration, "source", "bundle");
    json_object_object_add(files, path, json_object_get(text));
}
static void expand_assets(json_object *plan, json_object *assets, json_object *files, json_object *errors) {
    json_object *inputs = f_field(f_field(plan, "data"), "inputs");
    if (!json_object_is_type(inputs, json_type_object)) return;
    json_object_object_foreach(inputs, name, declaration) {
        (void)name;
        if (f_field(declaration, "asset")) asset_input(plan, declaration, assets, files, errors);
    }
}
/* The generated input for an inline prompt is named prompt-<step>. */
static bool prompt_input(json_object *plan, json_object *step, json_object *files) {
    const char *id = f_string(step, "id"); json_object *args = f_field(step, "args"), *prompt = f_field(args, "prompt");
    json_object *inputs = f_field(f_field(plan, "data"), "inputs"), *mapped, *declaration, *reference;
    char name[80], path[80];
    if (!id || strlen(id) > 57 || !json_object_is_type(inputs, json_type_object)) return false;
    snprintf(name, sizeof(name), "prompt-%s", id); snprintf(path, sizeof(path), "prompts/%s", id);
    if (f_field(inputs, name) || !(mapped = step_inputs(plan, id)) || f_field(mapped, name)) return false;
    declaration = json_object_new_object(); f_string_add(declaration, "path", path); f_string_add(declaration, "type", "file");
    json_object_object_add(declaration, "max_bytes", json_object_new_int64(json_object_get_string_len(prompt)));
    f_string_add(declaration, "source", "bundle");
    json_object_object_add(inputs, name, declaration);
    reference = json_object_new_object(); f_string_add(reference, "input", name); json_object_object_add(mapped, name, reference);
    json_object_object_add(files, path, json_object_get(prompt));
    json_object_object_del(args, "prompt"); f_string_add(args, "prompt_input", name);
    return true;
}
static void expand_prompts(json_object *plan, json_object *files, json_object *errors) {
    json_object *steps = f_field(plan, "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (f_field(f_field(step, "args"), "prompt") && !prompt_input(plan, step, files))
            plan_error(errors, "steps.args.prompt", "prompt_conflict",
                "an inline prompt becomes input prompt-<step>; use a step ID of at most 57 characters and do not declare that input yourself");
    }
}
/* argv may pass one of the step's materialized inputs as @input/<name>. */
static void input_references(json_object *plan, json_object *errors) {
    json_object *steps = f_field(plan, "steps"), *data = f_field(f_field(plan, "data"), "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i), *argv = f_field(f_field(step, "args"), "argv");
        json_object *inputs = f_field(f_field(data, f_string(step, "id")), "inputs");
        if (!json_object_is_type(argv, json_type_array)) continue;
        for (size_t j = 1; j < json_object_array_length(argv); j++) {
            const char *arg = f_text(json_object_array_get_idx(argv, j)), *name;
            if (!arg || strncmp(arg, PLAN_INPUT_PREFIX, strlen(PLAN_INPUT_PREFIX))) continue;
            name = arg + strlen(PLAN_INPUT_PREFIX);
            if (!plan_id(name) || !json_object_is_type(inputs, json_type_object) || !f_field(inputs, name))
                plan_error(errors, "steps.args.argv", "invalid_input_reference", "argv @input/<name> must name an input declared in data.steps.<step>.inputs of the same step");
        }
    }
}
json_object *plan_expand(json_object *plan, json_object *assets, json_object *files, json_object *errors) {
    json_object *out = plan_canonical(plan); size_t before = json_object_array_length(errors);
    if (!out) return NULL;
    expand_assets(out, assets, files, errors);
    expand_prompts(out, files, errors);
    input_references(out, errors);
    if (json_object_array_length(errors) == before) return out;
    json_object_put(out); return NULL;
}
int plan_bundle_write(json_object *files, const char *directory) {
    char root[F_PATH], path[F_PATH];
    if (f_path(root, sizeof(root), directory, "bundle")) return -1;
    json_object_object_foreach(files, name, text) {
        char *slash;
        if (!task_path(name) || f_path(path, sizeof(path), root, name)) return -1;
        slash = strrchr(path, '/'); *slash = '\0';
        if (f_mkdirs(path)) return -1;
        *slash = '/';
        if (f_write(path, json_object_get_string(text), (size_t)json_object_get_string_len(text), false)) return -1;
    }
    return 0;
}
json_object *plan_bundle_assets(json_object *files) {
    json_object *assets = NULL; size_t prefix = strlen("assets/");
    json_object_object_foreach(files, name, text) {
        if (strncmp(name, "assets/", prefix)) continue;
        if (!assets) assets = json_object_new_object();
        json_object_object_add(assets, name + prefix, json_object_get(text));
    }
    return assets;
}
