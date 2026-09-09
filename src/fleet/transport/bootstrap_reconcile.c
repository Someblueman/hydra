#include "fleet/transport/bundle.h"
#include "fleet/transport/remote.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdlib.h>
#include <string.h>

json_object *f_bootstrap_reconcile(struct f_remote *remote, const char *file, const char *digest, const char *prefix, unsigned seconds) {
    char actual[65], binary[F_PATH], expected[F_PATH], command[F_PATH * 8 + 256];
    char *text = NULL, *quoted_binary = NULL, *quoted_prefix = NULL; struct f_capture cap = {0}; json_object *result = NULL;
    if (!prefix || prefix[0] != '/' || !digest || f_hash(file, actual) || strcmp(actual, digest) ||
        !(text = f_read(file, F_LIMIT)) || f_path(binary, sizeof(binary), prefix, "libexec/hydra/hydra-fleet") ||
        f_path(expected, sizeof(expected), prefix, "bin/hydra")) goto done;
    quoted_binary = f_quote(binary); quoted_prefix = f_quote(prefix);
    if (!quoted_binary || !quoted_prefix || snprintf(command, sizeof(command), "%s install-check '%s' --prefix %s", quoted_binary, digest, quoted_prefix) >= (int)sizeof(command)) goto done;
    if (f_ssh(remote, command, text, strlen(text), seconds, false, &cap) || cap.status) goto done;
    result = f_parse(cap.out);
    if (!json_object_get_boolean(f_field(result, "ok")) || !f_string(f_field(result, "data"), "hydra") ||
        strcmp(f_string(f_field(result, "data"), "hydra"), expected) ||
        !f_string(f_field(result, "data"), "sha256") || strcmp(f_string(f_field(result, "data"), "sha256"), digest) ||
        f_copy(remote->hydra, sizeof(remote->hydra), expected)) { json_object_put(result); result = NULL; }
done:
    free(text); free(quoted_binary); free(quoted_prefix); f_capture_free(&cap);
    return result ? result : f_error("fleet-bootstrap", "outcome_unknown", "cannot confirm the reviewed installed bytes and exact path; no installation was retried");
}
