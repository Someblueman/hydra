# Worktree operations and Git evidence

## Out-of-band execution

`hydra exec` runs a command in selected recorded worktrees without typing into an
agent pane:

```sh
hydra exec --branch feature --timeout 300 -- make test
hydra exec --group backend --jobs 4 --json -- git status --short
hydra exec --all --jobs 8 --timeout 60 -- ./scripts/check.sh
```

Use repeated `--branch`, one `--group`, or `--all`; these selection modes are
mutually exclusive. The default is the current Git branch. `--jobs` is bounded to
1-16 (default 4), and each command has a non-negative timeout (default 300 seconds).
Timeout sends TERM and then KILL to the command process tree and records exit 124.

The command after `--` is an argument vector. Hydra neither joins nor reparses it.
Shell syntax requires the separate `--shell <text> --allow-shell` mode and current
repository trust. Captured stdout/stderr are capped by `HYDRA_EXEC_MAX_BYTES`
(default 1 MiB each), mode 0600, and stored under the project state tree by run and
head ID. JSON includes the bounded captures and per-head exit status. A nonzero
workload result makes the CLI exit nonzero while preserving all result records.

Command-mode exec evidence is distinct from messaging and lifecycle state. It does
not deliver agent steering, declare an outcome, update observed agent status, or
approve a gate.

Profile mode supervises a headless provider on exactly one head instead of taking
trailing command argv. Supported built-ins are Antigravity (`agy`), Cursor Agent
(`cursor`), OpenCode, Claude Code, Codex, and Pi. For example:

```sh
hydra exec --branch feature --profile agy --prompt-file task.txt \
  --require prompt,observations --result-file answer.txt --exit-code --json
```

This mode can deliver queued safe-point messages and translate provider
observations. It still cannot declare an outcome or approve a gate. Missing
executables or required capabilities fail before starting the provider; Cursor
usage is unsupported. See [supported profiles](PROFILES.md) and
[the headless contract](AGENT_CONTRACT.md) for resume, authentication, output
retention, and independently verified results.

## Git views

Every new head records the exact base commit. These commands use that immutable base:

```sh
hydra diff feature
hydra diff feature --stat
hydra diff feature --name-only --json
hydra review feature --json
hydra list --git
```

`diff` is ordinary `git diff <base> --`. `review` reports ahead/behind counts,
porcelain dirty-path count, changed files, numeric insertions/deletions, and
`git diff --check`. `list --git` adds a compact view of the same recorded-base
evidence. These are inspection surfaces, not merge, integration, or approval
commands.
