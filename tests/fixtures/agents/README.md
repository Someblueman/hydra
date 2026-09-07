These bounded JSONL fixtures describe translation contract version 1. They are synthetic protocol examples, not live-provider qualification. Run them through `make test-fleet`; malformed, partial, permission and cancellation boundaries also run through the public CLI in `tests/test_agent_execution.sh`. Provider versions and live results must be recorded separately.

Adapters cover Antigravity (`agy-jsonl`), Cursor Agent (`cursor-jsonl`), OpenCode,
Claude Code, Codex, Pi, and the provider-neutral canonical format. Antigravity uses
an `event` discriminator and a nested terminal result; Cursor uses `type` and
does not document token counts. Fixtures intentionally keep Cursor usage absent.
`tests/agent_builtin_cases.sh` exercises the exact built-in argv and recorded
resume paths through public `hydra exec`, using synthetic executables. It also
checks partial streams and failure events from providers that exit zero.

Format references: [Antigravity](https://antigravity.google/docs/cli/headless/),
[Cursor](https://cursor.com/docs/cli/reference/output-format), and
[OpenCode](https://opencode.ai/docs/cli/). Live versions and qualification limits
are recorded in `docs/WORKFLOW_AGENT_ACCEPTANCE.md`.
