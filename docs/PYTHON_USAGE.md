# Native language boundary

Hydra uses POSIX shell and C. Its active build, tests, planning examples,
standalone termviz export and CI do not require Python. JSON-C is a build
dependency for the optional fleet helper and structured native tests/examples.
The ordinary shell CLI and standalone termviz library retain their own smaller
requirements.

`make build-plan-precompile build-plan-example` builds the planning programs.
Copy `build/plan-example` into each disposable example source repository before
committing it. The manifest and staged examples also require the shared
`examples/planning/native/payload.sh`. Checkers independently reconstruct the
expected output; shared code handles JSON, hashing and evidence formatting.

Native test drivers live in `tests/native`, shell fixture helpers in
`tests/fixture`, and the independent PTY observer in `tests/termviz`. Existing
Make targets build and run them. The PTY observer parses emitted ANSI bytes
without using the product terminal parser. Performance controls preserve the
fixed seed, trial count and independent numerical recomputation.

C analysis uses a native clang-tidy 22.1.8 installation, selected by
`scripts/clang-tidy.sh`. No virtual environment or Python package is needed.

Files under `docs/evidence` and `docs/acceptance` are historical records tied
to their recorded revisions. Their old commands, source inventories and tool
versions describe those runs; they are not current build instructions or proof
that the native port passed acceptance. Current verification must be recorded
against the new source revision.
