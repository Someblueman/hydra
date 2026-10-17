#define _POSIX_C_SOURCE 200809L
#include "setup_json.h"
#include <string.h>

/* A small recursive-descent indexer: values are recorded as token ranges and
 * decoded only when read. Depth and token counts are bounded by the caller. */
#define SJ_DEPTH 32

struct sj_parser { struct sj_doc *doc; size_t pos; int depth; };

static int parse_value(struct sj_parser *p);

static char peek(const struct sj_parser *p) {
    return p->pos < p->doc->length ? p->doc->text[p->pos] : '\0';
}

static void skip_space(struct sj_parser *p) {
    while (p->pos < p->doc->length && strchr(" \t\r\n", p->doc->text[p->pos]) && p->doc->text[p->pos]) p->pos++;
}

static int add_token(struct sj_parser *p, enum sj_type type, size_t start) {
    struct sj_token *t;
    if (p->doc->count >= p->doc->capacity) return -1;
    t = &p->doc->tokens[p->doc->count];
    t->type = type; t->start = start; t->end = start; t->size = 0; t->next = p->doc->count + 1;
    return p->doc->count++;
}

static bool hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Validates one escape after the backslash at p->pos; advances past it. */
static bool skip_escape(struct sj_parser *p) {
    char c;
    p->pos++;
    c = peek(p);
    if (c && strchr("\"\\/bfnrt", c)) { p->pos++; return true; }
    if (c != 'u' || p->pos + 4 >= p->doc->length) return false;
    for (int i = 1; i <= 4; i++) if (!hex_digit(p->doc->text[p->pos + (size_t)i])) return false;
    p->pos += 5;
    return true;
}

static int parse_string(struct sj_parser *p) {
    int index = add_token(p, SJ_STRING, ++p->pos);
    if (index < 0) return -1;
    while (p->pos < p->doc->length) {
        unsigned char c = (unsigned char)p->doc->text[p->pos];
        if (c == '"') { p->doc->tokens[index].end = p->pos++; return index; }
        if (c < 0x20) return -1;
        if (c == '\\') { if (!skip_escape(p)) return -1; }
        else p->pos++;
    }
    return -1;
}

static size_t skip_digits(struct sj_parser *p) {
    size_t start = p->pos;
    while (peek(p) >= '0' && peek(p) <= '9') p->pos++;
    return p->pos - start;
}

static int parse_number(struct sj_parser *p) {
    int index = add_token(p, SJ_NUMBER, p->pos);
    if (index < 0) return -1;
    if (peek(p) == '-') p->pos++;
    if (!skip_digits(p)) return -1;
    if (peek(p) == '.') { p->pos++; if (!skip_digits(p)) return -1; }
    if (peek(p) == 'e' || peek(p) == 'E') {
        p->pos++;
        if (peek(p) == '+' || peek(p) == '-') p->pos++;
        if (!skip_digits(p)) return -1;
    }
    p->doc->tokens[index].end = p->pos;
    return index;
}

static int parse_literal(struct sj_parser *p, const char *word, enum sj_type type) {
    size_t length = strlen(word);
    int index;
    if (p->doc->length - p->pos < length || strncmp(p->doc->text + p->pos, word, length)) return -1;
    index = add_token(p, type, p->pos);
    if (index < 0) return -1;
    p->pos += length;
    p->doc->tokens[index].end = p->pos;
    return index;
}

/* One object member: "key" : value. */
static int parse_member(struct sj_parser *p) {
    skip_space(p);
    if (peek(p) != '"' || parse_string(p) < 0) return -1;
    skip_space(p);
    if (peek(p) != ':') return -1;
    p->pos++;
    return parse_value(p);
}

static int parse_container(struct sj_parser *p, bool object) {
    char close = object ? '}' : ']';
    int index = add_token(p, object ? SJ_OBJECT : SJ_ARRAY, p->pos++);
    if (index < 0 || ++p->depth > SJ_DEPTH) return -1;
    skip_space(p);
    if (peek(p) == close) p->pos++;
    else for (;;) {
        if ((object ? parse_member(p) : parse_value(p)) < 0) return -1;
        p->doc->tokens[index].size++;
        skip_space(p);
        if (peek(p) == close) { p->pos++; break; }
        if (peek(p) != ',') return -1;
        p->pos++;
    }
    p->depth--;
    p->doc->tokens[index].end = p->pos;
    p->doc->tokens[index].next = p->doc->count;
    return index;
}

static int parse_value(struct sj_parser *p) {
    char c;
    skip_space(p);
    c = peek(p);
    if (c == '{' || c == '[') return parse_container(p, c == '{');
    if (c == '"') return parse_string(p);
    if (c == 't') return parse_literal(p, "true", SJ_TRUE);
    if (c == 'f') return parse_literal(p, "false", SJ_FALSE);
    if (c == 'n') return parse_literal(p, "null", SJ_NULL);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p);
    return -1;
}

int sj_parse(struct sj_doc *doc, const char *text, size_t length, struct sj_token *tokens, int capacity) {
    struct sj_parser p;
    doc->text = text; doc->length = length; doc->tokens = tokens; doc->count = 0; doc->capacity = capacity;
    p.doc = doc; p.pos = 0; p.depth = 0;
    if (!text || capacity < 1 || parse_value(&p) < 0) { doc->count = 0; return -1; }
    skip_space(&p);
    if (p.pos != length) { doc->count = 0; return -1; }
    return 0;
}

enum sj_type sj_type_of(const struct sj_doc *doc, int token) {
    return token >= 0 && token < doc->count ? doc->tokens[token].type : SJ_NONE;
}

static bool key_equals(const struct sj_doc *doc, int token, const char *key) {
    const struct sj_token *t = &doc->tokens[token];
    size_t length = strlen(key);
    /* Keys Hydra reads are plain ASCII; an escaped key never matches. */
    return t->end - t->start == length && !memcmp(doc->text + t->start, key, length);
}

int sj_member(const struct sj_doc *doc, int object, int n, int *key) {
    int cursor, i;
    if (sj_type_of(doc, object) != SJ_OBJECT || n < 0 || n >= doc->tokens[object].size) return -1;
    cursor = object + 1;
    for (i = 0; i < n; i++) cursor = doc->tokens[cursor + 1].next;
    if (key) *key = cursor;
    return cursor + 1;
}

int sj_find(const struct sj_doc *doc, int object, const char *key) {
    int i, name, value;
    for (i = 0; (value = sj_member(doc, object, i, &name)) >= 0; i++)
        if (key_equals(doc, name, key)) return value;
    return -1;
}

int sj_item(const struct sj_doc *doc, int array, int n) {
    int cursor, i;
    if (sj_type_of(doc, array) != SJ_ARRAY || n < 0 || n >= doc->tokens[array].size) return -1;
    cursor = array + 1;
    for (i = 0; i < n; i++) cursor = doc->tokens[cursor].next;
    return cursor;
}

int sj_path(const struct sj_doc *doc, int token, const char *path) {
    char key[64];
    while (token >= 0 && *path) {
        size_t length = strcspn(path, ".");
        if (length >= sizeof(key)) return -1;
        memcpy(key, path, length); key[length] = '\0';
        token = sj_find(doc, token, key);
        path += length + (path[length] == '.');
    }
    return token;
}

static unsigned hex_value(const char *text) {
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        char c = text[i];
        value = value * 16U + (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return value;
}

/* Decodes the escape at text[*i] (the character after a backslash). */
static unsigned decode_escape(const char *text, size_t *i, size_t end) {
    static const char from[] = "\"\\/bfnrt", to[] = "\"\\/\b\f\n\r\t";
    const char *found;
    unsigned cp;
    if (text[*i] != 'u') { found = strchr(from, text[*i]); (*i)++; return found ? (unsigned char)to[found - from] : '?'; }
    cp = hex_value(text + *i + 1); *i += 5;
    if (cp >= 0xd800 && cp < 0xdc00 && *i + 6 <= end && text[*i] == '\\' && text[*i + 1] == 'u') {
        unsigned low = hex_value(text + *i + 2);
        if (low >= 0xdc00 && low < 0xe000) { *i += 6; return 0x10000U + ((cp - 0xd800U) << 10) + (low - 0xdc00U); }
    }
    return cp >= 0xd800 && cp < 0xe000 ? '?' : cp;
}

struct sj_out { char *text; size_t size, used; bool truncated; };

static void put_byte(struct sj_out *o, unsigned char byte) {
    if (o->used + 1 >= o->size) { o->truncated = true; return; }
    o->text[o->used++] = (char)byte;
}

static void put_codepoint(struct sj_out *o, unsigned cp) {
    if (cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp < 0xa0)) cp = '?';
    if (cp < 0x80) { put_byte(o, (unsigned char)cp); return; }
    if (o->used + 5 > o->size) { o->truncated = true; return; }
    if (cp < 0x800) { put_byte(o, (unsigned char)(0xc0 | (cp >> 6))); }
    else if (cp < 0x10000) { put_byte(o, (unsigned char)(0xe0 | (cp >> 12))); put_byte(o, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))); }
    else {
        put_byte(o, (unsigned char)(0xf0 | (cp >> 18))); put_byte(o, (unsigned char)(0x80 | ((cp >> 12) & 0x3f)));
        put_byte(o, (unsigned char)(0x80 | ((cp >> 6) & 0x3f)));
    }
    put_byte(o, (unsigned char)(0x80 | (cp & 0x3f)));
}

static void decode_string(const struct sj_doc *doc, const struct sj_token *t, struct sj_out *o) {
    size_t i = t->start;
    while (i < t->end && !o->truncated) {
        unsigned char c = (unsigned char)doc->text[i];
        if (c == '\\') { i++; put_codepoint(o, decode_escape(doc->text, &i, t->end)); }
        else { put_byte(o, c == 0x7f ? '?' : c); i++; }
    }
}

bool sj_text(const struct sj_doc *doc, int token, char *out, size_t size) {
    struct sj_out o = {out, size, 0, false};
    const struct sj_token *t;
    enum sj_type type = sj_type_of(doc, token);
    if (!size) return false;
    out[0] = '\0';
    if (type == SJ_NONE || type == SJ_OBJECT || type == SJ_ARRAY || type == SJ_NULL) return false;
    t = &doc->tokens[token];
    if (type == SJ_STRING) decode_string(doc, t, &o);
    else for (size_t i = t->start; i < t->end; i++) put_byte(&o, (unsigned char)doc->text[i]);
    out[o.used] = '\0';
    return !o.truncated;
}

bool sj_path_text(const struct sj_doc *doc, int token, const char *path, char *out, size_t size) {
    return sj_text(doc, sj_path(doc, token, path), out, size);
}

bool sj_true(const struct sj_doc *doc, int token) { return sj_type_of(doc, token) == SJ_TRUE; }
