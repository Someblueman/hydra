#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include "fleet/agent/agent.h"
#include <stdlib.h>
#include <string.h>

/* Declared sequence length and second-byte range for one RFC 3629 lead byte;
 * 0 rejects continuation, overlong and beyond-U+10FFFF leads. */
static size_t utf8_lead(unsigned char lead, unsigned char *low, unsigned char *high) {
    *low = 0x80; *high = 0xBF;
    if (lead < 0x80) return 1;
    if (lead < 0xC2 || lead > 0xF4) return 0;
    if (lead <= 0xDF) return 2;
    if (lead == 0xE0) *low = 0xA0;
    if (lead == 0xED) *high = 0x9F; /* UTF-16 surrogates are not scalar values. */
    if (lead <= 0xEF) return 3;
    if (lead == 0xF0) *low = 0x90;
    if (lead == 0xF4) *high = 0x8F;
    return 4;
}
/* Length of one well-formed UTF-8 sequence at text, or 0 when it is invalid
 * or cut off by the end of input. */
static size_t utf8_sequence(const unsigned char *text, size_t available) {
    unsigned char low, high; size_t length = utf8_lead(text[0], &low, &high), i;
    if (length < 2) return length;
    if (length > available || text[1] < low || text[1] > high) return 0;
    for (i = 2; i < length; i++) if ((text[i] & 0xC0) != 0x80) return 0;
    return length;
}

json_object *agent_excerpt(const char *text, size_t size) {
    json_object *excerpt; char *copy; size_t at = 0;
    if (!text || !size || !(copy = malloc(AGENT_DIAGNOSTIC_LIMIT + 1))) return NULL;
    while (at < size) {
        size_t length = utf8_sequence((const unsigned char *)text + at, size - at);
        size_t width = length ? length : 1;
        if (at + width > AGENT_DIAGNOSTIC_LIMIT) break;
        if (!length || !text[at]) copy[at] = '?';
        else memcpy(copy + at, text + at, length);
        at += width;
    }
    excerpt = json_object_new_object();
    json_object_object_add(excerpt, "text", json_object_new_string_len(copy, (int)at));
    json_object_object_add(excerpt, "bytes", json_object_new_int64((int64_t)size));
    json_object_object_add(excerpt, "truncated", json_object_new_boolean(at < size));
    free(copy); return excerpt;
}

void agent_diagnose(json_object *record, const struct agent_stream *stream, const struct f_capture *cap, int status) {
    json_object *diagnostic, *out = NULL, *err;
    size_t from = stream->malformed ? stream->line_start : stream->consumed;
    if (!status) return;
    if (strcmp(stream->adapter, "none") && from < cap->out_bytes) out = agent_excerpt(cap->out + from, cap->out_bytes - from);
    err = agent_excerpt(cap->err, cap->err_bytes);
    if (!out && !err) return;
    diagnostic = json_object_new_object();
    json_object_object_add(diagnostic, "stdout", out);
    json_object_object_add(diagnostic, "stderr", err);
    json_object_object_add(record, "diagnostic", diagnostic);
}
