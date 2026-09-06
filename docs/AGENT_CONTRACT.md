# Agent contract v1

The optional native helper supports declarative headless profiles through the
POSIX shell CLI. Existing interactive launch profiles remain supported separately;
launch availability does not imply prompt delivery, event hooks, or exact resume.
See the [acceptance record](WORKFLOW_AGENT_ACCEPTANCE.md) for tested versions,
reproduction commands, and the remaining live remote qualification boundary.

```sh
hydra agent contract codex
hydra agent probe codex
hydra agent import my-worker adapter.json
hydra exec --branch worker --profile my-worker --prompt-file task.txt \
  --require prompt,observations --result-file answer.txt --exit-code --json
hydra exec --branch worker --profile my-worker --prompt-file follow-up.txt \
  --resume-run RUN_ID --require resume --exit-code --json
```

Profile execution selects exactly one head and accepts no trailing command argv.
The existing exec supervisor owns cancellation, deadlines and bounded command
logs. A per-head lock prevents overlapping headless invocations. Profile execution
requires the optional `hydra-fleet` helper; ordinary shell execution and session
management do not.

## Declarations

A custom profile is a schema-versioned JSON object:

```json
{
  "schema_version": 1,
  "executable": "/absolute/path/to/worker",
  "argv": [{"input":"prompt"}],
  "prompt": "argument",
  "session": "none",
  "adapter": "canonical-jsonl",
  "probe_argv": ["--help"],
  "probe_tokens": ["worker"]
}
```

`argv` and optional `resume_argv` contain at most 128
literal strings or fixed input slots. Slots are `prompt`, `task_file`,
`session_id`, `input_dir`, and `output_dir`. There are no expressions, shell-string
templates, or provider-specific workflow branches. A literal argument remains one
argument, including whitespace, quotes, dollar signs, and shell punctuation.

Prompt transport is `none`, `stdin`, `argument`, or `file`. Argument and file
transport require exactly one corresponding slot. Prompts are regular, non-symlink,
NUL-free UTF-8 files bounded to 64 KiB. Session mode is `none`, `generated`, or
`observed`. Generated sessions receive a UUID; observed sessions must come from
translated events. Resume declarations require exactly one session slot and cannot
use session mode `none`. Resume binds to a successful recorded exec attempt's
profile digest and current instance. Hydra does not search for the latest provider
session. Missing or changed bindings fail explicitly.

Imports create a new private profile under `$HYDRA_HOME/profiles/NAME/adapter.json`.
Existing profiles and built-in names are not replaced. Built-in declarations target
Claude Code, Codex, Pi, and OpenCode, using their normal permission settings.

## Evidence and observations

`agent probe` records a UTC probe date, invoked executable path, version when
available, declared capabilities, executable/help checks, and `observed: null`.
Help probes do not establish authentication, successful prompt delivery, permission
hooks, or working resume. Actual runs record their normalized observations in
`state/v2/projects/PROJECT/exec/RUN/HEAD/agent.json` with the profile and prompt
digests, instance, session identity when known, timestamps, status, usage, and
bounded event metadata. Provider completion never sets a declared outcome or passes
a gate; `verification_passed` remains false in this provider receipt.

Translation adapters accept JSONL records bounded to 32 KiB each and at most 1024
normalized events. Malformed, NUL-containing, oversized, or unterminated records
stop execution. Events cannot update a replacement instance. Canonical events have
`schema_version: 1`, a `type`, and these fields:

| Type | Fields |
| --- | --- |
| `session` | `session_id` |
| `observation` | `status`: running, idle, or failed |
| `result` | `text` |
| `usage` | `usage`: input_tokens, output_tokens, cached_input_tokens, cost_usd |
| `permission` | `request_id` |

A permission report stops the turn as `permission_required`; it does not grant
permission or provide an interactive approval callback. Usage fields are optional
and remain null when unavailable. Exact cost enforcement is unsupported. Provider
JSONL examples in `tests/fixtures/agents` are synthetic conformance fixtures;
live-provider qualification is separate evidence.

## Safe points and retention

At the start of an explicit run or resume, Hydra drains only queued `safe-point`
messages addressed to the current instance into the bounded prompt. Oversized
messages remain queued; stale messages are archived with stale receipts. Messages
arriving during execution stay queued for the next turn. Delivery means copied to
prompt transport, not proof that the provider obeyed the message. No text is injected
into a terminal pane.

Provider stdout/stderr and result text are not retained in normal metadata.
`--result-file` explicitly captures the final answer as an artifact; the destination
must not already exist. `--retain-raw` keeps up to 1 MiB per stdout/stderr stream
under the head's `agent-payloads` directory, retaining ten completed runs. The
receipt records truncation. Providers may maintain their own private session stores;
Hydra does not copy those stores. File prompt transport uses a private temporary
copy during execution; ordinary completion removes it. An interrupted owner can
leave recovery evidence and must not be treated as permission to replay work.

## Workflow profiles and migration

An `exec` step can use `args.profile` with exactly one of `prompt_file` (relative to
its selected head's worktree) or `prompt_input` (a named, verified workflow input).
Optional `requires: [prompt, observations]` selects capabilities. With a workflow
data manifest, `result_file` is a relative path under that attempt's output
directory and is checked against declared outputs before dependents run. For
example, a downstream `prompt_input` can reference an upstream sealed file using
the manifest's existing `{step, output}` reference. Changing the profile field
leaves the workflow logic and artifact contract unchanged. Command argv and profile
execution are mutually exclusive. Profiles do not replace verification steps.

`resume_from: STEP_ID` resumes the exact recorded run of a direct `exec`
dependency with the same head and profile. The producer must have completed
successfully and recorded a resumable session. This reference preserves the
session identity across workflow recovery; it never selects a provider's latest
session. The receipt also binds the canonical worktree path, so sessions cannot
be resumed from a different worktree even when a head label is reused.

Each run's `observed` capability fields distinguish successful transport and
normalized reports from capabilities not exercised in that run, which remain null.
An observed prompt capability means the prompt was offered through the configured
transport; it does not establish that the model obeyed it. Live qualification must
compare actual results. A zero headless timeout is bounded internally to 24 hours;
ordinary command-mode exec retains its existing timeout behavior.

Interactive Codex restore no longer uses `resume --last`. `hydra resume HEAD`
requires a recorded provider session ID and fails before recreating a missing
worktree when no supported exact recipe is available. Older Codex heads without
that identity remain inspectable. Start a headless exec and use its returned run ID
with `--resume-run` for subsequent turns, or select the known provider session
explicitly outside Hydra. Existing successful headless receipts remain tied to
one current instance; they cannot be reused for a replacement instance.

This is an intentional behavior change for callers relying on implicit latest
session selection. There is no automatic migration that guesses a session identity.
No existing state is deleted, and the shell-only launch and no-agent paths remain
available. Headless-only imported profiles are invoked through `hydra exec`, not
interactive spawn; inspect their complete declaration with `hydra agent contract`.

## Host authentication

Fleet host sign-in and explicit portable credential copying are documented in
[host authentication](HOST_AUTH.md). Authentication stays in provider-owned stores,
outside profile declarations, task packages, and agent execution receipts.
