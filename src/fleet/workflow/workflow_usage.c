#include "fleet/workflow/workflow_usage.h"
#include "fleet/plan/plan.h"
#include "fleet/review_result.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WU_STEPS 128U
#define WU_FIELD 79U

/* Statistics rows are tab separated: every text value is single-line,
 * printable and bounded, and absent evidence is "-" rather than zero. */
static void field(const char *value)
{
    size_t i;
    if (!value || !*value) {
        fputs("\t-", stdout);
        return;
    }
    putchar('\t');
    for (i = 0; value[i] && i < WU_FIELD; i++)
        putchar((unsigned char)value[i] < 32U || value[i] == 127 ? '?' : value[i]);
}

static void count(json_object *agent, const char *key)
{
    json_object *value = f_field(agent, key);
    if (json_object_is_type(value, json_type_int))
        printf("\t%lld", (long long)json_object_get_int64(value));
    else
        fputs("\t-", stdout);
}

static void cost(json_object *agent)
{
    json_object *value = f_field(agent, "cost_usd");
    double usd = value ? json_object_get_double(value) : -1.0;
    if (value && isfinite(usd) && usd >= 0 && usd < 1e9)
        printf("\t%lld", (long long)llround(usd * 1e6));
    else
        fputs("\t-", stdout);
}

static bool project_of(const char *run, char project[F_PATH])
{
    char *marker;
    if (f_copy(project, F_PATH, run) || !(marker = strstr(project, "/workflows/runs/")))
        return false;
    *marker = 0;
    return true;
}

static void step_usage(const char *run, const char *project, const char *step)
{
    char attempt[F_PATH], log[F_PATH];
    json_object *agent;
    if (plan_attempt_directory(run, step, attempt) || !(agent = review_attempt_agent(project, attempt, log)))
        return;
    fputs("usage", stdout);
    field(step);
    field(f_string(agent, "profile"));
    field(f_string(agent, "executable_version"));
    field(f_string(agent, "model"));
    field(f_string(agent, "effort"));
    count(agent, "tokens_in");
    count(agent, "tokens_cached");
    count(agent, "tokens_out");
    cost(agent);
    putchar('\n');
    json_object_put(agent);
}

int wu_usage_tsv(const char *run)
{
    char project[F_PATH], steps[F_PATH];
    DIR *dir;
    struct dirent *entry;
    size_t seen = 0;
    if (!project_of(run, project) || f_path(steps, sizeof(steps), run, "steps") || !(dir = opendir(steps)))
        return 1;
    while ((entry = readdir(dir)) && seen < WU_STEPS) {
        if (!plan_id(entry->d_name))
            continue;
        seen++;
        step_usage(run, project, entry->d_name);
    }
    closedir(dir);
    return ferror(stdout) ? 1 : 0;
}
