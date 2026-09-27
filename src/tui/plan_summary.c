#include "plan_summary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Finds a preview line that starts with prefix; returns its value and length. */
static const char *preview_line(const char *text, size_t length, const char *prefix, size_t *value_length) {
    size_t n = strlen(prefix), offset = 0;
    while (offset < length) {
        const char *line = text + offset, *end = memchr(line, '\n', length - offset);
        size_t line_length = end ? (size_t)(end - line) : length - offset;
        if (line_length >= n && !memcmp(line, prefix, n)) {
            *value_length = line_length - n;
            return line + n;
        }
        offset += line_length + 1;
    }
    return NULL;
}

/* The preview joins items with " , "; keep the names and join them with ", ". */
static bool separator(char c) { return c == ' ' || c == ','; }

static void item_list(const char *value, size_t length, char *out, size_t size) {
    size_t used = 0, i = 0;
    out[0] = '\0';
    while (i < length) {
        size_t start, word;
        for (; i < length && separator(value[i]); i++) {}
        for (start = i; i < length && !separator(value[i]); i++) {}
        word = i - start;
        if (!word || used + word + 3 > size) break;
        if (used) { memcpy(out + used, ", ", 2); used += 2; }
        memcpy(out + used, value + start, word);
        used += word;
        out[used] = '\0';
    }
}

static bool budget(const char *line, const char *label, unsigned *value) {
    const char *at = strstr(line, label);
    char *end;
    unsigned long parsed;
    if (!at) return false;
    parsed = strtoul(at + strlen(label), &end, 10);
    if (end == at + strlen(label) || parsed > 100000000UL) return false;
    *value = (unsigned)parsed;
    return true;
}

bool plan_summary_parse(const char *preview, size_t length, struct plan_summary *out) {
    size_t n = 0;
    const char *value;
    char line[512];
    unsigned seconds = 0;
    memset(out, 0, sizeof(*out));
    if (!preview) return false;
    if ((value = preview_line(preview, length, "Tools:", &n))) item_list(value, n, out->tools, sizeof(out->tools));
    if ((value = preview_line(preview, length, "Declared repository writes:", &n))) item_list(value, n, out->writes, sizeof(out->writes));
    if (!(value = preview_line(preview, length, "Budgets:", &n)) || n >= sizeof(line)) return false;
    memcpy(line, value, n);
    line[n] = '\0';
    if (!budget(line, "parallelism ", &out->parallelism) || !budget(line, "wall time ", &seconds) ||
        !budget(line, "heads ", &out->heads)) return false;
    out->minutes = seconds / 60U + (seconds % 60U ? 1U : 0U);
    return true;
}

void plan_summary_policy(const struct plan_summary *s, char *out, size_t size) {
    snprintf(out, size, "tools %s; parallelism %u; at most %u head%s; %u min wall time",
             s->tools[0] ? s->tools : "none", s->parallelism, s->heads, s->heads == 1 ? "" : "s", s->minutes);
}

void plan_summary_consequences(const struct plan_summary *s, size_t steps, size_t spawns, char *out, size_t size) {
    const char *writes = !s->writes[0] ? "declares no repository writes" :
        !strcmp(s->writes, "@spawned:*") ? "writes only inside heads it spawns" : "may write ";
    snprintf(out, size, "Runs %zu step%s, spawns %zu head%s, up to %u minute%s; %s%s.", steps, steps == 1 ? "" : "s",
             spawns, spawns == 1 ? "" : "s", s->minutes, s->minutes == 1 ? "" : "s", writes,
             s->writes[0] && strcmp(s->writes, "@spawned:*") ? s->writes : "");
}
