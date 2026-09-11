#ifndef HYDRA_EXAMPLE_RESEARCH_H
#define HYDRA_EXAMPLE_RESEARCH_H
#include "example.h"
#define RESEARCH_JOBS 12
struct research_job { char *id; int64_t arrival, duration; };
struct research_data { struct research_job jobs[RESEARCH_JOBS]; char hash[65]; };
extern const char *const research_limits[5];
extern const char *const research_explanations[2];
extern const char *const research_locations[3];
extern const char research_question[];
json_object *research_recompute(const struct research_data *data, bool shortest);

int research_load(struct research_data *data);
void research_free(struct research_data *data);
json_object *research_provenance(const char *hash);
#endif
