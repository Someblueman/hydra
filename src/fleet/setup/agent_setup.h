#ifndef HYDRA_FLEET_SETUP_AGENT_SETUP_H
#define HYDRA_FLEET_SETUP_AGENT_SETUP_H
/*
 * Private interface of the agent setup files (agents.c, recipes.c, signin.c).
 * Ownership follows setup.h: returned JSON is caller-owned unless marked
 * borrowed; arguments are borrowed.
 */
#include "fleet/setup/setup.h"

#define AGENT_RECIPES_LIMIT 65536U

/* ---- Installer recipes (recipes.c), "agent-recipes" schema 1 ----
 * A recipe object has exactly: agent, executable, command, requires[],
 * expected_dirs[], login_args[], optional auth_status_argv[], docs_url and
 * verified_on. Built-in recipes are compiled in; a private local file
 * $HYDRA_HOME/fleet/agent-recipes.json ({"schema":"agent-recipes",
 * "schema_version":1,"recipes":[...]}, 0600, owned by the user, no symlink)
 * replaces built-in recipes of the same agent. Recipes never come from the
 * remote. */
bool setup_recipe_valid(json_object *recipe);
/* Built-in recipes as an agent-recipes document. */
json_object *setup_recipes_builtin(void);
/* Recipe for agent with an added "source" ("built-in" or "local"), or NULL.
 * When the local file exists but is unsafe or invalid, returns NULL and sets
 * *error to a setup_error envelope; otherwise *error is NULL. */
json_object *setup_recipe(const struct setup_ctx *ctx, const char *agent, json_object **error);

/* ---- Inventory and selection (agents.c) ---- */
/* Remote agent inventory (receiver agent-inventory capability, else the
 * preflight snapshot) or NULL with *error set. */
json_object *setup_inventory(struct setup_ctx *ctx, json_object **error);
/* Adds agent to the agents step detail "selected" array (keeps its status). */
int setup_agent_select(struct setup_ctx *ctx, const char *agent);
/* Interactive y/N question on stderr/stdin. */
bool setup_agent_confirm(const char *question);
/* Terminal available for an interactive provider session over ssh -t. */
bool setup_agent_tty(void);
#endif
