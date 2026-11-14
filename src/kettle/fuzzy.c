#include "components.h"
#include <string.h>

/* Abbreviation matching for filters and the command palette: the pattern's
 * characters in order, scored so that the matches a person means come
 * first. Pure functions over caller strings. */

enum {
    SCORE_CHARACTER = 16,   /* every matched character                       */
    SCORE_ADJACENT = 12,    /* right after the previous matched character    */
    SCORE_START = 16,       /* the first character of the text               */
    SCORE_WORD = 10,        /* the first character of a word                 */
    PENALTY_GAP_LIMIT = 24  /* most a run of unmatched characters costs      */
};

static uint32_t fold(uint32_t c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static size_t next(const char *text, uint32_t *cp) {
    size_t used = kt_utf8_decode(text, strlen(text), cp);
    if (!used) { *cp = (unsigned char)*text; used = 1; }
    return used;
}

/* The next pattern character after `*at`, skipping blanks; 0 at the end. */
static uint32_t pattern_char(const char *pattern, size_t *at) {
    uint32_t cp = 0;
    while (pattern[*at]) {
        *at += next(pattern + *at, &cp);
        if (cp != ' ' && cp != '\t') return fold(cp);
    }
    return 0;
}

/* Byte offset just past the earliest complete match starting at `from`, or
 * KT_NONE when the text does not contain the pattern in order. */
static size_t forward_end(const char *pattern, const char *text, size_t from) {
    size_t p = 0, t = from;
    uint32_t want = pattern_char(pattern, &p), cp;
    while (want && text[t]) {
        t += next(text + t, &cp);
        if (fold(cp) == want) want = pattern_char(pattern, &p);
    }
    return want ? KT_NONE : t;
}

/* Byte offsets of each pattern character, read backwards from `end`: the
 * latest start for a match ending there, which makes the span shortest. */
static size_t backward_start(const char *pattern, const char *text, size_t end) {
    uint32_t chars[KT_FUZZY_POSITIONS * 4];
    size_t count = 0, p = 0, t = end;
    uint32_t c;
    while ((c = pattern_char(pattern, &p)) != 0 && count < sizeof(chars) / sizeof(chars[0])) chars[count++] = c;
    while (count && t > 0) {
        size_t start = t - 1;
        uint32_t cp;
        while (start > 0 && ((unsigned char)text[start] & 0xC0U) == 0x80U) start--;
        (void)next(text + start, &cp);
        if (fold(cp) == chars[count - 1]) count--;
        t = start;
    }
    return t;
}

static bool word_start(const char *text, size_t at) {
    unsigned char before, here = (unsigned char)text[at];
    if (at == 0) return true;
    before = (unsigned char)text[at - 1];
    if (strchr(" \t-_/.:,;()[]", before)) return true;
    return before >= 'a' && before <= 'z' && here >= 'A' && here <= 'Z';
}

struct scoring { int score; size_t previous; bool any; };

static void score_char(struct scoring *s, const char *text, size_t at) {
    s->score += SCORE_CHARACTER;
    if (at == 0) s->score += SCORE_START;
    else if (word_start(text, at)) s->score += SCORE_WORD;
    if (s->any && at == s->previous) s->score += SCORE_ADJACENT;
    else if (s->any) s->score -= at - s->previous > PENALTY_GAP_LIMIT ? PENALTY_GAP_LIMIT : (int)(at - s->previous);
}

int kt_fuzzy(const char *pattern, const char *text, size_t *positions, size_t capacity) {
    struct scoring s = {0, 0, false};
    size_t end, at, p = 0, found = 0;
    uint32_t want, cp;
    if (!pattern || !text) return -1;
    if (!pattern_char(pattern, &p)) return 0;
    end = forward_end(pattern, text, 0);
    if (end == KT_NONE) return -1;
    at = backward_start(pattern, text, end);
    p = 0;
    want = pattern_char(pattern, &p);
    while (want && at < end) {
        size_t here = at;
        at += next(text + at, &cp);
        if (fold(cp) != want) continue;
        score_char(&s, text, here);
        if (positions && found < capacity) positions[found] = here;
        found++;
        s.any = true;
        s.previous = at;
        want = pattern_char(pattern, &p);
    }
    return s.score < 1 ? 1 : s.score;
}

/* ---- ranking ------------------------------------------------------------ */

static bool before(const struct kt_match *a, const struct kt_match *b) {
    return a->score > b->score || (a->score == b->score && a->index < b->index);
}

/* Heap sort with the worst match at the root, so the array ends best first. */
static void sift(struct kt_match *m, size_t root, size_t count) {
    for (;;) {
        size_t child = root * 2 + 1, worst = root;
        struct kt_match swap;
        if (child < count && before(&m[worst], &m[child])) worst = child;
        if (child + 1 < count && before(&m[worst], &m[child + 1])) worst = child + 1;
        if (worst == root) return;
        swap = m[root]; m[root] = m[worst]; m[worst] = swap;
        root = worst;
    }
}

void kt_fuzzy_rank(struct kt_match *matches, size_t count) {
    size_t i;
    if (count < 2) return;
    for (i = count / 2; i-- > 0;) sift(matches, i, count);
    for (i = count - 1; i > 0; i--) {
        struct kt_match swap = matches[0];
        matches[0] = matches[i]; matches[i] = swap;
        sift(matches, 0, i);
    }
}
