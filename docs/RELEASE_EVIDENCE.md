# Hydra 1.6.0 release evidence

Run the release gates from the repository root:

```sh
git diff --check
make lint
make test
```

The shell-only suite requires no compiler. Gate coverage is intentionally split by
behavior rather than a second orchestration layer:

| Release gate | Regression evidence |
| --- | --- |
| reversible migration and interrupted-write boundary | `tests/test_foundation.sh` |
| identical branch labels in separate repositories | `tests/test_project_isolation.sh` |
| stale prior-instance evidence cannot satisfy current work | `tests/test_lifecycle.sh` |
| declared, observed, and live channels remain separate | `tests/test_lifecycle.sh`, `tests/test_json_output.sh` |
| hookless Tier 0/1 and explicit capability confidence | `tests/test_profiles.sh`, `tests/test_lifecycle.sh` |
| absent, malformed, version-skewed, missed, and stale adapters | `tests/test_lifecycle.sh` |
| clean init, dry-run, task-aware no-agent head | `tests/test_onboarding.sh` |
| task injection and resume without pane typing | `tests/test_profiles.sh`, `tests/test_lifecycle.sh` |
| setup, teardown, transcript, hook, and secret defaults | `tests/test_lifecycle.sh`, `tests/test_setup_commands.sh` |
| exec is bounded, private, and not steering | `tests/test_operations.sh` |
| task to spawn to handoff to outcome to resume/teardown | `tests/test_lifecycle.sh`, `tests/test_operations.sh` |

The lifecycle scenario creates a throwaway repository, initializes trusted no-agent
state, spawns a task-aware head, rejects malformed and version-skewed adapter input,
accepts one correlated observation, records a typed handoff receipt, declares and
waits for an outcome, tears down with bounded redaction, resumes as a new instance,
rejects stale prior-instance input, then tears down without a default transcript.
Its event stream contains both instance IDs and its head/instance provenance remains
inspectable. Every fixture removes only its own temporary repository, worktree, and
tmux session.
