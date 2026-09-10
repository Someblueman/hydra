# Python boundaries

Hydra's installed CLI, coordinator and native interfaces remain C and POSIX
shell. The two finite-example precompilers now share one C/JSON-C implementation
in `examples/planning/native`, built with `make build-plan-precompile`.

Python remains useful at these development boundaries:

| Boundary | Reason to retain it |
| --- | --- |
| PTY drivers and terminal assertions | Real terminal interaction and independent screen observations. |
| Public CLI, SSH and protocol drivers | Exercise subprocess, serialization and failure boundaries independently of C internals. |
| Statistical and semantic checkers | Recompute expected values and evidence independently of the implementation. |
| Small planning example payloads | Demonstrate worker inputs, artifacts and checker contracts with little incidental code. |

The superseded toy planner evaluation runner is archived through its exact source
commit. [Replay instructions](../examples/planning/evaluation/README.md) preserve
its contracts, candidates, incorrect controls and recorded limitations without
keeping a second active evaluation framework.

New reusable planning or coordination behavior belongs in the native code.
Straightforward command and filesystem orchestration belongs in shell. Preserve
useful independent Python tests; file count alone is not a reason to rewrite them.
Avoid moving Python into shell heredocs or adding compatibility wrappers for
internal example entrypoints.

The [real cleanup workload](evidence/python-cleanup-20260910/contract.json) records
its per-file inventory, selected plan, rejected alternatives and verification
contract. Historical evidence continues to refer to its recorded source commit.
