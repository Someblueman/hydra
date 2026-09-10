# 9D research outcome

This bounded investigation was executed through Hydra's public plan workflow from a clean disposable Git source repository. Compilation was kept outside the source repository, and the accepted digest was passed explicitly to the public plan runner.

## Positive investigation

- Source repository: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.uocOGyoxAK`
- Compiled/output root: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.kWa6WGWw3B`
- Accepted plan SHA-256: `7f21b83f00939578207ec09c96a62f9908d8cd258619b0e7b625af6c93a33dce`
- Public run ID: `run_0fa9133dc069b918626b`
- State: `succeeded`; `workflow plan result` returned `verdict: pass`.
- Sealed report SHA-256: `94ee1e5025015a0df25100c08d6d988c2d049245a27fc5e87f0e3efa7711aace`.
- Independent assessment: schema 3, `evidence_status: valid`, `domain_verdict: pass`, all six obligations covered, and three cases executed with zero failures.

The actual recommendation for this trace is **neither qualifies**. FCFS has p95 turnaround 19 and maximum wait 18, so it fails the maximum-wait limit of 16. SJF has p95 turnaround 31 and maximum wait 22, so it fails both limits. The checker therefore records `constraint_checks: {"FCFS": false, "SJF": false}` without relaxing either limit.

The independent method recomputes deterministic non-preemptive FCFS and SJF schedules from `jobs.csv`, using waiting time `start - arrival`, turnaround `finish - arrival`, nearest-rank p95 `ceil(0.95*n)`, and input-order tie breaking. The evidence recipe cases use the valid plan identifiers `fcfs`, `sjf`, and `recommendation`; policy names and reported metrics remain FCFS and SJF.

Source and study limits are explicit: 12 synthetic jobs, one server, exact known durations, non-preemption, and no production evidence. The result qualifies this supplied trace and its reproducibility path. It does not establish a production scheduling choice, a general policy ranking, or an optimization target.

## Negative claim guard

A separate fresh public run intentionally changed the composed claim to `SJF is generally best` while leaving the independent checker and recipe unchanged:

- Source repository: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.6inXFyaebv`
- Compiled/output root: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.oYGhfIIX1q`
- Accepted plan SHA-256: `0cb07ff0b65b826800a959873747a313ea0f2e7d0712a4302390ea51f57d51e8`
- Public run ID: `run_155306363200c8a82e8f`
- State: `failed` at `check`; spawn, analysis, and composition completed.
- `workflow plan result` rejected the run with `invalid_or_stale_plan` because verification did not pass.

This negative run is evidence that unsupported recommendation claims are rejected. It is a claim guard, not a competing result or optimization experiment.
