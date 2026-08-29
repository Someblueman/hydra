# Per-head provenance

Hydra captures provenance when a head is created and when each instance is launched
or resumed. Inspect it with:

```sh
hydra provenance feature
hydra provenance feature --json
```

Head provenance contains the Hydra, Git, and tmux versions; exact recorded base
commit; task SHA-256 and byte count; current trusted-configuration hash; and declared
lifecycle sources. It does not contain task text, environment values, terminal
output, or message bodies.

Instance provenance contains launch or resume mode, resolved profile, executable,
profile version confidence, Hydra version, and the resolved launch/resume recipe.
Built-in executable versions are probed. Custom executable versions are marked
`user-declared`; Hydra does not execute an arbitrary custom `--version` command just
to collect metadata.

Provenance is local evidence, not an attestation. A same-user process can modify the
state tree, and a recorded version or hash does not prove a gate passed. Events name
the project, head, and instance so provenance can be correlated with lifecycle,
handoff receipts, exec results, and teardown/resume history.
