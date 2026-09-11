# 9B bounded handoff fixtures

`data.json` is a compatible producer/consumer measurement contract. The normal
value is `{"candidate":"current","duration":{"value":1000,"unit":"ms"}}`.

`tests/workflow_contract_cases.py` drives the public `hydra workflow plan` CLI
and the native workflow-data prepare/seal boundary using isolated source/run
fixtures. Its `--runtime` mode additionally runs public supervised workflows,
including the five negative 9B cases, and checks consumer non-submission.

See [the public contracts guide](../../../docs/CONTRACTS.md) for supported
constraints and the limits of this evidence.
