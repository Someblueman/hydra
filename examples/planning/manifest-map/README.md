# Finite manifest map

`precompile.py` is a PRE-COMPILE boundary: it validates a closed schema-1
manifest (at most eight unique IDs, integer values -10..10, boolean `enabled`)
and lowers it into an ordinary static schema-1 plan. The generated plan has one
worker per enabled member, a fixed composition join, and a checker join. Disabled
members remain explicit skipped records in the report. Empty and all-skipped
manifests still compile to a checked report with no worker steps.

The manifest is a declared repository input. It cannot select hosts, tools,
commands, or effects. `check.py` independently reconstructs every expected record and
emits schema-3 evidence; structural compilation does not claim runtime success.

```sh
python3 precompile.py manifest.json plan.json
hydra workflow plan compile plan.json policy.json ../compiled.json
```

Copy this directory to a disposable Git repository, generate and commit the
source plan, then initialize Hydra with `--no-agent --trust`. Keep `HYDRA_HOME`
and the compiled output outside the source repository. Review the compiled plan
and pass its exact returned SHA-256 to `hydra workflow plan run ... --accept ...`.
