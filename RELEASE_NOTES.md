# Hydra v2.2.0 Release Notes

Release date: 2026-09-07

Hydra 2.2.0 adds workflows with named inputs, sealed artifacts, durable approvals,
and retry policies. Headless agents run through explicit adapter contracts, while
local objective plans compile into reviewable workflows accepted by exact digest.

## Highlights

- Carry file and structured inputs between steps. Declared outputs are sealed and
  verified before dependents consume them; missing or changed artifacts block work.
- Suspend local workflows and remote tasks for durable approval, then resume with
  decisions bound to the reviewed evidence. Declared idempotent steps can retry
  selected failure classes with durable backoff deadlines.
- Run headless agents with capability checks, bounded event translation, exact
  recorded-session resume, safe-point steering, supervised cancellation, and
  optional bounded payload retention. New profiles include Antigravity and Cursor
  Agent, alongside expanded OpenCode integration.
- Inspect and validate bounded JSON objective plans, preview their deterministic
  compilation, and authorize local execution by exact digest. Final delivery
  requires passing verification reports bound to sealed deliverable bytes.
- Sign in to providers on selected fleet hosts or preview and explicitly approve
  copying private credentials. Pi and OpenCode support selected-provider merges.
- Receive reliable cancellation receipts and terminal workflow state, with failed
  or truncated Git fingerprint probes and malformed agent output rejected.

## Compatibility and upgrade

Upgrade the shell CLI and optional native helpers together: all executable version
handshakes are now 2.2.0. Existing state v2, event/JSON schemas, and core, TUI, and
fleet protocol versions remain unchanged. Existing command-based workflows remain
supported; agent profiles and workflow data are opt-in additions.

Interactive Codex heads preserve cwd-scoped `codex resume --last`, including heads
without a recorded provider session identity. Headless `hydra exec --resume-run`
remains separate: it resumes only the exact session in the selected successful
run receipt, bound to the same head, instance, worktree, and profile. See the
[agent resume contract](https://github.com/Someblueman/hydra/blob/v2.2.0/docs/AGENT_CONTRACT.md#workflow-profiles-and-migration).

The attached tar.gz and zip archives contain the exact release source; SHA256SUMS
verifies both downloads. The shell CLI remains compiler-free. To build the optional
native helpers, use `make build-core build-tui build-fleet`; fleet additionally
requires JSON-C development files and pkg-config. JSON-C is linked statically.
Run `sh install.sh` from the unpacked source to install into the documented prefix.
Users upgrading from 1.9 should first follow the documented 2.0 state migration.

## Scope and qualification limits

Objective DAG execution is local. Cross-host DAG coordination, automatic placement,
dynamic graph expansion, and scheduled task pools remain roadmap work. Remote
workflows still execute on one selected host.

Provider capabilities and authentication differ by host. Cursor local/remote,
Antigravity remote, and Claude Code remote live qualification remain outstanding;
Claude remote sign-in is deferred. Fixture and platform tests do not replace those
provider checks. See the
[workflow and agent acceptance matrix](https://github.com/Someblueman/hydra/blob/v2.2.0/docs/WORKFLOW_AGENT_ACCEPTANCE.md)
and [planning evidence](https://github.com/Someblueman/hydra/blob/v2.2.0/docs/evidence/plan-qualification.md).

Fleet remains a trusted-host tool with strict SSH host-key checks. A successful
agent run or collected remote result does not confer review approval or permission
to promote it. Credential copying requires approval of the exact preview digest.
