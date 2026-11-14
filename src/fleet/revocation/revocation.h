#ifndef HYDRA_FLEET_REVOCATION_H
#define HYDRA_FLEET_REVOCATION_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Explicit local revocation of candidate/source, transport (alias and its
 * accepted host key), package and project decisions. A record is private,
 * final for its exact identity, and never touches remote state, tasks,
 * installed packages or known_hosts. Scopes: candidate, source, transport,
 * package, project. Inputs are borrowed; returned JSON belongs to the caller. */
#define REVOCATION_ID 4096
#define REVOCATION_REASON 512
bool revocation_scope(const char *scope);
/* Canonical identity for a scope (a candidate target becomes its candidate
 * ID); false when the identity is not valid for the scope. */
bool revocation_normalize(const char *scope, const char *id, char out[REVOCATION_ID]);
json_object *revocation_find(const char *scope, const char *id);
bool revocation_blocked(const char *scope, const char *id);
/* Transport revocations retain the superseded accepted host key; a peer
 * presenting such a key is refused. Returns the record or NULL. */
json_object *revocation_find_key(const char *fingerprint);
/* {scope, id, reason, revoked_at, revoked_by} projection of a record. */
json_object *revocation_detail(json_object *record);
/* Typed "revoked" error carrying the detail projection as data. */
json_object *revocation_error(const char *command, const char *scope, const char *id);
/* Creates a record. superseded (borrowed, may be NULL) is copied into it. */
json_object *revocation_record(const char *scope, const char *id, const char *reason, json_object *superseded);
json_object *revocation_list(const char *scope);
json_object *revocation_cli(int argc, char **argv);
#endif
