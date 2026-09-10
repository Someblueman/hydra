#!/bin/sh
set -eu
python3 - "$HYDRA_WORKFLOW_INPUTS_DIR/analysis" "$HYDRA_WORKFLOW_INPUTS_DIR/raw" "$HYDRA_WORKFLOW_INPUTS_DIR/workload_sha256" "$HYDRA_WORKFLOW_INPUTS_DIR/environment" "$HYDRA_WORKFLOW_OUTPUTS_DIR/report" <<'PY'
import json,sys
analysis=json.load(open(sys.argv[1])); analysis['workload_sha256']=open(sys.argv[3]).read().strip(); analysis['environment']=open(sys.argv[4]).read().splitlines(); analysis['raw_samples']=open(sys.argv[2]).read().splitlines(); json.dump(analysis,open(sys.argv[5],'w'),sort_keys=True,indent=2); open(sys.argv[5],'a').write('\n')
PY
