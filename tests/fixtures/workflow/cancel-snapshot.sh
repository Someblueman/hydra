#!/bin/sh
# Release a real worker between two coordinator snapshots, without changing either result.
set -eu
control=${HYDRA_TEST_DAG_CONTROL:?}
run=${HYDRA_TEST_DAG_RUN:?}
case ${0##*/} in
    mv)
        if [ "$#" -eq 2 ] && [ "$2" = "$run/steps/produce/state" ] &&
            [ -f "$run/cancel-requested" ] && [ "$(cat "$1")" = waiting-remote ]; then
            attempt=0
            while [ ! -f "$control/release-cancel-worker" ] && [ "$attempt" -lt 400 ]; do
                sleep 0.05
                attempt=$((attempt + 1))
            done
            [ -f "$control/release-cancel-worker" ] || exit 1
        fi
        exec "$HYDRA_TEST_DAG_REAL_MV" "$@"
        ;;
    awk)
        # The driver's step-state scans are awk passes that receive the run as
        # WORKFLOW_RUN_DIR; the second snapshot is the count of active work
        # taken before the driver looks for waiting steps.
        if [ "${WORKFLOW_RUN_DIR:-}" = "$run" ] && [ -f "$run/cancel-requested" ]; then
            case " $* " in *' want= queued ready running retrying '*)
                "$HYDRA_TEST_DAG_REAL_AWK" "$@" > "$control/cancel-state-snapshot"
                [ "$(cat "$control/cancel-state-snapshot")" -ge 1 ]
                : > "$control/release-cancel-worker"
                attempt=0
                while [ "$(cat "$run/steps/produce/state")" = running ] && [ "$attempt" -lt 400 ]; do
                    sleep 0.05
                    attempt=$((attempt + 1))
                done
                [ "$(cat "$run/steps/produce/state")" = waiting-remote ] || exit 1
                : > "$control/cancel-snapshot-raced"
                cat "$control/cancel-state-snapshot"
                exit 0
                ;;
            esac
        fi
        exec "$HYDRA_TEST_DAG_REAL_AWK" "$@"
        ;;
    *) exit 2 ;;
esac
