# Lifecycle v1

Hydra records three independent channels:

- declared outcome: an attributed request such as `done`, `failed`, or `blocked`;
- observed status: a timestamped observation such as output activity or process exit;
- liveness: current tmux session and process presence.

None implies another. Quiet output is not completion, a missing session is not
success, and an agent declaration is not a passed gate or human approval. Every
declaration names its actor and instance. Only evidence from the current instance can
satisfy its completion policy; earlier instance events remain history.

Lifecycle events use Events v1. State changes use the shared lock protocol. Resume
and regenerate create a new instance and invalidate transient observations while
retaining head history. Dependencies name the required evidence type and source.

