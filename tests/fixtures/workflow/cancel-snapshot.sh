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
    find)
        if [ "$1" = "$run/steps" ] && [ -f "$run/cancel-requested" ]; then
            case " $* " in *' -name state '*)
                count=$(cat "$control/cancel-snapshot-count")
                count=$((count + 1))
                printf '%s\n' "$count" > "$control/cancel-snapshot-count"
                if [ "$count" -eq 2 ]; then
                    "$HYDRA_TEST_DAG_REAL_FIND" "$@" > "$control/cancel-state-snapshot"
                    grep -qx running "$control/cancel-state-snapshot"
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
                fi
                ;;
            esac
        fi
        exec "$HYDRA_TEST_DAG_REAL_FIND" "$@"
        ;;
    *) exit 2 ;;
esac
