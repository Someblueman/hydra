# 9B bounded handoff fixtures

`data.json` is a compatible producer/consumer measurement contract. The normal
value is `{"candidate":"current","duration":{"value":1000,"unit":"ms"}}`.

`tests/native/workflow_contract_cases.c` drives the public `hydra workflow plan`
CLI and the native workflow-data prepare/seal boundary using isolated source/run
fixtures. Its `--runtime` mode (`make test-workflow-contracts`) additionally runs
public supervised workflows, including the five negative 9B cases, and checks
consumer non-submission.

See [the public contracts guide](../../../docs/CONTRACTS.md) for supported
constraints and the limits of this evidence.
