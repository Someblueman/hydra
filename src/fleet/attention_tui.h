#ifndef HYDRA_FLEET_ATTENTION_TUI_H
#define HYDRA_FLEET_ATTENTION_TUI_H
#include <json-c/json.h>
int f_attention_tui_data(json_object *aggregate);
/* Borrowed validated attention item; caller-owned SHA256 buffers. Uses the
 * same canonical tuple and revision bytes as attention-data fields 15/16. */
int f_attention_item_hashes(json_object *item, char revision[65], char identity[65]);
#endif
