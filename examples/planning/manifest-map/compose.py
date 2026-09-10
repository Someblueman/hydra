#!/usr/bin/env python3
import json, os, pathlib
root = pathlib.Path(os.environ['HYDRA_WORKFLOW_INPUTS_DIR'])
out = pathlib.Path(os.environ['HYDRA_WORKFLOW_OUTPUTS_DIR']) / 'report.json'
manifest = json.loads((root / 'manifest').read_text())
records = []
for item in manifest['items']:
    if item['enabled']:
        records.append(json.loads((root / ('member-' + item['id'])).read_text()))
    else:
        records.append({'id': item['id'], 'value': item['value'], 'status': 'skipped'})
out.write_text(json.dumps({'schema_version': 1, 'members': records}, separators=(',', ':')) + '\n')
