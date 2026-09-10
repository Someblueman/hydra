# 9D feature outcome evidence

This record covers the deterministic `catalog-slug` feature pilot only. It does
not make a performance or research claim.

The source was copied into an isolated disposable Git repository and compiled
from the public plan after commit `484de74` (`codex/9d-public-outcomes`). The
compiled admission digest was
`83556f1c588a98501516ae1fabfb48d003161186dd652c7fd305df8e41eb5143`.

The positive public workflow completed successfully:

- run ID: `run_29af5e7a1073856ff401`
- state directory: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.F47y84L0t6/home/state/v2/projects/project_076e6936c74c28c10666/workflows/runs/run_29af5e7a1073856ff401`
- final event: `run.succeeded`
- verifier artifact: `steps/feature-check/attempt-1/outputs/verification.json`
- verifier artifact size: 4,127 bytes
- verifier report: schema 3, domain verdict `pass`, executable recipe cases for normalization, bounds, and CLI, with subject, validator, and recipe bindings.

The negative public fixture replaced the implementation with a stub while
leaving the CLI executable. Its workflow reached the independent check and was
retained as recovery-required rather than accepted:

- run ID: `run_ccea62a1dc8533867345`
- state directory: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.v0bjY8xTcp/home/state/v2/projects/project_d97edfc28f81546e523b/workflows/runs/run_ccea62a1dc8533867345`
- final event: `run.recovery-required`

A separate smoke check against an unmodified executable stub recorded that a
superficial presence check succeeds while the functional assertion fails:

```
candidate=/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.kflcTyp5xc/catalog-slug
executable_presence_exit=0
process_exit=0
functional_assert_exit=1
expected=hydra-catalog-42
actual=
```

The negative result demonstrates why executable presence or a green process
exit cannot substitute for the independent feature behavior checks.
