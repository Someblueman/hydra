# Agent adapters

Hydra 1.6 keeps provider state non-authoritative. Agent profiles declare launch,
task, resume, environment, adapter, and confidence capabilities. `hydra agent
doctor` probes executable availability and reports the declared surface.

No provider-specific hook is enabled merely because an executable exists. The
locally inspected Claude Code and Codex help surfaces establish launch and resume
flags, but do not establish a stable, versioned hook payload contract. Hydra
therefore ships no automatic provider hook adapter for those versions. This is the
capability-probed fallback: Tier 0/1 remains complete, and missed or unavailable
hooks leave observed status explicitly unavailable rather than inferred.

The universal integration point is canonical adapter JSON v1 on standard input:

```sh
printf '%s\n' '{"schema_version":1,"instance_id":"instance_...","kind":"observed","status":"idle"}' |
  hydra adapter ingest feature
```

The object must use exactly that field order and contain only restricted identifiers.
`kind` is `observed` (`starting`, `running`, `idle`, `exited`, `failed`, or
`unavailable`) or `outcome` (`done`, `failed`, `blocked`, `abandoned`, or
`canceled`). Input is bounded to 8 KiB. Hydra rejects malformed input, unknown
values, and any event whose instance is not currently active. Accepted input is
translated into the provider-neutral lifecycle state and Events v1 stream.

Provider adapters may be added only with a dated version probe, fixtures from the
documented provider surface, deterministic absence/version-skew behavior, and the
same generic ingest boundary.
