#ifndef HYDRA_FLEET_H
#define HYDRA_FLEET_H
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#define F_LIMIT (8U * 1024U * 1024U)
#define F_PATH 4096
#define F_PROTOCOL 1
#define F_VERSION "2.4.0"
/* Process configuration is initialized by main before dispatch. */
extern const char *f_home, *f_hydra;
struct json_object;
/* Caller owns the returned JSON; this projection is read-only. */
struct json_object *f_attention_aggregate(struct json_object *aggregate);
#endif
