#include "fleet/workflow/workflow_attention.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define WA_RUNS 32U
#define WA_STEPS 512U
#define WA_ITEMS 512U
struct wa {
    json_object    *items;
    const char     *project;
    char            root[F_PATH];
    size_t          runs, steps;
    bool            partial, truncated;
};
static json_object *canonical(json_object *value);
static int key_order(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static json_object *canonical(json_object *value)
{
    json_object *copy;
    if (!value) return NULL;
    if (json_object_is_type(value, json_type_array)) {
        size_t i; copy = json_object_new_array();
        for (i = 0; i < json_object_array_length(value); i++) json_object_array_add(copy, canonical(json_object_array_get_idx(value, i)));
        return copy;
    }
    if (!json_object_is_type(value, json_type_object)) return json_object_get(value);
    { size_t i, n = json_object_object_length(value), used = 0; char **keys = calloc(n ? n : 1, sizeof(*keys));
        if (!keys) return NULL;
        json_object_object_foreach(value, key, child) { (void)child; keys[used++] = key; }
        qsort(keys, used, sizeof(*keys), key_order); copy = json_object_new_object();
        for (i = 0; i < used; i++) json_object_object_add(copy, keys[i], canonical(f_field(value, keys[i])));
        free(keys); return copy;
    }
}

static bool
id(const char *s, const char *prefix)
{
    size_t          i, n = strlen(prefix);
    if (!s || strncmp(s, prefix, n) || !s[n])
        return false;
    for (i = n; s[i]; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return true;
}
static bool
reg(const char *p)
{
    struct stat     st;
    return !lstat(p, &st) && S_ISREG(st.st_mode);
}
static bool
directory(const char *p)
{
    struct stat     st;
    return !lstat(p, &st) && S_ISDIR(st.st_mode);
}
static char *
scalar(const char *p)
{
    char           *s, *n;
    if (!reg(p) || !(s = f_read(p, 4096)))
        return NULL;
    n = strchr(s, '\n');
    if (n) {
        *n = 0;
        if (n[1]) {
            free(s);
            return NULL;
        }
    } return s;
}
static char *
scalar_at(const char *directory, const char *name)
{
    char path[F_PATH];
    return f_path(path, sizeof(path), directory, name) ? NULL : scalar(path);
}
static void
nullable(json_object * o, const char *k, const char *v)
{
    json_object_object_add(o, k, v && *v ? json_object_new_string(v) : json_object_new_null());
}
static bool
number(const char *s, int64_t * out)
{
    char           *e;
    uintmax_t       n;
    size_t          i;
    if (!s || !*s)
        return false;
    for (i = 0; s[i]; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    errno = 0;
    n = strtoumax(s, &e, 10);
    if (errno == ERANGE || *e || n > INT64_MAX)
        return false;
    *out = (int64_t) n;
    return true;
}
static bool
binding_identity_ok(const char *p, const char *head, const char *instance)
{
    char *s, *line, *save = NULL;
    unsigned heads = 0, instances = 0;
    bool head_ok = false, instance_ok = false;
    if (!reg(p) || (s = f_read(p, F_LIMIT)) == NULL) return false;
    for (line = strtok_r(s, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        if (!strcmp(line, "head")) {
            heads++;
            head_ok = heads == 1 && !strcmp(tab + 1, head);
        } else if (!strcmp(line, "instance")) {
            instances++;
            instance_ok = instances == 1 && !strcmp(tab + 1, instance);
        }
    }
    free(s);
    return heads == 1 && instances == 1 && head_ok && instance_ok;
}
static char    *
pair_value(const char *p, const char *key)
{
    char           *s, *line, *save = NULL, *value = NULL;
    if (!reg(p) || !(s = f_read(p, F_LIMIT)))
        return NULL;
    for (line = strtok_r(s, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char           *tab = strchr(line, '\t');
        if (tab && (size_t) (tab - line) == strlen(key) && !strncmp(line, key, strlen(key))) {
            value = strdup(tab + 1);
            break;
        }
    }
    free(s);
    return value;
}
static bool
hash_text(const char *text, char digest[65])
{
    size_t length = strlen(text);
    if (!length || text[length - 1] != '\n') return false;
    length--;
    if (length != 40 && length != 64) return false;
    for (size_t i = 0; i < length; i++)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
    memcpy(digest, text, length); digest[length] = 0;
    return true;
}
static bool
git_hash(const char *path, char digest[65])
{
    char           *argv[] = {"git", "hash-object", (char *)path, NULL};
    struct f_capture cap = {0};
    bool            ok = !f_run(argv, NULL, 0, 30, &cap) && !cap.status && cap.out;
    if (ok) ok = hash_text(cap.out, digest);
    f_capture_free(&cap);
    return ok;
}
static bool
head_storage(struct wa *w, char project_dir[F_PATH], char project_path[F_PATH], char heads[F_PATH], DIR **dir)
{
    if (f_path(project_dir, F_PATH, w->root, "projects") || f_path(project_path, F_PATH, project_dir, w->project) ||
        f_path(heads, F_PATH, project_path, "heads") || !directory(heads)) return false;
    *dir = opendir(heads);
    return *dir != NULL;
}
static bool head_resolve(struct wa *w, const char *requested, char resolved[F_PATH], char **instance_out);
static bool
head_candidate(const char *heads, const char *name, const char *requested, char resolved[F_PATH], char **instance_out)
{
    char path[F_PATH], branch[F_PATH], *branch_value, *instance;
    if (f_path(path, sizeof(path), heads, name) || !directory(path) ||
        f_path(branch, sizeof(branch), path, "branch") || !(branch_value = scalar(branch))) return false;
    if (strcmp(name, requested) && strcmp(branch_value, requested)) {
        free(branch_value);
        return false;
    }
    free(branch_value);
    if (f_path(branch, sizeof(branch), path, "current-instance") || !(instance = scalar(branch))) return false;
    snprintf(resolved, F_PATH, "%s", name);
    *instance_out = instance;
    return true;
}
static char *
head_instance(struct wa *w, const char *head)
{
    char resolved[F_PATH], *instance = NULL;
    return head_resolve(w, head, resolved, &instance) ? instance : NULL;
}
static bool
head_scan_entry(const char *heads, const char *requested, const struct dirent *entry,
                char resolved[F_PATH], char **instance_out, bool found)
{
    char *instance = NULL;
    if (entry->d_name[0] == '.' || !id(entry->d_name, "head_")) return true;
    if (!head_candidate(heads, entry->d_name, requested, resolved, &instance)) return true;
    if (found) { free(instance); return false; }
    *instance_out = instance;
    return true;
}
static bool
head_resolve_scan(DIR *dir, const char *heads, const char *requested, char resolved[F_PATH], char **instance_out)
{
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!head_scan_entry(heads, requested, entry, resolved, instance_out, found)) return false;
        if (!*instance_out) continue;
        found = true;
    }
    return found;
}
static bool
head_resolve(struct wa *w, const char *requested, char resolved[F_PATH], char **instance_out)
{
    char project_dir[F_PATH], project_path[F_PATH], heads[F_PATH];
    DIR *dir;
    bool found;
    if (instance_out) *instance_out = NULL;
    if (!requested || !head_storage(w, project_dir, project_path, heads, &dir)) return false;
    found = head_resolve_scan(dir, heads, requested, resolved, instance_out);
    closedir(dir);
    return found;
}
static void
route(json_object * o, const char *kind, const char *run, const char *step, const char *attempt, bool nav)
{
    json_object    *r = json_object_new_object();
    f_string_add(r, "kind", kind);
    nullable(r, "run_id", run);
    nullable(r, "step_id", step);
    nullable(r, "attempt_id", attempt);
    json_object_object_add(r, "navigable", json_object_new_boolean(nav));
    json_object_object_add(r, "read_only", json_object_new_boolean(true));
    json_object_object_add(r, "fresh_action", json_object_new_boolean(false));
    json_object_object_add(o, "route", r);
}
static json_object *
add(struct wa *w, const char *kind, const char *reason, const char *run, const char *step, const char *attempt, const char *head, const char *instance, const char *request, const char *binding, const char *fresh, const char *next, json_object * semantic, bool nav){
    json_object    *o = json_object_new_object(), *rev = json_object_new_object();
    if (json_object_array_length(w->items) >= WA_ITEMS) {
        w->truncated = true;
        w->partial = true;
        json_object_put(o);
        json_object_put(rev);
        return NULL;
    }
    f_string_add(o, "kind", kind);
    f_string_add(o, "source", "workflow records");
    f_string_add(o, "reason", reason);
    f_string_add(o, "next_action", next);
    nullable(o, "project_id", w->project);
    nullable(o, "run_id", run);
    nullable(o, "step_id", step);
    nullable(o, "attempt_id", attempt);
    nullable(o, "head_id", head);
    nullable(o, "current_instance", instance);
    nullable(o, "request_id", request);
    nullable(o, "binding", binding);
    f_string_add(o, "freshness", fresh);
    json_object_object_add(o, "accepted", json_object_new_boolean(false));
    f_string_add(rev, "kind", kind);
    nullable(rev, "project_id", w->project);
    nullable(rev, "run_id", run);
    nullable(rev, "step_id", step);
    nullable(rev, "attempt_id", attempt);
    nullable(rev, "head_id", head);
    nullable(rev, "current_instance", instance);
    nullable(rev, "request_id", request);
    nullable(rev, "binding", binding);
    { json_object *stable = semantic ? canonical(semantic) : json_object_new_null();
        if (!stable && semantic) {
            json_object_put(o); json_object_put(rev); w->partial = true;
            return NULL;
        }
        json_object_object_add(rev, "semantic", stable);
    }
    f_string_add(o, "revision", json_object_to_json_string_ext(rev, JSON_C_TO_STRING_PLAIN));
    json_object_put(rev);
    route(o, !strcmp(kind, "result") ? "workflow-evidence" : "workflow-request", run, step, attempt, nav);
    json_object_array_add(w->items, o);
    return o;
}
static void
unknown(struct wa *w, const char *reason, const char *run, const char *step, const char *attempt, const char *head, const char *instance, const char *request, const char *binding, json_object * semantic)
{
    w->partial = true;
    if (json_object_array_length(w->items) < WA_ITEMS)
        add(w, "unknown", reason, run, step, attempt, head, instance, request, binding, "unknown", "inspect recorded workflow evidence", semantic, false);
    else
        w->truncated = true;
}
static int
expiry(const char *s)
{
    int64_t         n;
    if (!number(s, &n))
        return 2;
    if (!n)
        return 0;
    return n <= (int64_t) time(NULL) ? 1 : 0;
}
static bool
same_file(json_object *a, json_object *b)
{
    const char *ad = f_string(a, "sha256"), *bd = f_string(b, "sha256");
    const char *at = f_string(a, "type"), *bt = f_string(b, "type");
    json_object *ab = f_field(a, "bytes"), *bb = f_field(b, "bytes");
    return ad && bd && at && bt && !strcmp(ad, bd) && !strcmp(at, bt) &&
        json_object_is_type(ab, json_type_int) && json_object_is_type(bb, json_type_int) &&
        json_object_get_int64(ab) == json_object_get_int64(bb);
}
static bool
receipt_ok(json_object *r, const char *attempt, json_object *declarations)
{
    json_object    *files;
    if (!r || !f_number_is(r, "schema_version", 1) || (files = f_field(r, "files")) == NULL || !json_object_is_type(files, json_type_object))
        return false;
    if (!declarations || !json_object_is_type(declarations, json_type_object) || json_object_object_length(files) != json_object_object_length(declarations))
        return false;
    json_object_object_foreach(files, name, file) {
        json_object    *declaration = f_field(declarations, name), *actual;
        char            path[F_PATH], artifact_dir[F_PATH];
        if (!f_field(declarations, name)) return false;
        if (!f_name(name) || f_path(artifact_dir, sizeof(artifact_dir), attempt, "artifacts") || !directory(artifact_dir) ||
            f_path(path, sizeof(path), artifact_dir, name) || !(actual = wd_file(path, declaration)))
            return false;
        if (!same_file(file, actual)) { json_object_put(actual); return false; }
        json_object_put(actual);
        (void)name;
    } return true;
}
static bool
request_binding_ok(const char *state, const char *request_state, const char *head, const char *instance,
                   const char *binding, const char *bound_head, const char *bound_instance,
                   const char *binding_path, char digest[65])
{
    if (!state || strcmp(state, "waiting-approval") || !request_state || strcmp(request_state, "pending")) return false;
    if (!head || !instance || !binding || !bound_head || !bound_instance || strcmp(bound_instance, instance)) return false;
    if (!reg(binding_path) || !git_hash(binding_path, digest) || strcmp(binding, digest)) return false;
    return id(bound_head, "head_") && !strcmp(head, bound_head) &&
        binding_identity_ok(binding_path, bound_head, instance);
}
struct wa_request {
    char p[F_PATH], ap[F_PATH], aid[80], digest[65];
    char *rid, *state, *head, *instance, *binding, *expires, *message, *attempts;
    char *bound_head, *bound_instance, *request_state;
    int64_t attempt_number;
};
static bool
request_attempt(struct wa_request *r, struct wa *w, const char *run, const char *step,
                json_object *sem)
{
    if (!r->attempts || !strcmp(r->attempts, "0")) { r->aid[0] = 0; return true; }
    if (!number(r->attempts, &r->attempt_number) || r->attempt_number > 11) {
        unknown(w, "invalid_attempt", run, step, NULL, r->head, r->instance, r->rid, r->binding, sem);
        return false;
    }
    snprintf(r->aid, sizeof(r->aid), "attempt-%s", r->attempts);
    return true;
}
static bool
request_open(struct wa_request *r, struct wa *w, const char *run, const char *rd,
             const char *step, const char *sd, json_object *sem)
{
    r->rid = scalar_at(sd, "request-id");
    if (!r->rid || !id(r->rid, "step_")) {
        unknown(w, r->rid ? "malformed_request" : "missing_request", run, step, NULL, NULL, NULL, r->rid, NULL, sem);
        return false;
    }
    if (f_path(r->p, sizeof(r->p), rd, "approvals") || !directory(r->p) ||
        f_path(r->ap, sizeof(r->ap), r->p, r->rid) || !directory(r->ap)) {
        unknown(w, "missing_request", run, step, NULL, NULL, NULL, r->rid, NULL, sem);
        return false;
    }
    r->request_state = scalar_at(r->ap, "step-id");
    if (!r->request_state || strcmp(r->request_state, step)) {
        free(r->request_state); r->request_state = NULL;
        unknown(w, "request_step_mismatch", run, step, NULL, NULL, NULL, r->rid, NULL, sem);
        return false;
    }
    free(r->request_state); r->request_state = NULL;
    return true;
}
static bool
request_collect(struct wa_request *r, struct wa *w, const char *run, const char *rd,
                const char *step, const char *sd, json_object *sem)
{
    if (!request_open(r, w, run, rd, step, sd, sem)) return false;
    r->state = scalar_at(sd, "state");
    r->request_state = scalar_at(r->ap, "state");
    if (r->request_state && (!strcmp(r->request_state, "approve") || !strcmp(r->request_state, "reject")) && r->state && strcmp(r->state, "waiting-approval")) {
        free(r->request_state); r->request_state = NULL;
        return false;
    }
    r->head = scalar_at(r->ap, "head");
    r->binding = scalar_at(r->ap, "binding-hash");
    r->expires = scalar_at(r->ap, "expires-at");
    r->message = scalar_at(r->ap, "message");
    r->attempts = scalar_at(sd, "attempts");
    return request_attempt(r, w, run, step, sem);
}
static void
request_release(struct wa_request *r)
{
    free(r->rid); free(r->state); free(r->head); free(r->instance); free(r->binding);
    free(r->expires); free(r->message); free(r->attempts); free(r->bound_head);
    free(r->bound_instance); free(r->request_state);
}
static void
request_bind_and_emit(struct wa_request *r, struct wa *w, const char *run, const char *step,
                      json_object *sem)
{
    char resolved_head[F_PATH];
    int ex;
    if (r->head && !head_resolve(w, r->head, resolved_head, &r->instance)) {
        unknown(w, "approval_binding_unknown", run, step, r->aid, r->head, NULL, r->rid, r->binding, sem);
        return;
    }
    r->bound_head = f_path(r->p, sizeof(r->p), r->ap, "binding.tsv") ? NULL : pair_value(r->p, "head");
    if (r->bound_head) r->bound_instance = head_instance(w, r->bound_head);
    if (!request_binding_ok(r->state, r->request_state, resolved_head, r->instance, r->binding, r->bound_head, r->bound_instance, r->p, r->digest)) {
        unknown(w, "approval_binding_unknown", run, step, r->aid, r->head, r->instance, r->rid, r->binding, sem);
        return;
    }
    free(r->request_state); r->request_state = NULL;
    free(r->head); r->head = r->bound_head; r->bound_head = NULL;
    f_string_add(sem, "state", r->state); f_string_add(sem, "message", r->message ? r->message : "");
    f_string_add(sem, "expires_at", r->expires ? r->expires : ""); f_string_add(sem, "binding", r->binding);
    ex = expiry(r->expires);
    if (ex == 2) unknown(w, "malformed_expiry", run, step, r->aid, r->head, r->instance, r->rid, r->binding, sem);
    else add(w, ex == 1 ? "approval_expired" : "approval", ex == 1 ? "approval_expired" : "approval_pending", run, step, r->aid, r->head, r->instance, r->rid, r->binding, ex == 1 ? "expired" : "fresh", "inspect workflow request", sem, true);
}
static void
request(struct wa *w, const char *run, const char *rd, const char *step, const char *sd)
{
    struct wa_request r = {0};
    json_object    *sem = json_object_new_object();
    if (!request_collect(&r, w, run, rd, step, sd, sem)) goto done;
    request_bind_and_emit(&r, w, run, step, sem);
done:request_release(&r);
    json_object_put(sem);
}
static void
result_emit(struct wa *w, const char *run, const char *step, const char *aid, const char *subject,
            const char *state, char *head, char *instance, json_object *receipt, json_object *sem)
{
    json_object *item;
    bool identity = id(head, "head_") && id(instance, "instance_");
    f_string_add(sem, "state", state ? state : "");
    json_object_object_add(sem, "output_receipt", json_object_get(receipt));
    f_string_add(sem, "result_subject", subject);
    f_string_add(sem, "identity_provenance", identity ? "recorded" : "not_recorded");
    item = add(w, "result", "result_ready", run, step, aid, head, instance, NULL, NULL,
               "fresh", "inspect workflow evidence", sem, true);
    if (item) {
        json_object_object_add(item, "result_subject", json_object_new_string(subject));
        json_object_object_add(item, "output_receipt", json_object_get(receipt));
        f_string_add(item, "identity_provenance", identity ? "recorded" : "not_recorded");
    }
}
static void
retained_scalar(const char *run, const char *name, json_object *sem)
{
    char path[F_PATH]; char *value;
    if (snprintf(path, sizeof(path), "%s/%s", run, name) >= (int)sizeof(path)) return;
    value = scalar(path); if (value) { f_string_add(sem, name, value); free(value); }
}
static void
result_retained_bindings(const char *run, json_object *sem)
{
    static const char *const scalars[] = {"base-commit", "definition-hash", "data-hash",
        "plan-accepted", "plan-deadline", "parallelism", "disk-mb", "max-heads"};
    static const char *const files[] = {"resolved.yml", "graph.tsv", "data.json", "compiled.json"};
    static const char *const fields[] = {"resolved-recipe-sha256", "graph-sha256", "data-sha256", "compiled-sha256"};
    char path[F_PATH], digest[65]; size_t i;
    for (i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++) retained_scalar(run, scalars[i], sem);
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (snprintf(path, sizeof(path), "%s/%s", run, files[i]) < (int)sizeof(path) &&
            reg(path) && !f_hash(path, digest)) f_string_add(sem, fields[i], digest);
    }
}
static bool
result_files(const char *rd, const char *ap, const char *step, json_object **receipt, json_object **data)
{
    char p[F_PATH];
    json_object *declarations;
    if (f_path(p, sizeof(p), ap, "outputs.json") || !reg(p) ||
        !(*receipt = f_read_json(p, F_LIMIT))) return false;
    if (f_path(p, sizeof(p), rd, "data.json") || !reg(p) ||
        !(*data = f_read_json(p, F_LIMIT))) return false;
    declarations = f_field(f_field(*data, "steps"), step);
    declarations = f_field(declarations, "outputs");
    return receipt_ok(*receipt, ap, declarations);
}
static bool
result_attempt(const char *sd, char ap[F_PATH], char **attempt, char **aid, int64_t *number_out)
{
    *attempt = scalar_at(sd, "authoritative-attempt");
    if (!*attempt || !number(*attempt, number_out) || *number_out < 1 || *number_out > 11 ||
        snprintf(ap, F_PATH, "%s/attempt-%s", sd, *attempt) >= F_PATH) return false;
    *aid = ap + strlen(sd) + 1;
    return true;
}
static void
result_identity(const char *ap, char **head, char **instance)
{
    *head = scalar_at(ap, "head");
    *instance = scalar_at(ap, "instance");
    if (*head && !id(*head, "head_")) { free(*head); *head = NULL; }
    if (*instance && !id(*instance, "instance_")) { free(*instance); *instance = NULL; }
}
static void
result(struct wa *w, const char *run, const char *rd, const char *step, const char *sd)
{
    char            p[F_PATH], ap[F_PATH], *attempt = NULL, *state = NULL, *aid,
                   *retention = NULL;
    int64_t         n;
    json_object    *receipt = NULL, *sem = json_object_new_object(), *data = NULL;
    char           *head = NULL, *instance = NULL;
    if (!result_attempt(sd, ap, &attempt, &aid, &n)) {
        unknown(w, "missing_authoritative_attempt", run, step, NULL, NULL, NULL, NULL, NULL, sem);
        goto done;
    }
    if (!directory(ap)) {
        unknown(w, "missing_attempt", run, step, aid, NULL, NULL, NULL, NULL, sem);
        goto done;
    }
    if (f_path(p, sizeof(p), rd, "retention.json")) {
        unknown(w, "path_unavailable", run, step, aid, NULL, NULL, NULL, NULL, sem);
        goto done;
    }
    if (reg(p) || access(p, F_OK) == 0) {
        unknown(w, "retention_expired", run, step, aid, NULL, NULL, NULL, NULL, sem);
        goto done;
    } if (!result_files(rd, ap, step, &receipt, &data)) {
        unknown(w, "result_binding_unknown", run, step, aid, NULL, NULL, NULL, NULL, sem);
        goto done;
    }
    state = scalar_at(sd, "state");
    result_identity(ap, &head, &instance);
    result_retained_bindings(rd, sem);
    result_emit(w, run, step, aid, ap + strlen(rd) + 1, state, head, instance, receipt, sem);
done:free(attempt);
    free(state);
    free(retention);
    free(head);
    free(instance);
    json_object_put(receipt);
    json_object_put(data);
    json_object_put(sem);
}
static void
run_step(struct wa *w, const char *run, const char *rd, const char *name, const char *sd)
{
    char p[F_PATH], *s;
    s = scalar_at(sd, "state");
    if (f_path(p, sizeof(p), sd, "request-id")) {
        unknown(w, "path_unavailable", run, name, NULL, NULL, NULL, NULL, NULL, NULL);
        free(s);
        return;
    }
    if (reg(p)) request(w, run, rd, name, sd);
    else if (s && !strcmp(s, "waiting-approval")) unknown(w, "missing_request", run, name, NULL, NULL, NULL, NULL, NULL, NULL);
    if (s && !strcmp(s, "succeeded")) result(w, run, rd, name, sd);
    free(s);
}
static void
run_one(struct wa *w, const char *run, const char *rd)
{
    char            sp[F_PATH], sd[F_PATH];
    DIR            *d;
    struct dirent  *e;
    if (w->runs >= WA_RUNS) {
        w->truncated = true;
        w->partial = true;
        return;
    }
    w->runs++;
    if (!directory(rd) || f_path(sp, sizeof(sp), rd, "steps") || !directory(sp) || !(d = opendir(sp))) {
        unknown(w, "missing_steps", run, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        return;
    } while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !f_name(e->d_name))
            continue;
        if (w->steps >= WA_STEPS) {
            w->truncated = true;
            w->partial = true;
            break;
        }
        w->steps++;
        if (f_path(sd, sizeof(sd), sp, e->d_name) || !directory(sd))
            continue;
        run_step(w, run, rd, e->d_name, sd);
    } closedir(d);
}
static json_object    *
attention_open(struct wa *w, const char *project, char runs[F_PATH], DIR **dir)
{
    char projects[F_PATH], project_dir[F_PATH], workflows[F_PATH];
    const char *root = getenv("HYDRA_STATE_V2_ROOT"), *home = getenv("HYDRA_HOME");
    if (!root) {
        if (!home) return f_error("workflow attention", "not_initialized", "workflow state root is unavailable");
        snprintf(w->root, sizeof(w->root), "%s/state/v2", home);
    } else snprintf(w->root, sizeof(w->root), "%s", root);
    if (f_path(projects, sizeof(projects), w->root, "projects") || !directory(projects) ||
        f_path(project_dir, sizeof(project_dir), projects, project) || !directory(project_dir) ||
        f_path(workflows, sizeof(workflows), project_dir, "workflows") || !directory(workflows) ||
        f_path(runs, F_PATH, workflows, "runs") || !directory(runs) || !(*dir = opendir(runs)))
        return f_error("workflow attention", "not_initialized", "workflow run storage is unavailable");
    return NULL;
}
static void
attention_scan(struct wa *w, const char *runs, DIR *dir)
{
    char p[F_PATH]; struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.' || !id(e->d_name, "run_")) continue;
        if (f_path(p, sizeof(p), runs, e->d_name) || !directory(p)) continue;
        run_one(w, e->d_name, p);
        if (w->truncated) break;
    }
}
json_object    *
wd_attention(const char *project)
{
    struct wa       w = {0};
    char            runs[F_PATH];
    DIR            *d;
    json_object    *data, *counts;
    if (!id(project, "project_")) return f_error("workflow attention", "invalid_project", "project identity is invalid");
    w.project = project;
    { json_object *error = attention_open(&w, project, runs, &d); if (error) return error; }
    w.items = json_object_new_array();
    attention_scan(&w, runs, d); closedir(d);
    data = json_object_new_object();
    json_object_object_add(data, "snapshot_schema_version", json_object_new_int(1));
    json_object_object_add(data, "items", w.items);
    json_object_object_add(data, "partial", json_object_new_boolean(w.partial));
    json_object_object_add(data, "truncated", json_object_new_boolean(w.truncated));
    counts = json_object_new_object();
    json_object_object_add(counts, "runs", json_object_new_int64((int64_t) w.runs));
    json_object_object_add(counts, "steps", json_object_new_int64((int64_t) w.steps));
    json_object_object_add(counts, "items", json_object_new_int64((int64_t) json_object_array_length(w.items)));
    json_object_object_add(data, "counts", counts);
    return f_success("workflow attention", data);
}
