#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/agent/agent.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Read-only projections of one headless agent step for the native TUI:
 *   receipt-view RECEIPT        one tab-separated summary row of agent.json
 *   stream-view PROFILE FILE N  the last N provider events as readable lines
 * Every printed field is single-line text; unknown values print as "-" so a
 * reader can keep unknown distinct from zero. Nothing here is evidence. */

#define VIEW_FIELD 160U
#define VIEW_LINE 400U
#define VIEW_TAIL (64U * 1024U)
#define VIEW_LINES 200U

/* Length of the last complete UTF-8 prefix of text[0..used). */
static size_t view_utf8_boundary(const char *text, size_t used) {
    size_t start = used, need;
    unsigned char lead;
    while (start && ((unsigned char)text[start - 1] & 0xc0U) == 0x80U && used - start < 3) start--;
    if (!start) return used;
    lead = (unsigned char)text[start - 1];
    if (lead < 0xc0U) return used;
    need = lead >= 0xf0U ? 4 : lead >= 0xe0U ? 3 : 2;
    return used - start + 1 >= need ? used : start - 1;
}

/* Copies text as one line: controls become spaces, runs of spaces collapse,
 * and a cut never splits a UTF-8 sequence. */
static void view_clean(char *out, size_t size, const char *text) {
    size_t used = 0;
    bool space = false;
    for (; text && *text && used + 1 < size; text++) {
        unsigned char c = (unsigned char)*text;
        bool blank = c < 32 || c == 127 || c == ' ';
        if (blank && (space || !used)) continue;
        space = blank;
        out[used++] = blank ? ' ' : (char)c;
    }
    used = view_utf8_boundary(out, used);
    while (used && out[used - 1] == ' ') used--;
    out[used] = '\0';
}

static void view_field(const char *text) {
    char clean[VIEW_FIELD];
    view_clean(clean, sizeof(clean), text);
    fputs(clean[0] ? clean : "-", stdout);
}

static void view_integer(json_object *value) {
    if (json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0)
        printf("%" PRId64, json_object_get_int64(value));
    else putchar('-');
}

static void view_cost(json_object *value) {
    double cost = json_object_get_double(value);
    if ((json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int)) && isfinite(cost) && cost >= 0)
        printf("%.4f", cost);
    else putchar('-');
}

/* Observed evidence wins; configuration is labelled as such, never merged. */
static void view_setting(json_object *receipt, const char *observed, const char *configured) {
    json_object *entry = f_field(f_field(receipt, "configuration"), configured);
    if (f_string(receipt, observed)) { view_field(f_string(receipt, observed)); fputs("\tobserved\t", stdout); }
    else if (f_string(entry, "value")) { view_field(f_string(entry, "value")); fputs("\tconfigured\t", stdout); }
    else fputs("-\t-\t", stdout);
}

static json_object *receipt_view(const char *path) {
    json_object *receipt = f_read_json(path, AGENT_OUTPUT_LIMIT), *usage;
    json_object *model_source;
    if (!f_number_is(receipt, "schema_version", 1)) {
        json_object_put(receipt);
        return f_error("agent-view", "invalid_receipt", "the agent receipt is unavailable or has an unsupported schema");
    }
    usage = f_field(receipt, "usage");
    view_field(f_string(receipt, "profile")); putchar('\t');
    view_field(f_string(receipt, "state")); putchar('\t');
    view_integer(f_field(receipt, "exit_status")); putchar('\t');
    view_integer(f_field(receipt, "started_at")); putchar('\t');
    view_integer(f_field(receipt, "finished_at")); putchar('\t');
    view_field(f_string(f_field(receipt, "probe"), "executable_version")); putchar('\t');
    view_setting(receipt, "observed_model", "model");
    view_setting(receipt, "observed_reasoning_effort", "reasoning_effort");
    view_integer(f_field(usage, "input_tokens")); putchar('\t');
    view_integer(f_field(usage, "cached_input_tokens")); putchar('\t');
    view_integer(f_field(usage, "output_tokens")); putchar('\t');
    view_cost(f_field(usage, "cost_usd")); putchar('\t');
    model_source = f_field(f_field(receipt, "configuration"), "model");
    view_field(f_string(model_source ? model_source : f_field(f_field(receipt, "configuration"), "reasoning_effort"), "source"));
    putchar('\n');
    json_object_put(receipt);
    return NULL;
}

/* The bounded tail of a regular file, starting at a line boundary. */
static char *view_tail(const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    char *text = NULL;
    off_t start;
    size_t used = 0;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || !(text = calloc(VIEW_TAIL + 1, 1))) { close(fd); return NULL; }
    start = st.st_size > (off_t)VIEW_TAIL ? st.st_size - (off_t)VIEW_TAIL : 0;
    if (lseek(fd, start, SEEK_SET) < 0) { close(fd); free(text); return NULL; }
    while (used < VIEW_TAIL) {
        ssize_t n = read(fd, text + used, VIEW_TAIL - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(fd);
    text[used] = '\0';
    if (start > 0) {
        char *newline = strchr(text, '\n');
        if (newline) memmove(text, newline + 1, strlen(newline + 1) + 1);
    }
    return text;
}

static void view_emit(char lines[][VIEW_LINE], size_t *count, const char *prefix, const char *text) {
    char clean[VIEW_LINE];
    view_clean(clean, sizeof(clean), text);
    if (!clean[0] && !prefix[0]) return;
    if (*count == VIEW_LINES) { memmove(lines[0], lines[1], (VIEW_LINES - 1) * VIEW_LINE); (*count)--; }
    snprintf(lines[(*count)++], VIEW_LINE, "%s%s", prefix, clean);
}

static void view_usage(char *out, size_t size, json_object *usage) {
    json_object *in = f_field(usage, "input_tokens"), *out_tokens = f_field(usage, "output_tokens");
    json_object *cached = f_field(usage, "cached_input_tokens");
    if (!cached) cached = f_field(usage, "cache_read_input_tokens");
    snprintf(out, size, "tokens in %s, cached %s, out %s",
             in ? json_object_get_string(in) : "unknown", cached ? json_object_get_string(cached) : "unknown",
             out_tokens ? json_object_get_string(out_tokens) : "unknown");
}

/* Items whose display is one string member behind a fixed prefix. */
static const char *codex_simple_item(json_object *item, const char *kind, const char **prefix) {
    static const struct { const char *kind, *prefix, *member; } simple[] = {
        {"agent_message", "assistant: ", "text"}, {"reasoning", "thinking: ", "text"},
        {"web_search", "search: ", "query"}, {"error", "error: ", "message"}};
    size_t i;
    for (i = 0; i < sizeof(simple) / sizeof(simple[0]); i++) {
        if (strcmp(kind, simple[i].kind)) continue;
        *prefix = simple[i].prefix;
        return f_string(item, simple[i].member);
    }
    return NULL;
}

static const char *codex_command(json_object *item, char *buffer, size_t size) {
    json_object *code = f_field(item, "exit_code");
    const char *command = f_string(item, "command");
    snprintf(buffer, size, "%s%s%s", command ? command : "command", code ? "  -> exit " : "", code ? json_object_get_string(code) : "");
    return buffer;
}

static const char *codex_tool(json_object *item, char *buffer, size_t size) {
    const char *server = f_string(item, "server"), *tool = f_string(item, "tool");
    snprintf(buffer, size, "%s.%s", server ? server : "tool", tool ? tool : "call");
    return buffer;
}

static void codex_change(char *buffer, size_t size, json_object *change, bool first) {
    size_t used = strlen(buffer);
    const char *path = f_string(change, "path"), *kind = f_string(change, "kind");
    if (used + 1 >= size) return;
    snprintf(buffer + used, size - used, "%s%s%s%s%s", first ? "" : ", ", path ? path : "file",
             kind ? " (" : "", kind ? kind : "", kind ? ")" : "");
}

static const char *codex_changes(json_object *item, char *buffer, size_t size) {
    json_object *changes = f_field(item, "changes");
    size_t i;
    buffer[0] = '\0';
    if (!json_object_is_type(changes, json_type_array)) return buffer;
    for (i = 0; i < json_object_array_length(changes); i++) codex_change(buffer, size, json_object_array_get_idx(changes, i), !i);
    return buffer;
}

static const char *codex_item(json_object *item, char *buffer, size_t size, const char **prefix) {
    const char *kind = f_string(item, "type"), *text;
    *prefix = "";
    if (!kind) return NULL;
    if ((text = codex_simple_item(item, kind, prefix)) || **prefix) return text;
    if (!strcmp(kind, "command_execution")) { *prefix = "$ "; return codex_command(item, buffer, size); }
    if (!strcmp(kind, "file_change")) { *prefix = "edited: "; return codex_changes(item, buffer, size); }
    if (!strcmp(kind, "mcp_tool_call")) { *prefix = "tool: "; return codex_tool(item, buffer, size); }
    *prefix = "event: "; return kind;
}

/* Returns the text to show for one codex event, or NULL to skip it. */
static const char *codex_event(json_object *event, char *buffer, size_t size, const char **prefix) {
    const char *type = f_string(event, "type");
    *prefix = "";
    if (!type) return NULL;
    if (!strcmp(type, "item.completed")) return codex_item(f_field(event, "item"), buffer, size, prefix);
    if (!strcmp(type, "item.started")) {
        const char *text = codex_item(f_field(event, "item"), buffer, size, prefix);
        if (!strcmp(*prefix, "$ ")) { *prefix = "running $ "; return text; }
        return NULL;
    }
    if (!strcmp(type, "thread.started")) { *prefix = "session "; return f_string(event, "thread_id"); }
    if (!strcmp(type, "turn.started")) { *prefix = "turn started"; return ""; }
    if (!strcmp(type, "turn.completed")) { view_usage(buffer, size, f_field(event, "usage")); *prefix = "turn completed: "; return buffer; }
    if (!strcmp(type, "turn.failed") || !strcmp(type, "error")) {
        *prefix = "error: ";
        return f_string(event, "message") ? f_string(event, "message") : f_string(f_field(event, "error"), "message");
    }
    return NULL;
}

static void claude_part(char lines[][VIEW_LINE], size_t *count, json_object *part, bool user) {
    const char *type = f_string(part, "type");
    char buffer[VIEW_LINE];
    if (!type) return;
    if (!strcmp(type, "text") && !user) view_emit(lines, count, "assistant: ", f_string(part, "text"));
    else if (!strcmp(type, "thinking")) view_emit(lines, count, "thinking: ", f_string(part, "thinking"));
    else if (!strcmp(type, "tool_use")) {
        snprintf(buffer, sizeof(buffer), "%s %s", f_string(part, "name") ? f_string(part, "name") : "tool",
                 json_object_to_json_string_ext(f_field(part, "input"), JSON_C_TO_STRING_PLAIN));
        view_emit(lines, count, "tool: ", buffer);
    } else if (!strcmp(type, "tool_result")) view_emit(lines, count, "tool result", "");
}

static void claude_content(char lines[][VIEW_LINE], size_t *count, json_object *content, bool user) {
    size_t i;
    if (!json_object_is_type(content, json_type_array)) return;
    for (i = 0; i < json_object_array_length(content); i++) claude_part(lines, count, json_object_array_get_idx(content, i), user);
}

static bool claude_event(char lines[][VIEW_LINE], size_t *count, json_object *event) {
    const char *type = f_string(event, "type");
    char buffer[VIEW_LINE];
    if (!type) return false;
    if (!strcmp(type, "system")) {
        snprintf(buffer, sizeof(buffer), "%s%s%s", f_string(event, "subtype") ? f_string(event, "subtype") : "system",
                 f_string(event, "model") ? " model " : "", f_string(event, "model") ? f_string(event, "model") : "");
        view_emit(lines, count, "session: ", buffer);
    } else if (!strcmp(type, "assistant") || !strcmp(type, "user"))
        claude_content(lines, count, f_field(f_field(event, "message"), "content"), !strcmp(type, "user"));
    else if (!strcmp(type, "result")) {
        view_emit(lines, count, "result: ", f_string(event, "result"));
        view_usage(buffer, sizeof(buffer), f_field(event, "usage"));
        view_emit(lines, count, "finished: ", buffer);
    } else return false;
    return true;
}

static bool canonical_event(char lines[][VIEW_LINE], size_t *count, json_object *event) {
    const char *type = f_string(event, "type");
    char buffer[VIEW_LINE];
    if (!type || !f_number_is(event, "schema_version", 1)) return false;
    if (!strcmp(type, "result")) view_emit(lines, count, "result: ", f_string(event, "text"));
    else if (!strcmp(type, "observation")) view_emit(lines, count, "status: ", f_string(event, "status"));
    else if (!strcmp(type, "session")) view_emit(lines, count, "session ", f_string(event, "session_id"));
    else if (!strcmp(type, "usage")) { view_usage(buffer, sizeof(buffer), f_field(event, "usage")); view_emit(lines, count, "usage: ", buffer); }
    else view_emit(lines, count, "event: ", type);
    return true;
}

/* One provider line; lines an adapter does not describe are shown raw. */
static void view_line(char lines[][VIEW_LINE], size_t *count, const char *adapter, const char *line) {
    json_object *event = f_parse(line);
    bool shown = false;
    if (event && !strcmp(adapter, "codex-jsonl")) {
        char buffer[VIEW_LINE];
        const char *prefix, *text = codex_event(event, buffer, sizeof(buffer), &prefix);
        if (text) view_emit(lines, count, prefix, text);
        shown = f_string(event, "type") != NULL;
    } else if (event && !strcmp(adapter, "claude-jsonl")) shown = claude_event(lines, count, event) || f_string(event, "type");
    else if (event && !strcmp(adapter, "canonical-jsonl")) shown = canonical_event(lines, count, event);
    if (!shown) view_emit(lines, count, "", line);
    json_object_put(event);
}

/* SOURCE is a profile name, resolved to its declared adapter, or an adapter
 * name itself (such as codex-jsonl). */
static char *view_adapter(const char *source) {
    json_object *profile;
    char *adapter;
    size_t length = strlen(source);
    if (!strcmp(source, "none") || (length > 6 && !strcmp(source + length - 6, "-jsonl"))) return strdup(source);
    profile = agent_profile(source);
    adapter = strdup(f_string(profile, "adapter") ? f_string(profile, "adapter") : "none");
    json_object_put(profile);
    return adapter;
}

static json_object *stream_view(const char *source, const char *path, const char *limit_text) {
    char *text, *save = NULL, *line, *adapter;
    char (*lines)[VIEW_LINE];
    size_t count = 0, i, limit = strtoul(limit_text, NULL, 10);
    if (!limit || limit > VIEW_LINES) limit = VIEW_LINES;
    text = view_tail(path);
    if (!text) return f_error("agent-view", "stream_unavailable", "the live provider output is unavailable");
    lines = calloc(VIEW_LINES, VIEW_LINE);
    adapter = view_adapter(source);
    if (!lines || !adapter) { free(lines); free(adapter); free(text); return f_error("agent-view", "stream_unavailable", "cannot allocate the output view"); }
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) view_line(lines, &count, adapter, line);
    for (i = count > limit ? count - limit : 0; i < count; i++) puts(lines[i]);
    free(lines); free(text); free(adapter);
    return NULL;
}

json_object *agent_view_cli(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[0], "receipt-view")) return receipt_view(argv[1]);
    if (argc == 4 && !strcmp(argv[0], "stream-view")) return stream_view(argv[1], argv[2], argv[3]);
    return f_error("agent-view", "invalid_input", "use receipt-view RECEIPT or stream-view PROFILE FILE LINES");
}
