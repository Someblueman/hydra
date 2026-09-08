#!/bin/sh
# Test-only fault at the real scalar rename boundary; other writes use real mv.
set -eu
for stats_destination do :; done
if [ "${HYDRA_TEST_STATS_SEED:-0}" = 1 ]; then
    case "$stats_destination" in */steps/verify/attempt-1/completed-at)
        stats_run=${stats_destination%/steps/verify/attempt-1/completed-at}
        printf '1\n' > "$stats_run/verified-at"
        cp "$stats_run/plan-accepted" "$stats_run/verification-plan-sha256"
        : > "$HYDRA_TEST_STATS_MARKER.seeded"
        ;;
    esac
fi
if [ "${stats_destination##*/}" = "$HYDRA_TEST_STATS_FIELD" ] &&
    [ -f "$(dirname "$stats_destination")/graph.tsv" ]; then
    : > "$HYDRA_TEST_STATS_MARKER"
    exit 1
fi
exec "$HYDRA_TEST_REAL_MV" "$@"
