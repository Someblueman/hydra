#!/bin/sh
# A portable handoff for the real fixture, with CLI argv and frozen projections.
: "${fixture:?}" "${repo:?}" "${root:?}" "${native:?}" "${project:?}" "${run:?}" "${reference:?}"
sha256() { shasum -a 256 "$1" | awk '{print $1}'; }
jq -n --arg cwd "$repo" --arg home "$HYDRA_HOME" --arg state "$HYDRA_STATE_V2_ROOT" \
    --arg hydra "$root/bin/hydra" --arg native "$native" --arg tmux "$TMUX_TMPDIR" \
    --arg reference "$reference" --arg project "$project" --arg run "$run" --arg fixture "$fixture" \
    --arg binary_sha256 "$(sha256 "$native")" --arg source_sha256 "$(sha256 "$root/src/fleet/review.c")" \
    --arg contract_sha256 "$(sha256 "$root/src/fleet/review_contract.c")" \
    --arg result_json_sha256 "$(sha256 "$fixture/exact.json")" \
    --arg result_tsv_sha256 "$(sha256 "$fixture/exact.tsv")" \
    --arg request_json_sha256 "$(sha256 "$fixture/exact-request.json")" \
    --arg request_tsv_sha256 "$(sha256 "$fixture/exact-request.tsv")" \
    --slurpfile result "$fixture/exact.json" --slurpfile request "$fixture/exact-request.json" '
    def argv($x): $x.data.identity | [.kind,.project_id,.host,.task_id,.run_id,.step_id,.attempt_id,.head_id,.current_instance,.request_id,.binding,.revision_sha256,.identity_sha256] | map(. // "-");
    {cwd:$cwd, environment:{HYDRA_HOME:$home,HYDRA_STATE_V2_ROOT:$state,HYDRA_BIN_CMD:$hydra,HYDRA_FLEET_BIN:$native,TMUX_TMPDIR:$tmux},
     project:$project,run:$run,fixture:$fixture,cleanup_owner:"local review repair task; parent to confirm transfer after native parity",
     cleanup_argv:["rm","-rf",$fixture], binary_sha256:$binary_sha256,
     source_sha256:{"review.c":$source_sha256,"review_contract.c":$contract_sha256},
     result:{json:($fixture+"/exact.json"),tsv:($fixture+"/exact.tsv"),json_sha256:$result_json_sha256,tsv_sha256:$result_tsv_sha256,
       json_argv:([$hydra,"workflow","review"]+argv($result[0])+[$reference]),tsv_argv:([$hydra,"workflow","review-data"]+argv($result[0])+[$reference])},
     request:{json:($fixture+"/exact-request.json"),tsv:($fixture+"/exact-request.tsv"),json_sha256:$request_json_sha256,tsv_sha256:$request_tsv_sha256,
       json_argv:([$hydra,"workflow","review"]+argv($request[0])),tsv_argv:([$hydra,"workflow","review-data"]+argv($request[0]))}}' > "$fixture/manifest.json"
