# Reviewed host enrollment

H2 turns one successful `fleet qualify` result into an explicit, reviewable
operation intent. Qualification must include the `peer_fingerprint` reported by
the authenticated SSH session. A hostname, inventory record, or known_hosts
entry cannot supply this field.

Create an intent after reviewing the selected candidate and project mapping:

```sh
hydra fleet enroll review --input qualification.json \
  --candidate cand_... --project /srv/project --output enrollment.json
hydra fleet enroll review --input qualification.json \
  --candidate cand_... --project /srv/project \
  --package /tmp/hydra-linux.json --sha256 PACKAGE_SHA256 \
  --prefix /home/operator/.local/share/hydra/fleet --output enrollment.json
```

The output is a schema-1 `fleet-enrollment-intent`. Its `intent_sha256` is the
hash of the reviewed fields before the digest field itself is added. Changing
the candidate, accepted peer fingerprint, principal, required capability,
project, package, prefix, alias, or source evidence therefore requires a new
review and digest.

Apply requires the exact reviewed digest as an operator confirmation:

```sh
hydra fleet enroll apply --input enrollment.json --confirm INTENT_SHA256
```

Apply rechecks the authenticated peer fingerprint before invoking the existing
hash-verified bootstrap boundary and fleet `init` operation. It reports a host
row with `enrolled`, `failed`, `host_key_changed`, or `outcome_unknown`. A lost
mutation response is retained as `outcome_unknown`; the command never retries
it or infers success. Inspect and reconcile the receiver before any later
operator action.

The intent is an operator-selected review record, not a second execution
authority or a credential store. Enrollment never copies private keys, accepts
host keys, trusts repository configuration implicitly, or submits a task.
