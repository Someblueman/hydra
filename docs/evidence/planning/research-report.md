# Scheduling Policy Report: FCFS vs Non-Preemptive SJF on `jobs.csv`

## 1. Objective

Compare non-preemptive FCFS and shortest-job-first (SJF) on the supplied 12-job trace and recommend a policy subject to fixed limits: p95 turnaround ≤ 22 and maximum wait ≤ 16. One server starts at time zero; jobs become eligible at arrival; durations are known exactly (offline study); ties break by arrival, then input order. Evidence is limited to the sealed run reproduced in the reproducibility appendix appended below this report.

## 2. Data and Method

- **Data** (`jobs.csv`): 12 jobs A–L, arrivals 0–12, durations 1–9 (A=9, B=1, C=2, D=1, E=5, F=1, G=2, H=1, I=3, J=1, K=4, L=1). Total work = 31 time units.
- **Method** (`measure.sh`): validates the CSV (header, unique ids, positive integer durations, exactly n=12; exits 2 otherwise), then simulates each policy with awk on a single server from time 0. Among unfinished jobs with arrival ≤ current time, FCFS dispatches by earliest arrival (ties by input order) and SJF by shortest duration (ties by arrival, then input order); if none has arrived, time jumps to the earliest arrival. Dispatched jobs run to completion (non-preemptive).
- **Definitions**: wait = start − arrival; turnaround = finish − arrival (= wait + duration); p95 turnaround by nearest rank, rank = ceil(0.95·n) = ceil(11.4) = **12** for n = 12, i.e., the largest sorted turnaround; max wait and its job read from the schedule.
- **Consistency**: recomputing the aggregates from the appended schedule rows reproduces the appended metric rows exactly (means to 6 decimals; both policies are work-conserving, so both finish at t = 31 and differ only in ordering).

## 3. Results

### 3.1 Means (`means`)

| Policy | n | Mean wait | Mean turnaround |
|---|---|---|---|
| FCFS | 12 | 11.583333 (= 139/12) | 14.166667 (= 170/12) |
| SJF | 12 | 4.000000 (= 48/12) | 6.583333 (= 79/12) |

SJF lowers mean wait by 7.583333 (11.583333 → 4.000000) and mean turnaround by the same absolute 7.583333 (14.166667 → 6.583333); the two differences match because turnaround = wait + duration and every job's duration is counted under both policies.

### 3.2 Tails (`tails`)

| Policy | p95 turnaround (rank 12 of 12) | Max wait | Worst-wait job |
|---|---|---|---|
| FCFS | 19 | 18 | L |
| SJF | 31 | 22 | A |

At n = 12 the nearest-rank p95 is the largest sorted turnaround, so p95 equals the maximum turnaround here (FCFS 19; SJF 31). Tail metrics can disagree with mean improvements because they are order statistics dominated by a single extreme observation, while means average all 12 jobs. SJF's reordering benefits the many short jobs (hence the large mean gains) but defers the one long job A to turnaround 31 and wait 22, which alone set SJF's tail values — worse than FCFS's 19 and 18 despite SJF's better means.

### 3.3 Fairness (`fairness`)

- **FCFS**: worst wait is **job L, wait 18** (arrival 12, start 30, finish 31) — the last arrival queued behind the entire preceding backlog.
- **SJF**: worst wait is **job A, wait 22** (arrival 0, start 22, finish 31) — the longest job (duration 9), repeatedly passed over by later-arriving short jobs.

These are observed long waits, not proof of starvation. Both schedules are finite and complete: all 12 jobs finish by t = 31 under both policies, so every job eventually runs within this trace. A wait of 18 (L) or 22 (A) demonstrates deferral under the observed arrival pattern; starvation (indefinite postponement) could only be established with open-system evidence of continual arrivals blocking a job forever, which a closed 12-job simulation cannot provide.

### 3.4 Constraint checks (`recommendation`)

Fixed limits: p95 turnaround ≤ 22 and maximum wait ≤ 16.

| Policy | p95 turnaround | ≤ 22? | Max wait | ≤ 16? | Both met? |
|---|---|---|---|---|---|
| FCFS | 19 | Pass | 18 | **No (18 > 16)** | No |
| SJF | 31 | **No (31 > 22)** | 22 | **No (22 > 16)** | No |

## 4. Recommendation

**Neither policy qualifies for this trace under the stated limits, and the limits are not relaxed.** FCFS passes the p95 constraint (19 ≤ 22) but violates the maximum-wait constraint (18 > 16); SJF violates both (31 > 22 and 22 > 16). Because each candidate fails at least one constraint, the correct selection outcome is to adopt neither.

For context within this evidence only: the two policies occupy opposite ends of a mean–tail trade-off — SJF is far better on means (mean wait 4.000000 vs 11.583333) while FCFS is closer on the tail constraints (2 units over the wait limit versus SJF failing both). That trade-off does not change the verdict; choosing a policy anyway would require changing the limits or the policy space, which lies outside this trace.

## 5. Reproduction (`reproduce`)

- **Data**: `jobs.csv` as listed in §2.
- **Method**: `measure.sh` with the validation and dispatch rules in §2.
- **Procedure**: simulate both policies under the stated tie-break rules to obtain the schedule; compute wait = start − arrival and turnaround = finish − arrival per job; derive mean wait, mean turnaround, nearest-rank p95 (rank ceil(0.95·12) = 12), max wait and its job; compare against the two limits.
- **Required results**: metric rows — FCFS: 11.583333 / 14.166667 / 19 / 18 / L; SJF: 4.000000 / 6.583333 / 31 / 22 / A — plus the full per-policy schedules, all retained in the reproducibility appendix below. Recomputing from the appendix schedule reproduces every metric value exactly and both constraint verdicts.

## 6. Limits (`limits`)

- **12 synthetic jobs**: a tiny, closed sample. With n = 12 the nearest-rank p95 collapses to the single worst job, so every tail figure hinges on one observation; no variance or confidence statements are possible, and results describe only this trace.
- **Exact durations**: dispatch assumes duration is known exactly. Real systems rarely have this foresight; SJF's advantage here depends on it, and misestimated durations would yield different schedules.
- **One server**: no parallelism or competing resources; multi-server queueing behavior is not represented.
- **Non-preemption**: a started job runs to completion; preemptive alternatives (e.g., shortest-remaining-time, round-robin) were not evaluated and could change both means and tails.
- **No production evidence**: the workload is synthetic and closed; nothing here establishes behavior under open arrivals, heterogeneous traffic, or operational conditions, and no claims beyond this trace are made.

## 7. Reproducibility Appendix (appended below this report)

The two appended tables are the sealed outputs of `measure.sh` for this run.

- **Measured metrics** (one row per policy): `policy`; `n` (jobs simulated, 12); `mean_wait` (average of start − arrival); `mean_turnaround` (average of finish − arrival); `p95_turnaround` (nearest-rank value — for n = 12, the largest sorted turnaround); `max_wait` (largest observed wait); `worst_wait_job` (job attaining that wait). These are precisely the numbers used in §3 and the §3.4 constraint checks.
- **Complete schedule** (one row per dispatched job, in dispatch order): `policy`; `id`; `start`; `finish` (start + duration); `wait` (start − arrival); `turnaround` (finish − arrival). Row order exposes each policy's dispatch sequence and tie-break behavior; per-row waits and turnarounds are the raw values averaged into the metric rows and are the primary evidence for the fairness findings (L waiting 18 under FCFS; A waiting 22 under SJF).

## Reproducibility appendix

Metrics (time units from the input trace):

```csv
policy,n,mean_wait,mean_turnaround,p95_turnaround,max_wait,worst_wait_job
FCFS,12,11.583333,14.166667,19,18,L
SJF,12,4.000000,6.583333,31,22,A
```

Complete schedule:

```csv
policy,id,start,finish,wait,turnaround
FCFS,A,0,9,0,9
FCFS,B,9,10,9,10
FCFS,C,10,12,9,11
FCFS,D,12,13,10,11
FCFS,E,13,18,10,15
FCFS,F,18,19,14,15
FCFS,G,19,21,13,15
FCFS,H,21,22,13,14
FCFS,I,22,25,13,16
FCFS,J,25,26,15,16
FCFS,K,26,30,15,19
FCFS,L,30,31,18,19
SJF,B,0,1,0,1
SJF,C,1,3,0,2
SJF,D,3,4,1,2
SJF,F,4,5,0,1
SJF,E,5,10,2,7
SJF,H,10,11,2,3
SJF,J,11,12,1,2
SJF,L,12,13,0,1
SJF,G,13,15,7,9
SJF,I,15,18,6,9
SJF,K,18,22,7,11
SJF,A,22,31,22,31
```
