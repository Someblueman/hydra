# Agent profiles and capability matrix

Profiles are fixed launch declarations, not shell fragments. Built-ins resolve a
known executable name. Custom profiles created by `hydra agent init` require an
existing absolute executable path. Task delivery reads the mode-0600 state task file
inside a quoted command substitution, so task bytes become one argument and are
never interpolated into shell syntax.

Resolution order is explicit CLI profile, host-local project default, one positively
detected executable, or `none`. Multiple detected agents require a choice. Repository
setup is inert until the exact configuration hash is trusted on the host.

Capability matrix, observed 2026-08-28 from local `--help`/`--version` surfaces:

| Profile | Tier | Prompt | Resume | Adapter | Confidence |
| --- | ---: | --- | --- | --- | --- |
| `none` | 0 | none; task is still recorded | none | none | exact Hydra behavior |
| `claude` | 1 | positional prompt from task file | exact `--session-id` / `--resume` | none | verified local help, Claude Code 2.1.251 |
| `codex` | 1 | positional prompt from task file | cwd-scoped `resume --last` | none | verified local help, codex-cli 0.149.1 |
| `cursor`, `copilot`, `aider`, `gemini` | 1 | none until verified | none | none | launch-only |
| custom | 1 | declared `none` or `task-file` | none | none | user-declared |

No hook adapter is claimed by this matrix. Missing executables, unsupported prompt or
resume modes, and absent adapters fail before mutation or degrade explicitly to the
Tier-0 no-agent path; they never fabricate lifecycle events.

```sh
hydra agent list
hydra agent show claude
hydra agent doctor
hydra agent init my-agent --executable /absolute/path/to/wrapper --prompt-mode task-file
hydra capabilities --json
```

