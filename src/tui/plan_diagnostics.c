#include "plan_diagnostics.h"
#include <stdbool.h>
#include <string.h>

/* A bounded scanner for the compiler's JSON envelope. It never trusts the
 * response to be well formed: every lookup stops at the terminating NUL. */
struct summary { char *out; size_t size, used; };

static void put(struct summary *s, const char *text, size_t n) {
    size_t room = s->size - 1U - s->used;
    if (n > room) n = room;
    memcpy(s->out + s->used, text, n);
    s->used += n;
    s->out[s->used] = '\0';
}

/* p follows an opening quote; returns the closing quote or NULL. */
static const char *string_end(const char *p) {
    while (*p && *p != '"') p += (p[0] == '\\' && p[1]) ? 2 : 1;
    return *p ? p : NULL;
}

static char unescape(const char **p) {
    char c = **p;
    (*p)++;
    if (c == 'n' || c == 't' || c == 'r' || c == 'b' || c == 'f') return ' ';
    if (c != 'u') return c;
    for (int i = 0; i < 4 && **p; i++) (*p)++;
    return '?';
}

static void put_string(struct summary *s, const char *p, const char *end) {
    while (p < end) {
        char c = *p++;
        if (c == '\\' && p < end) c = unescape(&p);
        if ((unsigned char)c < 32U) c = ' ';
        put(s, &c, 1U);
    }
}

/* p follows the diagnostics array's '['; returns its closing ']' or NULL. */
static const char *array_end(const char *p) {
    int depth = 1;
    for (; *p; p++) {
        if (*p == '"') {
            p = string_end(p + 1);
            if (!p) return NULL;
        } else if (*p == '[' || *p == '{') {
            depth++;
        } else if ((*p == ']' || *p == '}') && --depth == 0) {
            return p;
        }
    }
    return NULL;
}

static int wanted(const char *key, const char *key_end) {
    static const char *const keys[] = {"path", "message", "recovery"};
    for (int i = 0; i < 3; i++)
        if ((size_t)(key_end - key) == strlen(keys[i]) && !strncmp(key, keys[i], strlen(keys[i]))) return i;
    return -1;
}

/* p follows a string's opening quote. Appends the member value when that
 * string is a wanted key; returns the position after the consumed strings. */
static const char *member(struct summary *s, const char *p, const char *end, unsigned *messages) {
    static const char *const prefixes[] = {"\n- ", "", "\n  Next: "};
    const char *key_end = string_end(p), *value, *value_end;
    int key;
    if (!key_end || key_end >= end) return NULL;
    key = wanted(p, key_end);
    if (key < 0 || key_end[1] != ':' || key_end[2] != '"') return key_end + 1;
    value = key_end + 3;
    value_end = string_end(value);
    if (!value_end || value_end > end) return NULL;
    put(s, prefixes[key], strlen(prefixes[key]));
    put_string(s, value, value_end);
    if (key == 0) put(s, ": ", 2U);
    if (key == 1) (*messages)++;
    return value_end + 1;
}

size_t plan_failure_summary(const char *raw, char *out, size_t size) {
    static const char key[] = "\"diagnostics\":[";
    struct summary s = {out, size, 0U};
    const char *p = raw ? strstr(raw, key) : NULL, *end = NULL;
    unsigned messages = 0U;
    if (!out || size < 2U) return 0U;
    out[0] = '\0';
    if (p) end = array_end(p + sizeof(key) - 1U);
    if (!end) return 0U;
    put(&s, "Validation failed; no execution occurred.", 41U);
    for (p += sizeof(key) - 1U; p && p < end;) p = *p == '"' ? member(&s, p + 1, end, &messages) : p + 1;
    if (!messages) { out[0] = '\0'; return 0U; }
    put(&s, "\n\nCompiler response:\n", 21U);
    put(&s, raw, strlen(raw));
    return s.used;
}
