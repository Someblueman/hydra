# H2 reviewed enrollment acceptance

Local qualification completed on 2026-09-10 (Europe/London), on
`codex/h2-reviewed-enrollment`, from integrated baseline `5fdb0e1`.
This evidence covers the final reviewed client, receiver, transport and installer
changes in the accompanying commit. It does not establish external host/provider
qualification, publication, or release.

- Public client tests: **16 passed**. They exercise exact confirmation and altered
  intent fields, changed local package/SSH policy, accepted key mismatch, existing
  alias conflicts, duplicate submission, interrupted preflight and resumption,
  a ten-host mixed apply preserving six successes, pinned upgrade of an older
  compatible receiver, read-only exact-prefix verification, dropped init/install
  responses, and changed installed bytes after an uncertain install.
- Real receiver tests: **6 passed**, including actual init invocation counting,
  concurrent duplicate claims, owner death, changed request/key bindings,
  unwritable state, corrupt/legacy receipts and exact saved-result replay.
- Ephemeral strict loopback OpenSSH: **2 passed**. Public qualification through
  pinned package installation, project init and alias creation agrees with the
  selected key, principal, package bytes, project and exact prefix. Closing the
  authenticated master immediately before init causes zero init effects; a new
  authenticated connection with the same intent reconciles to exactly one init.
- The same **16 client + 6 receiver + 2 SSH tests passed with UBSan** and
  `UBSAN_OPTIONS=halt_on_error=1`. Discovery's five tests also passed with UBSan.
- Default discovery: **5 passed**. Existing fleet CLI transport tests passed.
  Public installer regression: **19/19 passed**. Build and ShellCheck/dash lint
  passed. Full C quality: **186 advisory functions, no baseline regressions**;
  no complexity ceiling was increased.

The primary agent inspected the integrated H2 diff. The existing independent
reviewer closed the original identity, prefix, recovery and older-receiver
compatibility findings against the corrected source and bounded evidence.
Combined release-branch acceptance is recorded separately after local integration.

Logs are retained in the H2 checkout under `build/h2-client-final.log`,
`build/h2-real-ssh-final.log`, `build/h2-final-ubsan-{client,receiver,ssh,discovery}.log`,
`build/h2-{discovery,fleet,install}-final.log`, `build/h2-build-lint-final.log`, and
`build/h2-quality-accepted.log`.
