# 9D research outcome

The current deterministic research recipe was executed through the public plan workflow in two separate disposable source repositories. It uses no model provider. The independent checker recomputes both non-preemptive schedules from the sealed 12-job synthetic trace, including input-order tie breaks, waits and nearest-rank p95. Five cases cover FCFS, SJF, recommendation, scope and provenance across all six obligations.

The substantive result is **neither qualifies**: FCFS has p95 turnaround 19 and maximum wait 18 (limit 16); SJF has p95 turnaround 31 and maximum wait 22. This valid negative completes the specified investigation. It does not establish a production policy ranking or satisfy an optimization objective.

The checker binds the exact question, both competing explanations, thresholds, source hash, transformations, claim locations and limits. Raw observations preserve the claimed and independently recomputed values. Malformed instrumentation is invalid; a well-formed unsupported claim is valid evidence of a failed claim.

## valid-negative

- Public run: `run_6bad0dba53a686be63bf`; state `succeeded`; run/result exits `0/0`.
- Accepted plan: `3067e0b9d2748c1f09208658e57b3ab8b053ef9c1186a4a00cea37a97ed852f6`.
- Source: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-research-public-syj3_8l6/valid-negative/repo`.
- State home: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-research-public-syj3_8l6/valid-negative/home`.
- Subject: `94ee1e5025015a0df25100c08d6d988c2d049245a27fc5e87f0e3efa7711aace`.
- Assessment: schema 3, evidence `valid`, domain `pass`.

## unsupported-claim

- Public run: `run_f9dc3303897f9e21631e`; state `failed`; run/result exits `1/1`.
- Accepted plan: `0185bf5dc7a1ff6f9ca250fc05698c1b89ec5ea60a0a730a98fde5e9524e95a0`.
- Source: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-research-public-syj3_8l6/unsupported-claim/repo`.
- State home: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-research-public-syj3_8l6/unsupported-claim/home`.
- Subject: `c32040045369b5559e3fcf708800ab6c63a98cff67e0934d94f0e2a0f696f5e1`.
- Assessment: schema 3, evidence `valid`, domain `fail`.

In the negative control, composition succeeded but the independently checked recommendation `SJF is generally best` failed. The public result gate rejected it. Five focused unit tests also cover altered scope/provenance/question/claim locations/explanations, missing measurements, false metrics and malformed CSV. These are bounded deterministic checks, not general prose assessment.
