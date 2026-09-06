# Workflow and agent acceptance (in progress)

This record covers roadmap priorities 1 and 2 on
`feature/workflow-inputs-agent-adapters`, plus the subsequent Antigravity, Cursor,
and OpenCode support expansion. The [supported-agent matrix](PROFILES.md)
distinguishes implemented headless contracts from interactive launch and actual
provider qualification.

The original local Claude/Codex/Pi/OpenCode/plain-script matrix passed. Original
remote Codex, Pi, OpenCode, plain-script, and durable-approval scenarios passed over
real SSH. Claude native host sign-in and remote qualification remain deferred at
the user's request. The expansion below records Antigravity local execution,
recorded recall, byte preservation, and cancellation, plus fresh OpenCode byte and
recall checks. Cursor's CLI and conformance tests are available, but actual Cursor
execution stopped at missing authentication. Antigravity/Cursor remote provider
runs have not been qualified. No fixture result substitutes for those live checks,
and no release or push follows from this record.

## Local provider task, 6 September 2026

Each profile received the same instruction to return an exact nonce without a
trailing newline, tools, or file edits. Its captured result was compared byte for
byte against the expected file. Each resumable profile then received a follow-up
that omitted the nonce and requested the same exact answer through its recorded
run identity. Cancellation used a longer prompt, waited until the provider process
was observed beneath the supervisor, sent SIGTERM to the public CLI, and checked
both process termination and the normalized receipt.

| Profile | Executable version | Exact prompt result | Exact session recall | Cancellation |
| --- | --- | --- | --- | --- |
| Claude Code | 2.1.260 | Passed | Passed | Passed |
| Codex | 0.153.3 | Passed | Passed | Passed |
| OpenCode | 1.18.27 | Passed | Passed | Passed |
| Pi | 0.84.3 | Passed | Passed | Passed |
| Plain script | plain-worker-1 | Passed | Unsupported | Passed |

These fresh runs were probed between 01:09:40 and 01:10:35 UTC. Their initial run
IDs were respectively `run_26a4b6ebdbd9fecd7700`,
`run_e2269044292a7ab47376`, `run_a4097fc1243569965317`,
`run_900352358752d4c703f6`, and `run_7f045f6e2a2b04d4218b`.
All cancellation runs returned CLI status 143, recorded agent status 130 and
`state: cancelled`, and left no live observed provider PID.

Pi used a qualification-only imported profile with literal arguments
`--provider google --model gemini-3.6-flash` in both invocation recipes. Its normal
configured OpenAI Codex backend initially refused execution because of a usage
limit; Google `gemini-2.5-flash` then returned a retired-model error. Neither is
counted as a successful provider run. The working Google selection did not change
the user's default Pi configuration or use a usage reset. Other providers used
their existing authenticated configuration and normal permission settings.

The plain script implements stdin prompt transport and literal output; it declares
no session or observation adapter. Requiring resume fails explicitly before
execution. JSONL corruption, permission reports, stale instances, unknown usage,
safe-point delivery, and retention limits are tested with deterministic adapters;
these are not claims that every live provider emitted each optional event.

Local receipts, comparison files, and cancellation metadata are retained in the
private qualification directory identified by `/tmp/hydra-live-adapters-current`.
Raw provider payload retention was disabled. These local files are execution
evidence, not published repository artifacts or portable provider session stores.

## Reproduce the shared local task

Build the optional helper with `make build-fleet`. In a disposable initialized and
trusted Git repository, create a no-agent head using `hydra spawn qualification
--no-agent`. Use an isolated `HYDRA_HOME` and set `HYDRA_NONINTERACTIVE=1`,
`HYDRA_SKIP_AI=1`, and `HYDRA_NO_SWITCH=1`. Provider executables and authentication
must already be available. For a custom worker, import its declaration with
`hydra agent import PROFILE adapter.json`.

```sh
printf %s HYDRA_QUALIFICATION_d9d6adf9d346ec8c > expected
cat > prompt <<'EOF'
Return only the exact value after ARTIFACT= on the next line, with no explanation, markdown, spaces, or trailing newline. Do not modify files or invoke tools.
ARTIFACT=HYDRA_QUALIFICATION_d9d6adf9d346ec8c
EOF
hydra exec --branch qualification --profile PROFILE --prompt-file prompt \
  --require prompt --timeout 180 --result-file PROFILE-final.txt --exit-code --json
cmp expected "$(hydra path qualification)/PROFILE-final.txt"
```

Record the returned `data.run_id`; it identifies the exact session to resume.
Write a follow-up asking for the preceding exact value without including the value,
then run:

```sh
hydra exec --branch qualification --profile PROFILE --prompt-file follow-up \
  --resume-run RUN_ID --require resume --timeout 180 \
  --result-file PROFILE-final-resumed.txt --exit-code --json
cmp expected "$(hydra path qualification)/PROFILE-final-resumed.txt"
```

Result paths must be new. Receipt identity also binds the instance, profile digest,
and canonical worktree; keep these unchanged for the follow-up. To reproduce
cancellation, start another public exec with a long-running prompt, observe its
provider child before sending SIGTERM to the CLI PID, wait for exit, then inspect
the run's `agent.json` and check that the observed provider PID has terminated.

## Repository acceptance boundaries

Run `make test-all` for shell lint/tests, native tests, fleet transport fixtures,
PTY/parity checks, installation and onboarding. Run `make sanitize` for the
platform-supported native sanitizers. On this macOS host that means UBSan; it does
not establish ASan or Linux qualification.

On 6 September 2026, `make test-all` and `make sanitize` both exited 0. After adding
the queued-steering regression, `tests/test_workflow_approval.sh` passed all 29
assertions. The retained local logs are `/tmp/hydra-test-all-agent-workflow.log`,
`/tmp/hydra-sanitize-agent-workflow.log`, and `/tmp/hydra-approval-steering.log`.

The public CLI scenarios live in:

- `tests/test_workflow_data.sh`: exact producer/consumer artifact handoff, required
  output validation, traversal/type/digest rejection and recovery.
- `tests/test_workflow_approval.sh`: coordinator exit, explicit resume, stale code,
  graph and queued steering, expiry, rejection and concurrent decisions.
- `tests/test_workflow_runtime.sh`: completed-attempt preservation, retry classes,
  durable backoff and uncertain non-idempotent recovery.
- `tests/test_agent_execution.sh`: declarative execution, exact resume, malformed,
  partial and bounded output, permission reports, cancellation, stale instance,
  safe-point delivery and a failing verification gate after provider success.
- `tests/test_task_acceptance.sh`, with `task_agent_cases.sh` and
  `task_approval_cases.sh`: selected remote inputs, collected sealed outputs and
  metadata, approval suspension, lost acknowledgments, resume and cancellation
  through the fixture SSH transport.

Contracts and migration are documented in [workflow data](WORKFLOW_DATA.md),
[agent profiles](AGENT_CONTRACT.md), and [remote tasks](REMOTE_TASKS.md).
The remaining AI-provider runs must use the same prompt, exact-result comparison,
recorded-session follow-up and observed-process cancellation as the shared task.

## Real Ubuntu SSH qualification, 6 September 2026

The user's earlier VPS was accessed with batch authentication and strict host-key
checking. Current source built with GCC in a disposable directory. JSON-C's
development package was downloaded and extracted privately; no system package or
global PATH was changed. The public package/bootstrap path installed the matching
shell source and Linux helper under this immutable pin:

```text
a55065f49a7b2a8620bc6a3547a4c13c4d15d791c2b7e8fadcb58558ef9d7605
```

The local qualification directory is `/tmp/hydra-remote-agent-qualification.flUOhs`;
the remote directory is `/tmp/hydra-agent-qualification.CLIw0N`. Both contain only
this qualification's source, packages, isolated state and evidence. Credentials were absent during the initial runs. Subsequent explicitly approved
authentication is recorded below. Existing Hydra installations remain separate.

On that pin, the shared plain script task
`task_ecdc1593f75934bfe164d8f4a2e0ce8fce74fef1ac93f8db4558e51c27e75bdc`
passed prompt delivery, exact sealed artifact comparison in a downstream step,
and an independent gate. The downloaded result was verified again byte for byte
against the local expected nonce. Requiring unsupported resume returned status 1
without creating a new provider receipt. Cancellation task
`task_2e97af66dd708c790e4fcf30264d8648407962405da5873805a54979436237b5`
started an observed script process, then the public task cancellation command
stopped it and reported `confirmed_stopped`, `cancelled`, and a ready sealed result.

The real approval scenario suspended after producing the artifact. After an
approval was recorded, changing worktree evidence caused resume to issue a new
pending request. A fresh decision resumed the same run and preserved the original
producer attempt. The final-pin approval task
`task_4a5695952c28c1cb34c0c8d7197678e10f0539a4e2699027aaac8c4aa3f07f15`
also completed after explicit decision/resume; its downloaded evidence retained
producer attempt 1 and the exact artifact.

Linux `make sanitize-fleet` passed with AddressSanitizer and UBSan. Its first run
exposed a real collaborative-umask defect: a group-writable workflow attempt
directory was refused by the secure receipt reader. New attempt directories now
use a private umask; existing directory permissions are not changed. The regression
runs the shared agent workflow under umask `002`, and passed on Linux and macOS.
Local lint and all 38 workflow runtime assertions passed after the fix.

The four AI CLIs were installed at the same versions as the local matrix under the
private `providers` prefix, using `@anthropic-ai/claude-code`, `@openai/codex`,
`@earendil-works/pi-coding-agent`, and `opencode-ai`. Imported `live-*` profiles use
the built-in contracts with absolute executable paths. No usual provider
authentication files or API-key environment variables were present during initial
inspection. Claude and Codex's native authentication status commands confirmed
they were not logged in; OpenCode listed zero credentials. Default shared tasks
for Claude, Codex and Pi failed before returning an artifact. OpenCode's default
task reached its 180-second deadline and produced a sealed failure result. None
of these attempts is counted as live-provider qualification. Authentication or a
working provider configuration is still required for the remaining acceptance.

### OpenCode remote qualification

On pin `a55065f49a7b2a8620bc6a3547a4c13c4d15d791c2b7e8fadcb58558ef9d7605`,
OpenCode 1.18.27 with explicit `opencode/nemotron-3.5-lightning-free` passed the
shared prompt and recorded-session recall workflow in task
`task_bfaec736dece36c7732f86abccb2f95009ed1eb2f777462d68e290c1fe9ca73a`.
Both downloaded artifacts matched the expected bytes and independent gates passed.
Cancellation task
`task_14a49442b7f60e08594ade9a14ea27437ea175123390ecccac6a2fedfd651c67`
observed the provider PID before cancellation, then confirmed it stopped with
exit 130 and a ready result. The default unconfigured model timed out at its
180-second execution deadline; it is not counted as a successful model run.

### Host authentication acceptance

`make lint test test-fleet` passed on macOS after adding host authentication.
The auth storage and CLI checks also passed under macOS UBSan. Linux
`make sanitize-fleet` passed with ASan/UBSan; after the final sanitized error
message adjustment, the auth CLI suite passed again under both platforms'
sanitizers. Final shell completion changes passed lint and generated Bash, Zsh,
and Fish syntax checks.

The auth tests cover private-file enforcement, symlinks, hard links, oversized and
malformed JSON, stale source/destination approvals, concurrent writes, selected
provider preservation, dynamic-key refusal, native login argv, absent capabilities,
and credential-echoing or partial transport failures. See
`tests/c/test_agent_auth.c` and `tests/test_agent_auth.sh`.

Final private VPS pin
`9e907543bf6545c353041bf13a4502b389c4de9c812740cbe7715d40d0fcfc2f`
passed a synthetic credential copy over real SSH; downloaded bytes matched exactly
and the destination was mode 0600. All 125 runtime source files matched between the
local checkout and Linux build source. The synthetic copy test used an isolated credential directory and granted no
provider account access. Approved real credential transfer and its subsequent
provider qualification are recorded below.

Local evidence is retained in `/tmp/hydra-remote-agent-qualification.flUOhs/`
(`auth-proof-evidence.json`, `auth-final-source-checksums.json`, and
`linux-auth*-sanitize.log`). The local regression log is
`/tmp/hydra-host-auth-checks.log`.

### Authenticated Codex and Pi on the VPS

After explicit user approval, Hydra copied the local Codex authentication cache
and Pi's `openai-codex`, `openrouter`, and `anthropic` entries with fresh preview
bindings. Other Pi entries were preserved. Codex's native login status then
reported ChatGPT authentication. Pi qualification used OpenRouter with explicit
`google/gemini-3.6-flash` in both invocation recipes, avoiding the Anthropic
subscription credential in the third-party harness.

On private pin
`9e907543bf6545c353041bf13a4502b389c4de9c812740cbe7715d40d0fcfc2f`:

| Profile | Shared workflow task | Exact prompt and recall | Observed-process cancellation |
| --- | --- | --- | --- |
| Codex 0.153.3 | `task_bfa221c694a55a55437cb6fdd7922584016dfad809dc9bb9c0f3d5087d66cdf1` | Passed | Passed |
| Pi 0.84.3, OpenRouter/Gemini | `task_588ca53e69e33674c80afc9be6c415aaed50658ee312bf65ecd9efa70a2851dd` | Passed | Passed |

Both workflows completed their independent gates. Both downloaded first and
recalled artifacts were checked byte for byte against the expected nonce.
Cancellation tasks
`task_f78bf2e75aeb45a0d60628af2166396a9194fbc3f2071abaa473fbb071767a6e`
and `task_c195e5333a0c159cf21fdc716dca6f2a84cb839ac4988727797e7473196e0841`
first observed each provider under its task owner, then used public task cancel.
The observed provider PIDs disappeared; task runtime reported `cancelled`,
`confirmed_stopped`, exit 130 and a ready sealed result. Inner agent receipts
retain their interrupted running snapshot; the outer task cancellation record is
the terminal authority for these task-wide cancellations.

The private qualification directory retains `auth-approved-*-copy.json`,
`codex-authenticated-exact-evidence.json`, `pi-openrouter-exact-evidence.json`, and
the corresponding `*-cancellation-evidence.json` files. These copy receipts contain
paths and digests, not credential contents. Claude native host sign-in remains the
last live-provider qualification dependency and is deferred at the user's request.

## Acceptance coverage

| Requirement | Authoritative checks and evidence |
| --- | --- |
| Named file/structured data, types, sizes, paths and digests | `tests/c/test_workflow_data.c`, `tests/test_workflow_data.sh`; real Codex-to-Pi handoff below |
| Missing/tampered output blocks dependent execution | `tests/test_workflow_data.sh` checks absent output after exit zero and changed sealed bytes before dispatch |
| Durable action/evidence-bound approval, explicit resume, expiry and decision source | `tests/test_workflow_approval.sh`, `tests/task_approval_cases.sh`, and real SSH approval task records above |
| Failure-class retry/backoff and recovery without replaying uncertain side effects | `tests/test_workflow_runtime.sh` and `tests/test_workflow_schema.sh`; completed-attempt and non-idempotent restart assertions |
| Versioned declarations, literal argv and exact session binding | `tests/c/test_agent_profile.c`, `tests/test_agent_execution.sh`, and local/remote recorded-session comparisons |
| Small event translations, dated declared/probed/observed capabilities | Provider JSONL fixtures, `tests/c/test_agent_profile.c`, and actual agent receipts with executable versions and timestamps |
| Supervision, malformed/partial events, stale instances, unsupported capability refusal | `tests/test_agent_execution.sh`; live cancellation task evidence above |
| Verification independent of provider completion | A successful two-agent fixture workflow is rejected by its failing independent gate; live workflows separately compare outputs before passing gates |
| Opt-in bounded payload retention and unknown usage | `tests/c/test_agent_profile.c` checks byte/run retention bounds; `tests/test_agent_execution.sh` checks default absence, explicit retention and null usage |
| Original named providers on the same local and remote task | Original local matrix complete; remote Codex/Pi/OpenCode/plain complete; Claude remote explicitly deferred |
| Antigravity/Cursor/OpenCode expansion | Public builtin argv/resume/failure conformance in `tests/agent_builtin_cases.sh`; Antigravity and fresh OpenCode live evidence below; Cursor live execution requires authentication |
| Host credential setup | `tests/c/test_agent_auth.c`, `tests/test_agent_auth.sh`, real SSH synthetic copy, and approved real Codex/Pi transfers |

## Live Codex-to-Pi artifact handoff

A final task uses different heads and different real providers. Codex produces a
specific instruction artifact. Pi's `prompt_input` references that sealed output
through `{step: produce, output: instruction}`. A separate compare step checks
both the produced instruction and Pi's answer against expected files; only then
can an independent gate pass.

The exact recipe is retained in the qualification source repository as
`.hydra/workflows/handoff.yml`, `.hydra/workflows/handoff.json`, and
`verify-handoff.sh`. The selected request, exact source commit, bounds, profile
names and content hashes are retained in `handoff-spec.json`,
`handoff-package.json` and `handoff-preview.json`. Reproduce the public transport
with the same configured profiles and credentialed host:

```sh
hydra fleet task prepare --source "$SOURCE" --spec handoff-spec.json \
  --output handoff-package.json
hydra fleet task submit build --input handoff-package.json --key NEW_KEY \
  --trust-spec SPEC_SHA256
hydra fleet task status build --id TASK_ID
hydra fleet task result build --id TASK_ID --output handoff-result.json
```

Inspect the prepare result for `SPEC_SHA256` and the submission result for
`TASK_ID`. Use new output filenames when repeating a run. The final comparison
also checks that the SHA-256 of Codex's downloaded artifact equals Pi's recorded
`prompt_sha256`, establishing exact transport across the provider boundary.

This passed on pin
`9e907543bf6545c353041bf13a4502b389c4de9c812740cbe7715d40d0fcfc2f`
in task
`task_f36b0ce48bd9f604ce7fe77dacfc814dbf15496a36efb1adb6c4a6353daa947e`,
workflow run `run_bc7f36097b96f13cd817`. Producer and consumer used distinct heads.
Both the producer artifact and consumer prompt hashed to
`6aec143a8fa3ca17847fc5c85c0e75eb0913daa573958eaeb21cbc8e79eebec6`.
The exact consumer output and independent gates also passed. The downloaded
result and final comparison are retained as `handoff-result.json` and
`handoff-exact-evidence.json` in the qualification directory.

## Suspended cancellation deadline

The final acceptance run exposed a five-second client deadline expiring while a
cancelled approval workflow sealed its result. Cancellation now uses 30 seconds
per handshake/request, with a public `--timeout 1..300` override. The receiver's
execution and cancellation bounds are unchanged. Transport loss still reports
`outcome_unknown` and never automatically replays a mutation.

`tests/task_approval_cases.sh` runs through the public CLI and real receiver with
six seconds of controlled cancellation transport delay. It checks invalid bounds,
a one-second deadline returning `outcome_unknown` with the workflow still waiting,
and default cancellation returning `cancelled`, `confirmed_stopped`, and a ready
sealed result. This client deadline change follows the remote provider pin above;
it does not change provider execution or the evidence recorded by those runs.

Final local verification after the cancellation deadline fix passed on 6 September
2026: `make test-all` and `make sanitize-fleet` (the macOS target uses
UndefinedBehaviorSanitizer). The sanitizer log contained no runtime-error or
sanitizer findings. Logs are retained locally as
`/tmp/hydra-final-deferred-claude-test-all.log` and
`/tmp/hydra-final-deferred-claude-sanitize-fleet.log`. Claude remote qualification
remains deferred; these checks do not close that live-provider requirement.

## Antigravity, Cursor, and OpenCode expansion, 6 September 2026

Built-in `agy` and `cursor` headless contracts now join the existing OpenCode
contract. Interactive launch adds Antigravity/OpenCode and changes Cursor to the
separate `cursor-agent` CLI. Current help/version inspection covered Antigravity
1.1.27, Cursor Agent `2026.09.02-c22c1a3`, and OpenCode 1.18.27. Normal provider
permission settings were preserved. Tests also preserve a previously registered
custom profile when an upgrade introduces its name as a built-in.

| Check | Antigravity | Cursor Agent | OpenCode |
| --- | --- | --- | --- |
| Actual version/help probe | Passed | Passed | Passed |
| Builtin literal argv, exact recorded resume, partial stream and failed-event conformance | Passed with synthetic executable | Passed with synthetic executable | Passed with synthetic executable |
| Local provider result bytes match raw provider JSON | Passed | Not qualified: unauthenticated | Passed |
| Local recorded-session recall | Passed, byte-identical answer | Not qualified: unauthenticated | Passed, byte-identical answer |
| Local observed print-process cancellation | Passed | Not qualified: unauthenticated | Earlier local qualification passed |
| Real SSH provider execution | Not qualified | Not qualified | Earlier remote qualification passed |
| Native fleet sign-in recipe | `agy` startup; SSH argv tested | `cursor-agent login`; SSH argv tested | Existing `opencode auth login` |

Native sign-in recipe tests do not authenticate accounts. `cursor-agent status
--format json` reported `isAuthenticated: false`, and public profile execution
returned exit 1 in `run_429a82e2e58e5c05cd3e`. No successful Cursor answer or resume
is claimed. Portable credential copying for Antigravity/Cursor is unsupported;
they use provider-owned native sign-in.

A disposable Git repository and Hydra state directory are identified by
`/tmp/hydra-expanded-agents-current`. They retain the selected prompts, independent
comparisons, public CLI responses, and `*-fidelity-evidence.json`,
`*-resume-evidence.json`, and `agy-cancellation-evidence.json`. The temporary head
was stopped through public `hydra kill`; copies of its answers remain in the
qualification directory's `answers/` folder. Only the explicit
fidelity runs enabled bounded `--retain-raw`; the normal invocation default remains
o provider-payload retention.

Fidelity runs requested a fixed token with no extra words, then independently
compared the result file with the terminal provider JSON text. Token-content
checks ignored surrounding whitespace; byte-preservation checks did not.
Antigravity emitted 22 bytes (including a final newline) in
`run_0829a4f76da16dd965c1`, SHA-256
`269295387bbb3fff75ee61ba63b9faeea0c978bc2cbb9841f5b33b15a2451e50`.
OpenCode emitted 21 bytes in `run_b841bef376bd9133eb62`, SHA-256
`a4050dbba2a0f881e8d1b1483e3bf11212ceeab6ec68b69bfab445188a724a6c`.
Both result files matched the raw provider text exactly. Exploratory prompts
requesting exact newline counts did not pass formatting checks: Antigravity added
newlines and OpenCode omitted them. Hydra did not trim or rewrite either response;
these records are adapter-fidelity evidence, not a claim of exact model formatting
or completion of the original no-newline remote task by the new providers.

Antigravity resumed `run_7cb2031538066a13231a` as
`run_e6715820075a5541fbd5`, retaining session
`356009c6-d5c0-49b8-a51c-0c14d598ec45`. OpenCode resumed
`run_d6dbd458700c6be02cb4` as `run_d9135ef5548cbbeff4ef`, retaining session
`ses_f8813f29effecOjb0gCGO1gP8U`. Follow-up prompts omitted the prior token.
Each pair had identical output bytes, the same profile digest and session identity,
and an observed successful resume receipt.

Cancellation run `run_e9414fa74d4e951bec62` first observed Antigravity's actual
`--print` process, not a version/help probe, beneath the public exec owner. Sending
SIGTERM to that owner returned shell status 143; the observed provider PID was gone
and the receipt reported `cancelled`, exit 130. No provider completion was used as
a verification gate.

Reproduce on an authenticated host with a disposable no-agent head:

```sh
hydra agent probe agy
hydra exec --branch worker --profile agy --prompt-file prompt.txt \
  --require prompt,observations,resume --result-file answer.txt --retain-raw \
  --timeout 90 --exit-code --json
hydra exec --branch worker --profile agy --prompt-file follow-up.txt \
  --resume-run RUN_ID --result-file recalled.txt --timeout 90 --exit-code --json
```

`RUN_ID` comes from the successful first exec response. Compare the returned files
independently and inspect the exact recorded session. Replace `agy` with `cursor`
or `opencode` to exercise their contracts; install and authenticate each provider
first. Cursor keeps usage null and rejects `--require usage` before starting.

Expansion checks on 6 September 2026 passed: `make test-all` and
`make sanitize-fleet` (UndefinedBehaviorSanitizer on macOS, with no reported
runtime findings). Final localized prompt/custom-profile refinements were checked
with `tests/test_profiles.sh` (40 passed), `tests/test_operations.sh` (37 passed),
and the full ShellCheck/dash lint. All local link targets in the 19 changed
Markdown files resolved, and `git diff --check` passed. Local logs are
`/tmp/hydra-expanded-agents-test-all.log`,
`/tmp/hydra-expanded-agents-sanitize-fleet.log`,
`/tmp/hydra-expanded-agents-profiles.log`, and
`/tmp/hydra-expanded-agents-operations.log`.
