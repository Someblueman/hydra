# Native finite-example precompiler

Build with `make build-plan-precompile`, then use the absolute binary path from
a copied example repository:

```sh
/path/to/hydra/build/plan-precompile manifest manifest.json plan.json
HYDRA_BIN=/path/to/hydra/bin/hydra /path/to/hydra/build/plan-precompile staged manifest.json RUN_ID plan2.json
```

This shared C/JSON-C helper replaces the two internal Python precompilers. It
validates the same bounded manifest, checks the accepted stage-1 result when
staging, and constructs an ordinary static plan. It uses the existing native JSON,
process and hashing helpers. It is an example build target and is not installed
as part of Hydra's runtime. No Python compatibility entrypoints remain.

Manifest mode requires the current example's `manifest.json`. Staged mode also
accepts a manifest path from another directory, runs result lookup in that
manifest's parent, and preserves caller-relative output paths. Refused inputs
leave an existing output untouched. Inputs must be bounded regular JSON files;
FIFO/device inputs are rejected before reading.

`make test-plan-outcomes test-plan-staged` exercises the retained Python boundary
drivers and independent checkers. `make test-plan-staged-public` runs both real
stages. `HYDRA_PLAN_PRECOMPILE_BIN` selects a separately built helper for testing.
The tiny example workers, composers and semantic checkers remain Python: they are
payloads and independent oracles, not a second planning implementation.
