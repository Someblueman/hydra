# Reviewed host enrollment

`fleet enroll review` selects up to 16 candidates from a saved `fleet qualify`
result. Each candidate must have a compatible handshake and an authenticated
SSH peer fingerprint. Review the source, target, Unix principal, host key,
capability and project mapping before confirming an intent.

```sh
hydra fleet qualify --ssh build --ssh-config /home/operator/.ssh/config \
  --require list > qualification.json
hydra fleet enroll review --input qualification.json \
  --candidate cand_6275696c64 --project /srv/project --output enrollment.json
hydra fleet enroll apply --input enrollment.json --confirm INTENT_SHA256
```

Repeat `--candidate ID` to select a batch. The candidate ID is the default alias;
`--alias NAME` overrides it for a single selected host. Review does not install,
initialize a project, create a remote alias, or accept a host key. Apply requires
an exact `--confirm` value copied from the reviewed `intent_sha256`.

The schema-2 intent contains a `hosts` array and binds each candidate/source,
accepted peer fingerprint, resolved Unix principal/target, required fleet
protocol and capability, absolute project mapping, alias, and optional package
and prefix. It also binds the selected SSH configuration and a SHA-256 digest of
the complete effective `ssh -G` policy. The public policy projection omits
ProxyCommand arguments and private key contents. Changes to these reviewed
fields require a fresh review. Older draft intents must be reviewed again.

A pinned package requires its exact local digest and an absolute destination:

```sh
hydra fleet enroll review --input qualification.json \
  --candidate cand_6275696c64 --project /srv/project \
  --package /tmp/hydra-linux.json --sha256 PACKAGE_SHA256 \
  --prefix /home/operator/hydra-reviewed --output enrollment.json
```

This installs exactly `/home/operator/hydra-reviewed/bin/hydra`. Existing
contents are reused only when all packaged bytes agree; enrollment never
replaces a conflicting alias or installation. Without a package, apply uses
`hydra` on the reviewed host. The existing `fleet bootstrap HOST` command keeps
its default digest-specific destination and alias behavior.

Before mutation, apply re-resolves SSH policy and establishes a dedicated
strict SSH master. It reads the actual negotiated host key from local SSH
diagnostics, compares the reviewed fingerprint, and binds subsequent requests
to that existing master. Loss of the master cannot fall back to another
connection. Project preflight requires an accessible Git working tree. A
compatible older receiver may be upgraded by the pinned package; the installed
receiver must advertise `enrollment-init` before initialization.

Initialization uses the existing `init --no-agent` boundary, without `--trust`.
Enrollment neither copies credentials nor grants repository trust, accepts host
keys, or submits work. Resulting aliases retain the reviewed principal, project,
accepted key and selected SSH configuration.

Progress is stored under `$HYDRA_HOME/fleet/enrollment/<intent-sha256>.json`,
with an exclusive apply lock, per-host operation IDs and durable phase writes
before mutation. Successful rows remain successful when a batch is resumed.
Failures include `review_required`, `host_key_changed`, `capability_changed`,
`project_unavailable`, `alias_conflict` and `outcome_unknown`; the result includes
the host-specific preflight, installation and initialization responses.

Reapplying the same confirmed intent reconciles incomplete operations. An
uncertain install uses the installed helper's read-only `install-check` to
verify every pinned byte at the exact prefix, without uploading/installing
again. Initialization uses a durable receiver record under
`$HYDRA_HOME/fleet/enrollment-ops/`, atomically claimed before the effect. The
record binds the operation ID, project, arguments and expected peer key and
replays the actual saved result. A pending, corrupt or missing outcome never
implies success and is never executed again under an existing claim. If the
receiver cannot establish the outcome, retain `outcome_unknown` and inspect
that receiver before choosing any new operation.

`make test-enrollment` exercises the public CLI and real receiver with controlled
transport failures, including a ten-host mixed batch, interruption, duplicate
submission, owner loss, changed review fields and dropped mutation responses.
`make test-enrollment-ssh` additionally uses an ephemeral loopback OpenSSH server,
strict pinned test keys and real package bytes. It covers public onboarding and
loss of the authenticated master. This local acceptance does not qualify live
external hosts, jump chains or providers.
