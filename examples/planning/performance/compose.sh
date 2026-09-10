#!/bin/sh
set -eu
python3 - "$HYDRA_WORKFLOW_INPUTS_DIR/analysis" "$HYDRA_WORKFLOW_INPUTS_DIR/raw" "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); r['raw_samples']=open(sys.argv[2]).read().splitlines(); json.dump(r,open(sys.argv[3],'w'),sort_keys=True,indent=2); open(sys.argv[3],'a').write('\n')
PY
