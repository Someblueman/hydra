#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "onboarding_flow.h"
/* Headless onboarding (H5): the view opened on recorded envelopes for the
 * fixture renderer, and the model dump that tests/test_onboarding_view.sh
 * compares with the CLI's JSON. No child runs here. */

int native_onboarding_fixture(struct app *app, const char *export_file, const char *intent_file, const char *screen) {
    struct native_onboarding *s = native_onboarding_state(app);
    if (!s) return 2;
    s->open = true; s->directory_known = true; s->loaded = true;
    copy_text(s->directory, sizeof(s->directory), "/fixture/onboarding");
    if (export_file && (onb_parse_file(s->model, export_file) || !s->model->last.parsed)) { fprintf(stderr, "onboarding fixture %s is not a Hydra envelope\n", export_file); return 2; }
    if (export_file) copy_text(s->model->export_path, sizeof(s->model->export_path), export_file);
    if (intent_file && (onb_parse_file(s->model, intent_file) || !s->model->intent.loaded)) { fprintf(stderr, "onboarding intent fixture %s is not a review or status envelope\n", intent_file); return 2; }
    if (intent_file) copy_text(s->intent_path, sizeof(s->intent_path), s->model->intent.path[0] ? s->model->intent.path : intent_file);
    s->screen = ONB_SCREEN_LIST;
    if (screen && !strcmp(screen, "detail")) { s->selected = 0; onb_open_detail(s); }
    else if (screen && !strcmp(screen, "changes")) s->screen = ONB_SCREEN_CHANGES;
    else if (screen && !strcmp(screen, "intent")) s->screen = ONB_SCREEN_INTENT;
    else if (screen && !strcmp(screen, "select")) s->screen = ONB_SCREEN_SELECT;
    else if (screen && !strcmp(screen, "review")) { copy_text(s->candidate, sizeof(s->candidate), s->model->candidate_count ? s->model->candidates[0].candidate_id : ""); s->screen = ONB_SCREEN_REVIEW; }
    else if (screen && strcmp(screen, "list")) { fprintf(stderr, "unknown onboarding screen %s\n", screen); return 2; }
    return 0;
}

void native_onboarding_dump(struct app *app, FILE *out) {
    struct native_onboarding *s = app->onboarding;
    struct onb_row rows[ONB_CANDIDATES + ONB_RECORDS];
    size_t count, i;
    if (!s) return;
    count = onb_rows(s->model, &s->filter, rows, ONB_CANDIDATES + ONB_RECORDS);
    fprintf(out, "HYDRA_ONBOARDING\t1\n");
    for (i = 0; i < count; i++) {
        if (rows[i].kind == ONB_ROW_CANDIDATE) {
            const struct onb_candidate *c = &s->model->candidates[rows[i].index];
            const struct onb_host *h = onb_host_for(s->model, c->candidate_id);
            fprintf(out, "C\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", c->candidate_id, c->target, c->status, onb_typed_name(onb_candidate_typed(s->model, c)),
                    onb_candidate_source(c), onb_candidate_platform(c), h ? h->alias : "-", h ? h->status : "-");
        } else {
            const struct onb_record *r = &s->model->records[rows[i].index];
            fprintf(out, "N\t%s\t%s\t%s\t%s\t%s\n", r->name, r->target, r->reason, r->revocation.present ? "revoked" : "conflicting", r->source);
        }
    }
    fprintf(out, "END\t%zu\n", count);
}
