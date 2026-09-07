# Supported agents and profiles

Hydra supports Antigravity (`agy`), Cursor Agent, OpenCode, Claude Code, Codex,
and Pi through declarative headless profiles. Interactive launch support is a
separate interface, listed below. A supported profile does not imply that its
executable is installed or its account is authenticated on every host.

## Headless execution

The optional `hydra-fleet` helper supplies these built-in contracts for
`hydra exec --profile NAME` and workflow `args.profile`. All six support prompt
transport, supervised cancellation, bounded observations, and resume from an exact
recorded session. They preserve the provider's normal permission settings.

| Profile | Executable | Invocation | Recorded resume | Usage |
| --- | --- | --- | --- | --- |
| `agy` (Antigravity) | `agy` | `--print`, `stream-json` | `--conversation ID` | Reported aggregate tokens |
| `cursor` | `cursor-agent` | `--print`, `stream-json` | `--resume ID` | Unknown; not declared supported |
| `opencode` | `opencode` | `run --format json` | `run --session ID` | Reported tokens |
| `claude` | `claude` | `--print`, `stream-json` | `--resume ID` | Reported tokens |
| `codex` | `codex` | `exec --json`, stdin prompt | `exec resume ID` | Reported tokens |
| `pi` | `pi` | `--print --mode json` | `--session ID` | Reported tokens |
| imported profile / plain script | Explicit executable | Declared argument, stdin, or file recipe | Only when declared | Only when translated |

Run `hydra agent contract NAME` to inspect the exact argv, adapter, and declared
capabilities. `hydra agent probe NAME` checks the installed executable's version
and help. Successful provider execution, session recall, and cancellation require
separate live evidence; see the dated [acceptance record](WORKFLOW_AGENT_ACCEPTANCE.md).
Cursor live qualification requires sign-in. Claude remote qualification remains
deferred. Antigravity's live checks and the earlier OpenCode local/remote checks
are recorded there with their scope and limitations.

```sh
hydra agent probe agy
hydra agent probe cursor
hydra agent probe opencode
hydra exec --branch worker --profile agy --prompt-file task.txt \
  --require prompt,observations --result-file answer.txt --exit-code --json
hydra exec --branch worker --profile agy --prompt-file follow-up.txt \
  --resume-run RUN_ID --require resume --result-file follow-up-answer.txt --exit-code
```

Replace `agy` with another supported profile to use the same supervision and
workflow interface. `RUN_ID` must identify a successful run with the same profile,
head, instance, and worktree. Hydra never selects the latest provider conversation.
Custom headless profiles are imported with `hydra agent import NAME adapter.json`;
see [Agent contract v1](AGENT_CONTRACT.md) for declarations and event limits.

## Interactive launch

`hydra spawn --profile NAME` creates a tmux head and starts the selected interactive
CLI. These profiles work without the native helper. `hydra agent list`, `show`,
`doctor`, and `hydra capabilities --json` describe this launch interface.

| Profile | Executable | Task delivery | Interactive restore |
| --- | --- | --- | --- |
| `none` | Shell only | Task is recorded | No provider session |
| `agy` | `agy` | `--prompt-interactive` | No automatic session capture; use headless recorded resume |
| `cursor` | `cursor-agent` | Positional prompt after `--` | No automatic session capture; use headless recorded resume |
| `opencode` | `opencode` | `--prompt` | No automatic session capture; use headless recorded resume |
| `claude` | `claude` | Positional prompt | Exact generated `--session-id` / `--resume` |
| `codex` | `codex` | Positional prompt | cwd-scoped `resume --last` |
| `copilot`, `aider`, `gemini` | Matching executable name | Not declared | Launch only |
| custom | Explicit absolute executable | Declared `none` or `task-file` | Not declared |

Pi currently has a built-in headless contract only. For an interactive Pi head,
create a custom launch profile with its absolute executable path. Custom headless
imports run through `exec`, not `spawn`.

Launch profiles are fixed declarations, not shell fragments. Task delivery reads
the private state task file inside a quoted command substitution: the prompt is
one argument and cannot inject shell syntax. The launch command removes trailing
newlines through shell command substitution; headless prompt transport preserves
file bytes. Tier 0 means no agent; Tier 1 means a launch recipe. No interactive
profile installs provider hooks or claims to observe the agent automatically.
`verified-local-help` means the recipe's flags were inspected, not authenticated
execution on the current host. `launch-only` and `user-declared` retain their
literal meanings.

Resolution order is explicit CLI profile, host-local project default, one detected
built-in executable, or `none`. Multiple detected agents require an explicit
choice. Repository setup stays inert until its configuration is trusted.

```sh
hydra spawn worker --profile agy --task 'Review the parser'
hydra spawn editor-work --profile cursor --task 'Explain the parser'
hydra spawn review --profile opencode --task 'Review the parser'
hydra agent init my-agent --executable /absolute/path/to/worker --prompt-mode task-file
```

## Installation and migration

Install the provider CLI and authenticate on each execution host. Cursor requires
`cursor-agent`; the `cursor` editor launcher alone is insufficient. The `cursor`
profile now starts Cursor Agent instead of opening the editor. No authentication
cache or provider configuration is copied by selecting a profile. Native fleet
sign-in and the narrower credential-copy support are documented in [Host auth](HOST_AUTH.md).

Existing custom profiles registered before a new built-in name was introduced
retain their stored declarations. They are listed once, and invalid stored
headless declarations fail rather than silently falling back to a built-in.
New imports cannot replace a reserved built-in name. Inspect a pre-existing custom
name before assuming it uses the built-in recipe; use a fresh profile name for
customizations.

Interactive Codex heads retain cwd-scoped `resume --last`, including older heads
without recorded provider session IDs. Headless `exec --resume-run` still requires
an exact recorded session; see [the resume contract](AGENT_CONTRACT.md#workflow-profiles-and-migration).
