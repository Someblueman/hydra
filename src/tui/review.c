#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

#define REVIEW_LIMIT (1024U * 1024U)
#define REVIEW_LINE 8192U
#define REVIEW_REFS 16U
/* Producer previews contain at most 16 KiB before line framing. Allow the
 * reconstructed display newlines too, within the whole-document byte cap. */
#define REVIEW_PREVIEW 32768U

struct review_reference {
    char kind[16], state[32], locator[4096];
    char preview[REVIEW_PREVIEW + 1U];
    size_t length;
};
struct review_document {
    char text[REVIEW_LIMIT + 1U];
    size_t length, text_count, ref_count, preview_count;
    struct review_reference refs[REVIEW_REFS];
};
#define REVIEW_HEADINGS 64U
#define REVIEW_LOG (1024U * 1024U)
struct native_review {
    struct native_capture job;
    struct native_review_identity identity;
    struct review_document *document;
    size_t scroll, lines, reference;
    bool active, references, preview, opening, stale, identity_view, log_view;
    char notice[TEXT];
    char supplied_reference[8256];
    /* Wrapped row of each section heading from the last frame, for n/N. */
    size_t headings[REVIEW_HEADINGS], heading_count;
    /* The complete local log opened with L; read-only and bounded. */
    char *log, log_path[4096];
    size_t log_bytes;
    bool log_tail;
};

static void discard_log(struct native_review *review);

bool native_review_active(const struct app *app)
{ return app->review && app->review->active; }

void native_review_destroy(struct app *app)
{
    if (!app->review) return;
    native_capture_destroy(&app->review->job);
    discard_log(app->review);
    free(app->review->document);
    free(app->review);
    app->review = NULL;
}

static void discard_log(struct native_review *review)
{
    free(review->log); review->log = NULL; review->log_view = false; review->log_bytes = 0U; review->log_tail = false;
}

static void discard_document(struct native_review *review)
{
    native_capture_destroy(&review->job);
    discard_log(review);
    free(review->document); review->document = NULL;
    review->scroll = 0U; review->lines = 0U; review->reference = 0U;
    review->references = false; review->preview = false; review->opening = false; review->identity_view = false;
}

void native_review_sync(struct app *app, const struct native_review_identity *identity, bool current)
{
    struct native_review *review = app->review;
    if (!review || !review->active) return;
    if (app->view != 9 || !identity) {
        discard_document(review); review->active = false; return;
    }
    if (memcmp(identity, &review->identity, sizeof(*identity))) {
        discard_document(review); review->identity = *identity;
        review->supplied_reference[0] = '\0';
        review->stale = true;
        copy_text(review->notice, sizeof(review->notice), "Selection or revision changed; r loads exact review");
    } else if (!current && !review->stale) {
        discard_document(review); review->stale = true;
        copy_text(review->notice, sizeof(review->notice), "Attention unavailable; review is no longer current");
    }
}

static bool matches_header(char *line, const struct native_review_identity *id)
{
    char *fields[16]; size_t i;
    const char *expected[] = {"HYDRA_REVIEW", "1", id->source, id->project, id->host,
        id->task, id->run, id->step, id->attempt, id->head, id->instance, id->request,
        id->binding, id->revision, id->identity};
    if (split_fields(line, fields, 16U) != 15U) return false;
    for (i = 0U; i < 15U; i++) if (strcmp(fields[i], expected[i])) return false;
    return true;
}

static bool append_text(char *text, size_t capacity, size_t *length, const char *line)
{
    size_t n = strlen(line);
    if (*length >= capacity || n >= capacity - *length - 1U) return false;
    memcpy(text + *length, line, n); *length += n;
    text[(*length)++] = '\n'; text[*length] = '\0';
    return true;
}

static bool reference_locator(const char *locator)
{
    const char *authority;
    size_t i;
    if (!locator[0] || strlen(locator) >= 4096U) return false;
    if (locator[0] == '/') return true;
    if (!strncmp(locator, "https://", 8U)) authority = locator + 8U;
    else if (!strncmp(locator, "http://", 7U)) authority = locator + 7U;
    else return false;
    if (!*authority || *authority == '/' || *authority == '?' || *authority == '#') return false;
    for (i = 0U; locator[i]; i++) if ((unsigned char)locator[i] <= 0x20U || locator[i] == '\\') return false;
    return true;
}

static bool parse_reference(struct review_document *document, char *line)
{
    char *fields[5]; struct review_reference *ref;
    if (document->ref_count == REVIEW_REFS || split_fields(line, fields, 5U) != 4U) return false;
    if (strcmp(fields[1], "transcript") && strcmp(fields[1], "log") && strcmp(fields[1], "pr")) return false;
    if (strcmp(fields[2], "available") && strcmp(fields[2], "inaccessible") && strcmp(fields[2], "supplied-unopened")) return false;
    if (!reference_locator(fields[3])) return false;
    if ((fields[3][0] == '/') == !strcmp(fields[2], "supplied-unopened")) return false;
    ref = &document->refs[document->ref_count++];
    copy_text(ref->kind, sizeof(ref->kind), fields[1]);
    copy_text(ref->state, sizeof(ref->state), fields[2]);
    copy_text(ref->locator, sizeof(ref->locator), fields[3]);
    return true;
}

static bool review_number(const char *value, unsigned *number)
{
    size_t i;
    if (!value || !*value) return false;
    for (i = 0U; value[i]; i++) if (value[i] < '0' || value[i] > '9') return false;
    return parse_unsigned(value, number);
}

static bool parse_preview(struct review_document *document, char *line)
{
    char *fields[4]; unsigned index; struct review_reference *ref;
    if (split_fields(line, fields, 4U) != 3U || !review_number(fields[1], &index) || index >= document->ref_count) return false;
    ref = &document->refs[index];
    if (strcmp(ref->state, "available") || ref->locator[0] != '/') return false;
    if (!append_text(ref->preview, sizeof(ref->preview), &ref->length, fields[2])) return false;
    document->preview_count++;
    return true;
}

static bool parse_end(struct review_document *document, char *line)
{
    char *fields[5]; unsigned text_count, ref_count, preview_count;
    if (split_fields(line, fields, 5U) != 4U || !review_number(fields[1], &text_count) ||
        !review_number(fields[2], &ref_count) || !review_number(fields[3], &preview_count)) return false;
    return text_count == document->text_count && ref_count == document->ref_count &&
        preview_count == document->preview_count && text_count > 0U;
}

static bool parse_body(struct review_document *document, char *line, bool *ended)
{
    if (!strncmp(line, "TEXT\t", 5U)) {
        if (strchr(line + 5U, '\t') || !append_text(document->text, sizeof(document->text), &document->length, line + 5U)) return false;
        document->text_count++; return true;
    }
    if (!strncmp(line, "REF\t", 4U)) return parse_reference(document, line);
    if (!strncmp(line, "PREVIEW\t", 8U)) return parse_preview(document, line);
    if (strncmp(line, "END\t", 4U) || !parse_end(document, line)) return false;
    *ended = true; return true;
}

/* Read bytes rather than fgets: embedded NUL cannot hide trailing records.
 * Framing, document bytes, record lengths and references are independently bounded. */
static bool accept_line(char *line, const struct native_review_identity *id,
                        struct review_document *document, bool *header, bool *ended)
{
    if (*header) return parse_body(document, line, ended);
    if (!matches_header(line, id)) return false;
    *header = true; return true;
}

static bool parse_document(FILE *input, const struct native_review_identity *id, struct review_document *document)
{
    char line[REVIEW_LINE]; size_t used = 0U, total = 0U; int byte;
    bool header = false, ended = false;
    while ((byte = fgetc(input)) != EOF) {
        if (++total > REVIEW_LIMIT || ended || used + 1U >= sizeof(line)) return false;
        if ((byte < 0x20 && byte != '\t' && byte != '\n') || byte == 0x7f) return false;
        if (byte != '\n') { line[used++] = (char)byte; continue; }
        line[used] = '\0'; used = 0U;
        if (!accept_line(line, id, document, &header, &ended)) return false;
    }
    return header && ended && !used && !ferror(input);
}

static void complete_review(struct native_review *review)
{
    FILE *input = native_capture_take(&review->job);
    struct review_document *document = calloc(1U, sizeof(*document));
    bool valid = input && document && parse_document(input, &review->identity, document);
    if (input) fclose(input);
    if (!valid) {
        free(document); review->stale = true;
        copy_text(review->notice, sizeof(review->notice), "Review unavailable: failed, malformed or identity mismatch");
        return;
    }
    free(review->document); review->document = document;
    review->stale = false; review->notice[0] = '\0';
}

void native_review_tick(struct app *app)
{
    struct native_review *review = app->review;
    if (!review || !review->job.pid || !native_capture_step(&review->job)) return;
    if (!review->opening) { complete_review(review); return; }
    {
        bool success; FILE *input = native_capture_result(&review->job, &success);
        if (input) fclose(input);
        copy_text(review->notice, sizeof(review->notice), success ?
            "Opened in browser; content not verified" : "Browser open failed or unavailable");
        review->opening = false;
    }
}

void native_review_open(struct app *app, const struct native_review_identity *identity)
{
    struct native_review *review = app->review;
    const char *command = !strcmp(identity->source, "workflow records") ? "workflow" :
        !strcmp(identity->source, "fleet overview") ? "fleet" : NULL;
    if (!review) {
        review = calloc(1U, sizeof(*review));
        if (!review) return;
        review->job.fd = -1; app->review = review;
    }
    if (memcmp(identity, &review->identity, sizeof(*identity))) review->supplied_reference[0] = '\0';
    discard_document(review); review->identity = *identity; review->active = true; review->stale = false;
    if (command) {
        struct native_review_identity *id = &review->identity;
        char *argv[] = {(char *)app->hydra, (char *)command, "review-data", id->kind, id->project,
            id->host, id->task, id->run, id->step, id->attempt, id->head, id->instance,
            id->request, id->binding, id->revision, id->identity,
            review->supplied_reference[0] ? review->supplied_reference : NULL, NULL};
        if (native_capture_start(&review->job, argv, !strcmp(command, "fleet") ? 60000L : 13000L)) {
            copy_text(review->notice, sizeof(review->notice), "Loading exact review... Esc back / q quit"); return;
        }
    }
    review->stale = true;
    copy_text(review->notice, sizeof(review->notice), "Review unavailable: no supported public route");
}

static void open_reference(struct native_review *review)
{
    struct review_reference *ref;
    if (!review->document || review->reference >= review->document->ref_count || review->stale) return;
    ref = &review->document->refs[review->reference];
    review->preview = true; review->scroll = 0U;
    if (ref->locator[0] == '/') {
        copy_text(review->notice, sizeof(review->notice), !strcmp(ref->state, "available") ?
            "Recorded local preview (bounded)" : "Reference inaccessible"); return;
    }
    if (review->job.pid) return;
    {
#ifdef __APPLE__
        char *argv[] = {"open", ref->locator, NULL};
#else
        char *argv[] = {"xdg-open", ref->locator, NULL};
#endif
        if (native_capture_start(&review->job, argv, 5000L)) {
            review->opening = true; copy_text(review->notice, sizeof(review->notice), "Opening supplied web reference...");
        } else copy_text(review->notice, sizeof(review->notice), "Browser open unavailable");
    }
}

static void scroll_review(struct native_review *review, int direction)
{
    if (review->references && !review->preview) {
        size_t count = review->document ? review->document->ref_count : 0U;
        if (direction > 0 && review->reference + 1U < count) review->reference++;
        if (direction < 0 && review->reference) review->reference--;
        return;
    }
    if (direction > 0 && review->scroll + 1U < review->lines) review->scroll++;
    if (direction < 0 && review->scroll) review->scroll--;
}

/* This only encodes one explicit user-supplied reference for the producer.
 * Evidence and validation remain owned by the public review command. */
static void encode_reference(struct native_review *review, const char *kind, const char *locator)
{
    size_t i, used;
    char *out = review->supplied_reference;
    used = (size_t)snprintf(out, sizeof(review->supplied_reference), "[{\"kind\":\"%s\",\"locator\":\"", kind);
    for (i = 0U; locator[i]; i++) {
        if (locator[i] == '"' || locator[i] == '\\') out[used++] = '\\';
        out[used++] = locator[i];
    }
    memcpy(out + used, "\"}]", 4U);
}

static void supply_reference(struct app *app, char key)
{
    struct native_review *review = app->review;
    struct native_review_identity identity = review->identity;
    const char *kind = key == 'l' ? "log" : key == 't' ? "transcript" : "pr";
    char prompt[100], locator[4096];
    snprintf(prompt, sizeof(prompt), "%s reference: absolute local path or http(s) URL", kind);
    if (prompt_text(app, prompt, locator, sizeof(locator))) return;
    if (memcmp(&identity, &review->identity, sizeof(identity)) || !review->active || review->stale) {
        copy_text(review->notice, sizeof(review->notice), "Selection changed or stale; reference was not requested"); return;
    }
    if (!reference_locator(locator)) {
        copy_text(review->notice, sizeof(review->notice), "Unsupported reference: use absolute path or http(s)"); return;
    }
    encode_reference(review, kind, locator);
    native_review_open(app, &identity);
    review->references = true;
}

static void jump_section(struct native_review *review, int direction)
{
    size_t i;
    if (review->references || review->identity_view || review->log_view) return;
    for (i = 0U; direction > 0 && i < review->heading_count; i++)
        if (review->headings[i] > review->scroll) { review->scroll = review->headings[i]; return; }
    for (i = review->heading_count; direction < 0 && i > 0U; i--)
        if (review->headings[i - 1U] < review->scroll) { review->scroll = review->headings[i - 1U]; return; }
}

static void page_review(const struct app *app, struct native_review *review, int direction)
{
    size_t page = app->rows > 8 ? (size_t)app->rows - 8U : 1U;
    if (direction > 0) review->scroll = review->scroll + page < review->lines ? review->scroll + page : review->lines ? review->lines - 1U : 0U;
    else review->scroll = review->scroll > page ? review->scroll - page : 0U;
}

/* Read the first available local log reference in full (the last 1 MiB of a
 * larger file). Terminal control bytes are replaced; nothing is executed. */
static bool read_log(struct native_review *review, const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    struct stat st;
    ssize_t n = -1;
    char *text;
    if (fd < 0) return false;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || !(text = malloc(REVIEW_LOG + 1U))) { close(fd); return false; }
    review->log_tail = (size_t)st.st_size > REVIEW_LOG;
    if (!review->log_tail || lseek(fd, st.st_size - (off_t)REVIEW_LOG, SEEK_SET) >= 0) n = read(fd, text, REVIEW_LOG);
    close(fd);
    if (n < 0) { free(text); return false; }
    for (ssize_t i = 0; i < n; i++)
        if (((unsigned char)text[i] < 32U && text[i] != '\n' && text[i] != '\t') || text[i] == 127) text[i] = '?';
    text[n] = '\0';
    review->log = text; review->log_bytes = (size_t)st.st_size;
    return true;
}

static void open_log(struct native_review *review)
{
    size_t i;
    if (!review->document || review->stale) return;
    for (i = 0U; i < review->document->ref_count; i++) {
        const struct review_reference *ref = &review->document->refs[i];
        if (strcmp(ref->kind, "log") || strcmp(ref->state, "available") || ref->locator[0] != '/') continue;
        discard_log(review);
        if (!read_log(review, ref->locator)) break;
        copy_text(review->log_path, sizeof(review->log_path), ref->locator);
        review->log_view = true; review->references = false; review->identity_view = false; review->scroll = 0U;
        return;
    }
    copy_text(review->notice, sizeof(review->notice), "No readable local log is recorded for this review");
}

static void review_back(struct native_review *review)
{
    if (review->log_view) { discard_log(review); review->scroll = 0U; }
    else if (review->preview) { review->preview = false; review->scroll = 0U; }
    else if (review->references) { review->references = false; review->scroll = 0U; }
    else if (review->identity_view) { review->identity_view = false; review->scroll = 0U; }
    else { discard_document(review); review->active = false; }
}

bool native_review_key(struct app *app, char key)
{
    struct native_review *review = app->review;
    if (!native_review_active(app)) return false;
    switch (key) {
        case 'j': case 'B': scroll_review(review, 1); break;
        case 'k': case 'A': scroll_review(review, -1); break;
        case 'g': review->scroll = 0U; break;
        case 'G': review->scroll = review->lines ? review->lines - 1U : 0U; break;
        case 'f': review->references = !review->references; review->identity_view = false; review->preview = false; review->scroll = 0U; break;
        case 'i': review->identity_view = !review->identity_view; review->references = false; review->preview = false; review->scroll = 0U; break;
        case '\r': case '\n': case 'o': if (review->references) open_reference(review); break;
        case 'l': case 't': case 'p': supply_reference(app, key); break;
        case 'n': case 'N': jump_section(review, key == 'n' ? 1 : -1); break;
        case ' ': case 'b': page_review(app, review, key == ' ' ? 1 : -1); break;
        case 'L': open_log(review); break;
        case 27: review_back(review); break;
        case 'r': case 'I': case '[': case ']': return false;
        default: break;
    }
    /* A review never falls through into an unrelated action or acknowledgement. */
    return true;
}

static void wrapped_line(struct app *app, const char *text, size_t length, size_t *row, size_t scroll)
{
    size_t width = app->cols > 5 ? (size_t)app->cols - 5U : 1U, offset = 0U;
    if (width > 2000U) width = 2000U;
    do {
        size_t count = length - offset;
        if (count > width) count = width;
        if ((*row)++ >= scroll && app->line < app->limit) linef(app, "%.*s", (int)count, text + offset);
        offset += count;
    } while (offset < length);
}

/* A section heading is an upper-case word of three or more letters at the
 * start of a line, ended by two spaces or the line end (e.g. "DIFF  ..."). */
static bool heading_line(const char *text, size_t length)
{
    size_t i = 0U;
    while (i < length && text[i] >= 'A' && text[i] <= 'Z') i++;
    if (i < 3U) return false;
    while (i < length && ((text[i] >= 'A' && text[i] <= 'Z') || text[i] == ' ')) {
        if (text[i] == ' ' && i + 1U < length && text[i + 1U] == ' ') return true;
        i++;
    }
    return i == length;
}

static bool starts(const char *text, size_t length, const char *prefix)
{ size_t n = strlen(prefix); return length >= n && !memcmp(text, prefix, n); }

/* Styling only; the document text is shown unchanged. */
static enum tone line_tone(const char *text, size_t length, bool diff)
{
    if (heading_line(text, length)) return TONE_STRONG;
    if (diff && starts(text, length, "    @@")) return TONE_BORDER;
    if (diff && starts(text, length, "    +") && !starts(text, length, "    +++")) return TONE_SUCCESS;
    if (diff && starts(text, length, "    -") && !starts(text, length, "    ---")) return TONE_WARNING;
    if (starts(text, length, "  PASS ")) return TONE_SUCCESS;
    if (starts(text, length, "  FAIL ") || starts(text, length, "  Note:") || starts(text, length, "  NOT REPORTED") ||
        starts(text, length, "  UNVERIFIED") || starts(text, length, "readiness: revoked")) return TONE_WARNING;
    return TONE_BASE;
}

static void render_text(struct app *app, struct native_review *review, const char *text, size_t *row)
{
    const char *end;
    bool diff = false;
    while (*text) {
        size_t length;
        end = strchr(text, '\n');
        if (!end) end = text + strlen(text);
        length = (size_t)(end - text);
        if (heading_line(text, length)) {
            diff = starts(text, length, "DIFF");
            if (review->heading_count < REVIEW_HEADINGS) review->headings[review->heading_count++] = *row;
        }
        style(app, line_tone(text, length, diff));
        wrapped_line(app, text, length, row, review->scroll);
        style(app, TONE_BASE);
        text = *end ? end + 1U : end;
    }
}

static void render_references(struct app *app, struct native_review *review, size_t *row)
{
    struct review_document *document = review->document;
    struct review_reference *ref;
    char heading[128];
    if (!document->ref_count) { render_text(app, review, "No explicit references recorded\n", row); return; }
    ref = &document->refs[review->reference];
    snprintf(heading, sizeof(heading), "Reference %zu/%zu: %s / %s", review->reference + 1U, document->ref_count, ref->kind, ref->state);
    wrapped_line(app, heading, strlen(heading), row, review->scroll);
    wrapped_line(app, ref->locator, strlen(ref->locator), row, review->scroll);
    if (review->preview) render_text(app, review, ref->length ? ref->preview : "No local preview available\n", row);
    else render_text(app, review, "j/k selects; Enter follows reference\n", row);
}

static void render_identity(struct app *app, struct native_review *review, size_t *row)
{
    const struct native_review_identity *id = &review->identity;
    const char *labels[] = {"Source", "Kind", "Project", "Host", "Task", "Run", "Step", "Attempt",
        "Head", "Instance", "Request", "Binding", "Requested revision SHA256", "Identity SHA256"};
    const char *values[] = {id->source, id->kind, id->project, id->host, id->task, id->run, id->step, id->attempt,
        id->head, id->instance, id->request, id->binding, id->revision, id->identity};
    for (size_t i = 0U; i < 14U; i++) {
        char line[200];
        snprintf(line, sizeof(line), "%s: %s", labels[i], values[i]);
        wrapped_line(app, line, strlen(line), row, review->scroll);
    }
}

static void render_log(struct app *app, struct native_review *review, size_t *row)
{
    char heading[4200];
    if (snprintf(heading, sizeof(heading), "Full log, %zu bytes%s: %s", review->log_bytes,
                 review->log_tail ? " (the last 1 MiB is shown)" : "", review->log_path) >= (int)sizeof(heading))
        heading[sizeof(heading) - 1U] = '\0';
    style(app, TONE_MUTED); wrapped_line(app, heading, strlen(heading), row, review->scroll); style(app, TONE_BASE);
    render_text(app, review, review->log[0] ? review->log : "The log is empty.\n", row);
}

static const char *review_mode(const struct native_review *review)
{
    if (review->identity_view) return "selected identity";
    if (review->log_view) return "full log";
    return review->references ? "references" : "evidence";
}

bool native_review_render(struct app *app)
{
    struct native_review *review = app->review; size_t row = 0U;
    if (!native_review_active(app)) return false;
    linef(app, "EXACT REVIEW / %s%s", review_mode(review), review->stale ? " / UNAVAILABLE" : "");
    if (review->notice[0]) linef(app, "%s", review->notice);
    if (review->document && !review->stale) {
        style(app, TONE_MUTED);
        linef(app, "n/N section  L log  space/b page  f refs  i IDs  Esc back");
        style(app, TONE_BASE);
    }
    review->heading_count = 0U;
    if (review->identity_view) { render_identity(app, review, &row); review->lines = row; return true; }
    if (!review->document) return true;
    if (review->log_view && review->log) render_log(app, review, &row);
    else if (review->references) render_references(app, review, &row);
    else render_text(app, review, review->document->text, &row);
    review->lines = row;
    return true;
}
