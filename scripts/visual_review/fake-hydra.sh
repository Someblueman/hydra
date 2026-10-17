#!/bin/sh
# Visual-review shim around the deterministic TUI fixtures in
# tests/fixtures/tui: the fixture file a screen reads is chosen per state
# through $VR_STATE/<kind> files, so one PTY session walks the control centre
# through running, finished and failed runs, attention queues and reviews
# without a real project. Everything else goes to the ordinary fixture.
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
fixtures="$here/../../tests/fixtures/tui"
state="${VR_STATE:?VR_STATE names the visual-review state directory}"
pick() {
    if [ -f "$state/$1" ]; then cat "$state/$1"; else printf '%s' "$2"; fi
}
case "${1:-}:${2:-}" in
    workflow:tui-data) cat "$(pick workflow "$fixtures/workflow-v1.tsv")" ;;
    workflow:statistics-data) cat "$(pick statistics "$fixtures/statistics-v4.tsv")" ;;
    workflow:attention-data|fleet:attention-data|tui:--attention-data) cat "$(pick attention "$fixtures/attention-v1.tsv")" ;;
    workflow:review-data|fleet:review-data) cat "$(pick review "$fixtures/review-v1.tsv")" ;;
    *)
        HYDRA_TEST_SEEN_FILE="$state/seen.tsv"
        export HYDRA_TEST_SEEN_FILE
        exec "$fixtures/fake-hydra.sh" "$@"
        ;;
esac
