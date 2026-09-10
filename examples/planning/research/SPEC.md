# Research objective: choose a local scheduling policy for this trace

Deliver a completed report comparing non-preemptive FCFS and shortest-job-first
(SJF) on the supplied `jobs.csv`, not just a plan or experimental log. One server
starts at time zero. Jobs become eligible at their arrival time. Duration is
known exactly for this offline study. Break ties by arrival, then input order.

Requirements:

- `means`: report mean waiting time and mean turnaround for both policies, with
  waiting = start - arrival and turnaround = finish - arrival.
- `tails`: report p95 turnaround by nearest rank (ceil(0.95*n)) and the maximum
  wait for each policy; explain why those can disagree with mean improvements.
- `fairness`: identify the job with the worst wait for each policy using its
  schedule, and distinguish observed long waits from proof of starvation.
- `recommendation`: choose a policy for this trace subject to p95 turnaround at
  most 22 and maximum wait at most 16, or explicitly report that neither policy
  qualifies. State both constraint checks explicitly; do not relax the limits.
- `reproduce`: identify the data and method, provide enough results to reproduce
  the recommendation, and retain the schedule and metric appendix.
- `limits`: explain the limits of 12 synthetic jobs, exact-duration knowledge,
  one server, non-preemption, and lack of general production evidence.

The source dataset is the authority for this bounded research question. The
source-bound deterministic analyzer writes a JSON report and a separate checker
recomputes schedules, metrics and constraint decisions from jobs.csv. The checker
also enforces the exact question, claim locations, scope explanations and source
provenance required by this exemplar; it is not a general prose judge. A complete
negative recommendation is valid research evidence. Malformed input is invalid
instrumentation and does not become a substantive negative finding.
