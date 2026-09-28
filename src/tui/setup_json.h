#ifndef HYDRA_TUI_SETUP_JSON_H
#define HYDRA_TUI_SETUP_JSON_H
#include <stdbool.h>
#include <stddef.h>
/* Bounded read-only JSON reader for CLI envelopes. The TUI has no JSON-C
 * dependency; this parser only indexes a caller-owned document. Tokens are in
 * document order; an object's children alternate key and value. */
enum sj_type { SJ_NONE, SJ_OBJECT, SJ_ARRAY, SJ_STRING, SJ_NUMBER, SJ_TRUE, SJ_FALSE, SJ_NULL };
struct sj_token {
    enum sj_type type;
    size_t start, end; /* strings exclude their quotes */
    int size;          /* members (object) or items (array) */
    int next;          /* first token after this value's subtree */
};
struct sj_doc {
    const char *text;
    size_t length;
    struct sj_token *tokens;
    int count, capacity;
};
/* Returns 0 for one complete JSON value followed only by whitespace, -1 for
 * malformed input, nesting deeper than 32 levels or too many tokens. text and
 * tokens stay caller-owned and must outlive doc. */
int sj_parse(struct sj_doc *doc, const char *text, size_t length, struct sj_token *tokens, int capacity);
/* Value token of key in object, or -1. */
int sj_find(const struct sj_doc *doc, int object, const char *key);
/* Dotted path of object keys from token, e.g. "data.next.step"; -1 if absent. */
int sj_path(const struct sj_doc *doc, int token, const char *path);
/* nth item of an array, or -1. */
int sj_item(const struct sj_doc *doc, int array, int n);
/* nth key/value of an object; returns the value token and sets *key, or -1. */
int sj_member(const struct sj_doc *doc, int object, int n, int *key);
enum sj_type sj_type_of(const struct sj_doc *doc, int token);
/* Terminal-safe text of a string, number or literal: escapes are decoded and
 * control characters become '?'. False (with empty out) for other tokens or
 * truncation. */
bool sj_text(const struct sj_doc *doc, int token, char *out, size_t size);
/* Convenience: text of a dotted path under token. */
bool sj_path_text(const struct sj_doc *doc, int token, const char *path, char *out, size_t size);
bool sj_true(const struct sj_doc *doc, int token);
#endif
