/* Onboarding operator view (H5): recorded `hydra fleet discover|qualify|
 * enroll` envelopes read into the model, typed state and filters, the exact
 * argv of every delegated command and its guards, the screens at 80x24 and
 * 140x40, and a 100-candidate list within the frame budget. Screen captures
 * are written to the evidence directory given as argv[2]. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../../src/tui/internal.h"
#include "../../src/tui/onboarding_flow.h"

static int failures;
static const char *fixtures, *evidence;

static void check(bool ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

static bool load(struct onb_model *m, const char *name) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", fixtures, name);
    return !onb_parse_file(m, path);
}

static size_t typed_count(const struct onb_model *m, enum onb_typed typed) {
    size_t i, n = 0;
    for (i = 0; i < m->candidate_count; i++) if (onb_candidate_typed(m, &m->candidates[i]) == typed) n++;
    return n;
}

static size_t host_status_count(const struct onb_model *m, const char *status) {
    size_t i, n = 0;
    for (i = 0; i < m->intent.host_count; i++) if (!strcmp(m->intent.hosts[i].status, status)) n++;
    return n;
}

/* ---- Records ---- */

static void record_cases(struct onb_model *m) {
    const struct onb_candidate *c;
    const struct onb_source_report *s;
    check(load(m, "discover-one.json") && m->last.ok && m->candidate_count == 1 && !strcmp(m->candidates[0].candidate_id, "cand_6275696c64") &&
          !strcmp(m->candidates[0].status, "discovered") && onb_candidate_typed(m, &m->candidates[0]) == ONB_NEW && !m->candidates[0].qualified,
          "discover-one: one new candidate with its identity, not qualified");
    check(load(m, "qualify-one.json") && m->candidate_count == 1 && onb_candidate_typed(m, &m->candidates[0]) == ONB_QUALIFIED &&
          !strcmp(m->candidates[0].fingerprint, "SHA256:nThbg6kXUpJWGl7E1IGOCspRomTxdCARLviKw6E5SY8") &&
          !strcmp(onb_candidate_platform(&m->candidates[0]), "linux-x86_64") && !strcmp(onb_candidate_source(&m->candidates[0]), "ssh") &&
          m->candidates[0].resolution_ok && !strcmp(m->candidates[0].user, "root") && m->complete && !strcmp(m->capability, "list"),
          "qualify-one: qualified with fingerprint, platform, principal and source");
    check(load(m, "qualify-two.json") && m->candidate_count == 2 && typed_count(m, ONB_QUALIFIED) == 1 && typed_count(m, ONB_REVOKED) == 1 &&
          m->revoked_count == 1, "qualify-two: a revoked identity is typed revoked beside a qualified one");
    c = &m->candidates[1];
    check(!strcmp(c->candidate_id, "cand_6f74686572") && c->revocation.present && !strcmp(c->revocation.reason, "retired") &&
          !strcmp(c->revocation.scope, "candidate") && c->revocation.revoked_at > 0, "qualify-two: the revocation keeps scope, reason and time");
    check(load(m, "qualify-mixed.json") && !m->last.ok && !strcmp(m->last.code, "partial_failure") && m->candidate_count == 10 && m->partial &&
          typed_count(m, ONB_QUALIFIED) == 7 && typed_count(m, ONB_FAILED) == 2 && typed_count(m, ONB_REVOKED) == 1,
          "qualify-mixed: ten rows retained with 7 qualified, 2 failed, 1 revoked");
    check(!strcmp(m->candidates[3].code, "offline") && !strcmp(m->candidates[4].code, "capability_unavailable"),
          "qualify-mixed: each failure keeps its typed code");
    check(load(m, "qualify-50.json") && m->candidate_count == 50 && typed_count(m, ONB_QUALIFIED) == 16 && typed_count(m, ONB_NEW) == 34 &&
          m->pending == 34 && m->remaining == 34 && !m->complete, "qualify-50: an interrupted batch keeps all 50 rows, 16 qualified and 34 pending");
    check(load(m, "qualify-50-complete.json") && m->candidate_count == 50 && typed_count(m, ONB_QUALIFIED) == 50 && m->complete,
          "qualify-50-complete: every row qualified");
    check(load(m, "discover-100.json") && m->candidate_count == 100 && typed_count(m, ONB_NEW) == 100 && !strcmp(onb_candidate_source(&m->candidates[0]), "inventory") &&
          strstr(m->candidates[0].sources[0].labels, "pool:many"), "discover-100: one hundred discovered candidates with their labels");
    check(load(m, "discover-ansible-first.json") && m->source_count == 1 && m->sources[0].first_observation && m->sources[0].added_count == 3 &&
          m->sources[0].credentials_omitted == 1 && !m->sources[0].cached, "discover-ansible-first: a first observation counts its added records and omitted credentials");
    check(load(m, "discover-ansible.json") && m->candidate_count == 1 && !strcmp(onb_candidate_source(&m->candidates[0]), "ansible-inventory") &&
          m->record_count == 3 && m->source_count == 1 && m->diff_requested, "discover-ansible: the imported candidate and the reported records");
    s = &m->sources[0];
    check(s->cached && s->added_count == 2 && s->changed_count == 1 && s->conflicting_count == 2 && s->revoked_count == 1 && s->added_n == 2 &&
          !strcmp(s->added[1], "web5") && s->changed_n == 1 && !strcmp(s->changed[0].name, "web3") && !strcmp(s->changed[0].previous_target, "third") &&
          !strcmp(s->changed[0].target, "fourth"), "discover-ansible: the snapshot diff lists added, changed and conflicting records with the cache");
    check(!strcmp(m->records[0].reason, "duplicate_target") && !m->records[0].revocation.present && m->records[2].revocation.present &&
          !strcmp(m->records[2].name, "web2"), "discover-ansible: conflicting and revoked records keep their provenance");
}

static void intent_cases(struct onb_model *m) {
    const struct onb_host *h;
    check(load(m, "qualify-one.json") && load(m, "review-one.json") && m->intent.loaded && m->intent.host_count == 1 && onb_hex_digest(m->intent.digest),
          "review-one: the intent loads with its digest");
    h = &m->intent.hosts[0];
    check(!strcmp(h->alias, "build") && !strcmp(h->candidate_id, "cand_6275696c64") && !strcmp(h->project, "/tmp/hydra-onboarding-fixture/project") &&
          onb_hex_digest(h->package_sha256) && !strcmp(h->prefix, "/tmp/hydra-onboarding-fixture/prefix") && h->fingerprint[0] && !strcmp(h->status, "not_started"),
          "review-one: alias, project, package digest, prefix and fingerprint are the bindings shown");
    check(onb_candidate_typed(m, &m->candidates[0]) == ONB_REVIEWED, "a reviewed candidate is typed reviewed, not enrolled");
    check(load(m, "status-not-started.json") && m->intent.loaded && !m->intent.progress_recorded && !m->intent.hosts[0].attempted &&
          !strcmp(m->intent.hosts[0].status, "not_started") && !strcmp(m->intent.path, "/tmp/hydra-onboarding-fixture/intent-1.json"),
          "status-not-started: no progress recorded, nothing attempted");
    check(load(m, "apply-one.json") && !strcmp(m->intent.hosts[0].status, "enrolled") && !strcmp(m->intent.hosts[0].phase, "complete") &&
          m->intent.hosts[0].attempted && onb_candidate_typed(m, &m->candidates[0]) == ONB_ENROLLED, "apply-one: the apply result merges into the intent and the candidate is enrolled");
    check(load(m, "status-enrolled.json") && m->intent.progress_recorded && !strcmp(m->intent.hosts[0].status, "enrolled") && m->intent.progress_path[0] == '/',
          "status-enrolled: recorded progress names its file");
    check(load(m, "qualify-mixed.json") && load(m, "status-mixed.json") && m->intent.host_count == 7 && host_status_count(m, "enrolled") == 6 &&
          host_status_count(m, "outcome_unknown") == 1 && typed_count(m, ONB_ENROLLED) == 6 && typed_count(m, ONB_UNKNOWN) == 1 &&
          typed_count(m, ONB_FAILED) == 2 && typed_count(m, ONB_REVOKED) == 1 && m->candidate_count == 10,
          "status-mixed: ten rows keep success, unknown, failed and revoked outcomes");
    check(load(m, "qualify-50-complete.json") && load(m, "status-50.json") && m->intent.host_count == 50 && host_status_count(m, "enrolled") == 15 &&
          host_status_count(m, "outcome_unknown") == 1 && host_status_count(m, "not_started") == 34 && typed_count(m, ONB_ENROLLED) == 15 &&
          typed_count(m, ONB_UNKNOWN) == 1 && typed_count(m, ONB_REVIEWED) == 34 && m->intent.partial,
          "status-50: an interrupted apply keeps every row with its success, unknown or not-started outcome");
    check(load(m, "review-revoked.json") && !m->last.ok && !strcmp(m->last.code, "revoked") && m->intent.host_count == 50,
          "review-revoked: a refused review leaves the loaded intent alone");
    check(load(m, "apply-wrong-digest.json") && !strcmp(m->last.code, "invalid_intent") && host_status_count(m, "enrolled") == 15,
          "apply-wrong-digest: a refused apply changes no row");
    check(load(m, "status-wrong-input.json") && !strcmp(m->last.code, "invalid_intent"), "status-wrong-input: a non-intent is typed");
}

/* ---- Filters ---- */

static void filter_cases(struct onb_model *m) {
    struct onb_filter f;
    struct onb_row rows[ONB_CANDIDATES + ONB_RECORDS];
    char value[64] = "";
    memset(&f, 0, sizeof(f)); f.typed = -1;
    check(load(m, "qualify-mixed.json") && load(m, "status-mixed.json") && onb_rows(m, &f, rows, 200) == 10, "no filter lists every row");
    f.typed = ONB_ENROLLED;
    check(onb_rows(m, &f, rows, 200) == 6, "the state filter keeps enrolled rows");
    f.typed = ONB_UNKNOWN;
    check(onb_rows(m, &f, rows, 200) == 1 && !strcmp(m->candidates[rows[0].index].target, "mixed00"), "the state filter isolates the unknown outcome");
    f.typed = -1; copy_text(f.platform, sizeof(f.platform), "unknown");
    check(onb_rows(m, &f, rows, 200) == 2, "the platform filter keeps rows whose platform was never observed");
    f.platform[0] = '\0'; copy_text(f.label, sizeof(f.label), "rack:1");
    check(onb_rows(m, &f, rows, 200) == 3, "the label filter matches source labels");
    f.label[0] = '\0'; copy_text(f.search, sizeof(f.search), "mixed0");
    check(onb_rows(m, &f, rows, 200) == 10 && (copy_text(f.search, sizeof(f.search), "cand_6d697865643035"), onb_rows(m, &f, rows, 200) == 1),
          "search matches targets and candidate IDs");
    memset(&f, 0, sizeof(f)); f.typed = -1;
    onb_next_platform(m, value, sizeof(value));
    check(!strcmp(value, "linux-x86_64") && (onb_next_platform(m, value, sizeof(value)), !strcmp(value, "unknown")) &&
          (onb_next_platform(m, value, sizeof(value)), !value[0]), "the platform cycle visits each distinct platform then all");
    check(load(m, "discover-ansible.json") && (f.typed = ONB_CONFLICTING, onb_rows(m, &f, rows, 200) == 2) && (f.typed = ONB_REVOKED, onb_rows(m, &f, rows, 200) == 1) &&
          (f.typed = ONB_NEW, onb_rows(m, &f, rows, 200) == 1), "reported records are rows typed conflicting or revoked");
    value[0] = '\0'; onb_next_source(m, value, sizeof(value));
    check(!strcmp(value, "ansible-inventory"), "the source cycle names the live source");
}

/* ---- argv ---- */

static bool words(const struct onb_argv *a, const char *expected) {
    char joined[4096] = "";
    size_t i;
    for (i = 0; i < a->count; i++) { if (i) strcat(joined, " "); strncat(joined, a->words[i], sizeof(joined) - strlen(joined) - 2); }
    return !strcmp(joined, expected);
}

static void argv_cases(struct onb_model *m) {
    struct onb_argv a;
    struct onb_selection s;
    struct onb_review r;
    const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    memset(&s, 0, sizeof(s)); memset(&r, 0, sizeof(r));
    copy_text(s.aliases, sizeof(s.aliases), "build mac");
    check(onb_discover_argv(&a, &s, false, NULL) && words(&a, "hydra fleet discover --ssh build --ssh mac --diff --json"), "discover argv names each alias and --diff");
    check(onb_discover_argv(&a, &s, true, "/home/u/.hydra/fleet/onboarding/qualification-1.json") &&
          words(&a, "hydra fleet qualify --ssh build --ssh mac --require list --progress /home/u/.hydra/fleet/onboarding/qualification-1.json --json") &&
          onb_discover_argv_valid(&a, "/home/u/.hydra/fleet/onboarding"), "qualify argv records its export under the onboarding directory");
    check(!onb_discover_argv_valid(&a, "/home/u/.hydra/fleet/other"), "an export outside the onboarding directory is refused");
    copy_text(s.aliases, sizeof(s.aliases), "a b c d e f g h i j k l m n o p q");
    check(!onb_discover_argv(&a, &s, false, NULL), "more than 16 aliases are refused");
    memset(&s, 0, sizeof(s)); copy_text(s.inventory, sizeof(s.inventory), "/inv.json");
    check(!onb_discover_argv(&a, &s, false, NULL) && (copy_text(s.names, sizeof(s.names), "h1 h2"), onb_discover_argv(&a, &s, false, NULL)) &&
          words(&a, "hydra fleet discover --inventory /inv.json --select h1 --select h2 --diff --json"), "an inventory needs selected names");
    memset(&s, 0, sizeof(s)); copy_text(s.source, sizeof(s.source), "ssh-config");
    check(onb_discover_argv(&a, &s, false, NULL) && words(&a, "hydra fleet discover --source ssh-config --diff --json") && !onb_discover_argv(&a, &s, true, "/x/q.json"),
          "a live source discovers alone but qualifies only with selected names");
    check(load(m, "qualify-one.json") && onb_export_selection(m, &s) && !strcmp(s.aliases, "build") && !s.inventory[0] && !s.source[0],
          "the loaded export's selection is reconstructed for Q");
    check(load(m, "qualify-50.json") && onb_export_selection(m, &s) && !strcmp(s.inventory, "/Users/you/onboarding/fifty.json") && strstr(s.names, "fifty49") &&
          !strncmp(s.names, "fifty00 fifty01", 15), "an inventory selection is reconstructed with every name");
    check(load(m, "discover-ansible.json") && onb_export_selection(m, &s) && !strcmp(s.source, "ansible-inventory:/Users/you/onboarding/hosts.ini") && !strcmp(s.names, "web1"),
          "a live source selection is reconstructed");
    copy_text(r.project, sizeof(r.project), "/srv/project"); copy_text(r.alias, sizeof(r.alias), "build");
    check(onb_review_argv(&a, "/d/qualification-1.json", "cand_6275696c64", &r, "/d/intent-1.json") &&
          words(&a, "hydra fleet enroll review --input /d/qualification-1.json --candidate cand_6275696c64 --project /srv/project --alias build --output /d/intent-1.json --json"),
          "review argv binds the export, candidate, project, alias and output");
    copy_text(r.package, sizeof(r.package), "/d/package"); copy_text(r.sha256, sizeof(r.sha256), digest); copy_text(r.prefix, sizeof(r.prefix), "/opt/hydra");
    check(onb_review_argv(&a, "/d/q.json", "cand_6275696c64", &r, "/d/i.json") && strstr(a.words[12], "--package") && !strcmp(a.words[15], digest) && !strcmp(a.words[17], "/opt/hydra"),
          "a pinned package carries its digest and exact prefix");
    r.sha256[0] = '\0';
    check(!onb_review_argv(&a, "/d/q.json", "cand_6275696c64", &r, "/d/i.json") && !onb_review_argv(&a, "q.json", "cand_x", &r, "/d/i.json"),
          "a package without its digest or a relative export is refused");
    check(onb_apply_argv(&a, "/d/intent-1.json", digest) && words(&a, "hydra fleet enroll apply --input /d/intent-1.json --confirm 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef --json") &&
          onb_apply_argv_valid(&a, "/d/intent-1.json", digest), "apply argv is exactly the intent and its digest");
    copy_text(a.words[7], ONB_ARG_TEXT, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdee");
    check(!onb_apply_argv_valid(&a, "/d/intent-1.json", digest), "a changed digest never validates");
    (void)onb_apply_argv(&a, "/d/intent-1.json", digest); copy_text(a.words[5], ONB_ARG_TEXT, "/d/intent-2.json");
    check(!onb_apply_argv_valid(&a, "/d/intent-1.json", digest), "a changed intent path never validates");
    (void)onb_apply_argv(&a, "/d/intent-1.json", digest); (void)onb_argv_add(&a, "--force");
    check(!onb_apply_argv_valid(&a, "/d/intent-1.json", digest) && !onb_apply_argv(&a, "/d/i.json", "abc"), "extra words or a short digest never validate");
    check(onb_digest_confirms("0123456789ab", digest) && onb_digest_confirms(digest, digest) && !onb_digest_confirms("0123456789a", digest) &&
          !onb_digest_confirms("0123456789ac", digest) && !onb_digest_confirms("", digest), "the typed confirmation is a matching prefix of at least 12 characters");
    check(onb_status_argv(&a, "/d/intent-1.json") && words(&a, "hydra fleet enroll status --input /d/intent-1.json --json"), "status argv is read-only");
}

/* ---- Screens ---- */

static bool render_capture(struct app *app, char *out, size_t size) {
    FILE *capture = tmpfile();
    int saved = dup(STDOUT_FILENO);
    size_t n;
    if (!capture || saved < 0) { if (capture) fclose(capture); if (saved >= 0) close(saved); return false; }
    fflush(stdout);
    dup2(fileno(capture), STDOUT_FILENO);
    render(app, 1, true);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO); close(saved);
    if (fseek(capture, 0, SEEK_SET)) { fclose(capture); return false; }
    n = fread(out, 1, size - 1, capture);
    out[n] = '\0';
    fclose(capture);
    return n > 0;
}

static void keep(const char *name, int cols, int rows, const char *text) {
    char path[4096];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s-%dx%d.txt", evidence, name, cols, rows);
    if ((f = fopen(path, "w"))) { fputs(text, f); fclose(f); }
}

/* Every rendered row fits the width (the frame marks each row with |). */
static bool fits(const char *frame, int cols) {
    const char *line = frame;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line), columns = 0, i;
        for (i = 0; i < length; i++) if (((unsigned char)line[i] & 0xc0) != 0x80) columns++;
        if (strncmp(line, "FRAME ", 6) && columns > (size_t)cols) return false;
        line = end ? end + 1 : NULL;
    }
    return true;
}

static void render_all(struct app *app, char *all, size_t size) {
    char frame[65536];
    all[0] = '\0';
    app->onboarding->scroll = 0;
    do {
        if (!render_capture(app, frame, sizeof(frame))) return;
        if (strlen(all) + strlen(frame) + 1 < size) strcat(all, frame);
        app->onboarding->scroll += 4;
    } while (app->onboarding->more && app->onboarding->scroll < 400);
    app->onboarding->scroll = 0;
}

static void screen_case(struct app *app, const char *label, enum onb_screen screen, const char *const must[], size_t count, int cols, int rows) {
    static char all[1 << 20];
    char frame[65536], name[160];
    size_t i;
    bool ok = true;
    app->cols = cols; app->rows = rows;
    app->onboarding->open = true; app->onboarding->screen = screen; app->onboarding->notice[0] = '\0';
    render_all(app, all, sizeof(all));
    (void)render_capture(app, frame, sizeof(frame));
    keep(label, cols, rows, frame);
    for (i = 0; i < count; i++) if (!strstr(all, must[i])) { ok = false; fprintf(stderr, "  missing: %s\n", must[i]); }
    snprintf(name, sizeof(name), "%s screen at %dx%d shows what it must and fits", label, cols, rows);
    check(ok && fits(frame, cols) && strstr(frame, "[Hosts]"), name);
}

static void screens_at(struct app *app, int cols, int rows) {
    static const char *const list_must[] = {"ONBOARDING", "mixed00", "outcome unknown", "mixed05", "revoked", "failed: offline", "enrolled"};
    static const char *const detail_must[] = {"CANDIDATE DETAIL", "cand_6d697865643030", "SHA256:", "linux-x86_64", "Provenance", "outcome unknown", "Operation"};
    static const char *const revoked_must[] = {"cand_6d697865643035", "Revoked", "decommissioned", "inspected here and nowhere else"};
    static const char *const changes_must[] = {"SNAPSHOT CHANGES", "ansible-inventory", "2 added, 0 removed,", "1 changed,", "web3: third -> fourth", "duplicate_target"};
    static const char *const record_must[] = {"REPORTED RECORD", "web2", "retired", "inspected here and nowhere else"};
    static const char *const intent_must[] = {"ENROLLMENT INTENT", "Intent digest", "Apply command", "hydra fleet enroll apply --input", "--confirm", "Package", "prefix", "alias build", "Fingerprint"};
    static const char *const select_must[] = {"DISCOVER CANDIDATES", "SSH aliases", "Live source", "no connection"};
    static const char *const review_must[] = {"REVIEW ENROLLMENT OF cand_6275696c64", "Remote project", "Package SHA-256", "Nothing is applied here"};
    static const char *const error_must[] = {"NOT DONE", "revoked", "never enters a new intent"};
    struct native_onboarding *s = app->onboarding;
    check(load(s->model, "qualify-mixed.json") && load(s->model, "status-mixed.json"), "mixed fixtures load for the screens");
    s->filter.typed = -1; s->selected = 0;
    screen_case(app, "list-mixed", ONB_SCREEN_LIST, list_must, 7, cols, rows);
    s->detail_record = false; s->detail_index = 0;
    screen_case(app, "detail-unknown", ONB_SCREEN_DETAIL, detail_must, 7, cols, rows);
    s->detail_index = 5;
    screen_case(app, "detail-revoked", ONB_SCREEN_DETAIL, revoked_must, 4, cols, rows);
    screen_case(app, "intent-mixed", ONB_SCREEN_INTENT, intent_must + 0, 5, cols, rows);
    check(load(s->model, "discover-ansible.json"), "ansible fixture loads");
    screen_case(app, "changes", ONB_SCREEN_CHANGES, changes_must, 6, cols, rows);
    s->detail_record = true; s->detail_index = 2;
    screen_case(app, "record-revoked", ONB_SCREEN_DETAIL, record_must, 4, cols, rows);
    check(load(s->model, "qualify-one.json") && load(s->model, "review-one.json"), "one-host fixtures load");
    screen_case(app, "intent-one", ONB_SCREEN_INTENT, intent_must, 9, cols, rows);
    screen_case(app, "select", ONB_SCREEN_SELECT, select_must, 4, cols, rows);
    copy_text(s->candidate, sizeof(s->candidate), "cand_6275696c64");
    screen_case(app, "review", ONB_SCREEN_REVIEW, review_must, 4, cols, rows);
    check(load(s->model, "review-revoked.json"), "revoked review fixture loads");
    screen_case(app, "error-revoked", ONB_SCREEN_ERROR, error_must, 3, cols, rows);
}

/* One hundred candidates: every row reachable by paging, filters bounded,
 * and frames within budget. */
static void hundred_case(struct app *app, int cols, int rows) {
    struct native_onboarding *s = app->onboarding;
    char frame[65536], name[96];
    struct timespec start, end;
    double elapsed;
    int i;
    app->cols = cols; app->rows = rows;
    check(load(s->model, "discover-100.json") && s->model->candidate_count == 100, "100-candidate fixture loads");
    s->open = true; s->screen = ONB_SCREEN_LIST; s->selected = 0; s->filter.typed = -1; s->notice[0] = '\0';
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "many00") && strstr(frame, "100 candidates") && fits(frame, cols), "the first page lists the first candidates");
    keep("list-100-first", cols, rows, frame);
    s->selected = 99;
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "> many99") && strstr(frame, "of 100"), "the last page reaches the last candidate");
    keep("list-100-last", cols, rows, frame);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (i = 0; i < 50; i++) { s->selected = (size_t)(i * 2); (void)render_capture(app, frame, sizeof(frame)); }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
    snprintf(name, sizeof(name), "50 frames of the 100-candidate list at %dx%d take %.0f ms (budget 2500 ms)", cols, rows, elapsed * 1000.0);
    check(elapsed < 2.5, name);
    copy_text(s->filter.label, sizeof(s->filter.label), "rack:3"); s->selected = 0;
    check(render_capture(app, frame, sizeof(frame)) && strstr(frame, "of 25") && strstr(frame, "label:rack:3"), "a label filter narrows the hundred to its rows");
    s->filter.label[0] = '\0';
}

/* ---- Keys (no child ever starts here) ---- */

static void press(struct app *app, const char *keys) {
    for (; *keys; keys++) (void)native_onboarding_byte(app, (unsigned char)*keys);
    native_onboarding_flush_input(app);
}

static void key_cases(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    app->cols = 100; app->rows = 30; app->view = 6;
    check(load(s->model, "qualify-mixed.json") && load(s->model, "status-mixed.json"), "key fixtures load");
    s->open = true; s->screen = ONB_SCREEN_LIST; s->selected = 0; s->filter.typed = -1; s->notice[0] = '\0';
    press(app, "s");
    check(s->filter.typed == ONB_NEW && (press(app, "s"), s->filter.typed == ONB_QUALIFIED), "s cycles the state filter");
    press(app, "x");
    check(s->filter.typed == -1, "x clears the filters");
    press(app, "jjjjj");
    check(s->selected == 5 && !strcmp(s->model->candidates[5].status, "revoked"), "j moves the selection to the revoked row");
    press(app, "r");
    check(s->screen == ONB_SCREEN_LIST && !s->job.active && strstr(s->notice, "revoked") && strstr(s->notice, "inspected"), "r on a revoked identity offers inspection only");
    press(app, "\r");
    check(s->screen == ONB_SCREEN_DETAIL && s->detail_index == 5 && !s->detail_record, "Enter opens the revoked candidate's detail");
    press(app, "\033"); native_onboarding_flush_input(app);
    check(s->screen == ONB_SCREEN_LIST, "Esc returns to the list");
    press(app, "kk");
    press(app, "r");
    check(s->screen == ONB_SCREEN_LIST && !s->job.active && strstr(s->notice, "compatible"), "r on a failed candidate runs nothing");
    s->selected = 1; copy_text(s->model->export_path, sizeof(s->model->export_path), "/nonexistent/qualification-3.json");
    press(app, "r");
    check(s->screen == ONB_SCREEN_REVIEW && !s->job.active && !strcmp(s->candidate, "cand_6d697865643031"), "r on a qualified candidate opens the review form without running anything");
    press(app, "\r\r\r\r\r");
    check(s->screen == ONB_SCREEN_REVIEW && !s->job.active && strstr(s->problem, "required"), "an empty project is caught before anything runs");
    press(app, "\033"); native_onboarding_flush_input(app);
    press(app, "i");
    check(s->screen == ONB_SCREEN_INTENT && !s->job.active, "i shows the loaded intent");
    press(app, "\033"); native_onboarding_flush_input(app);
    s->directory_known = false;
    press(app, "Q");
    check(!s->job.active && strstr(s->notice, "directory"), "Q without the onboarding directory runs nothing");
    s->directory_known = true; copy_text(s->directory, sizeof(s->directory), "/nonexistent/hydra-onboarding");
    s->model->candidate_count = 0; s->model->record_count = 0;
    press(app, "Q");
    check(s->screen == ONB_SCREEN_SELECT && s->form_qualify && !s->job.active, "Q with nothing listed opens the selection form in qualify mode");
    press(app, "\033"); native_onboarding_flush_input(app);
    press(app, "f");
    check(s->screen == ONB_SCREEN_SELECT && !s->form_qualify, "f opens the selection form for discovery");
    press(app, "\r\r\r\r\r");
    check(s->screen == ONB_SCREEN_SELECT && !s->job.active && s->problem[0], "an empty selection is caught before anything runs");
    press(app, "\033"); native_onboarding_flush_input(app);
    press(app, "\033"); native_onboarding_flush_input(app);
    check(!s->open, "Esc on the list closes the view");
}

int main(int argc, char **argv) {
    struct app *app = calloc(1, sizeof(*app));
    struct onb_model *m = calloc(1, sizeof(*m));
    if (argc != 3 || !app || !m) { free(app); free(m); fputs("usage: test-tui-onboarding FIXTURES EVIDENCE\n", stderr); return 2; }
    fixtures = argv[1]; evidence = argv[2];
    (void)mkdir(evidence, 0700);
    record_cases(m);
    intent_cases(m);
    filter_cases(m);
    argv_cases(m);
    app->hydra = "hydra"; app->view = 6; app->no_color = true; app->ascii = true;
    if (!native_onboarding_state(app)) { free(app); free(m); return 1; }
    screens_at(app, 80, 24);
    screens_at(app, 140, 40);
    hundred_case(app, 80, 24);
    hundred_case(app, 140, 40);
    key_cases(app);
    native_onboarding_destroy(app);
    frame_free(app); transcript_free(app->transcript);
    free(app); free(m);
    printf("%s: %d failure%s\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
