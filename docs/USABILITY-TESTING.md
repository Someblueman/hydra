# Automated usability evaluation

Proposed implementation plan, 13 September 2026. This document defines the next
test capability; it does not claim the runner or current product passes these checks.

## What we need to establish

Can a user open Hydra, give an agent a task, understand progress, intervene, and
review the result without learning Hydra's internal machinery? Can they add a
remote machine and continue in the same workspace?

Automate observable failures and preserve evidence for human judgment. A model's
opinion of a screenshot is advisory; it cannot certify usability. A successful
script that knows hidden shortcuts does not establish discoverability either.

## Build on the current tests

`tests/termviz/pty_support.{c,h}` already provides real PTY input, resizing, screen
inspection, clear/overflow counters and HTML screen capture. Attached PTY tests
exercise real attachment. Retain these helpers and their focused regression tests.
The existing `test-tui-pty` target also uses canned producers, and the onboarding
test explicitly initializes a no-agent head. Those remain useful engineering
checks, but do not represent first-use agentic acceptance.

Add a small journey runner around a fresh prefix installation, real CLI, real
tmux and disposable Git repositories. The runner owns isolation and cleanup so the
person iterating runs one command, not a page of environment setup. Do not replace
the production frontend, mutation path or state producer with a fixture.

## Three complementary evaluation modes

1. **Repeatable interaction checks.** Drive actual keys against the installed
   product. Use a clearly labeled deterministic provider fixture for repeatable
   questions, Unicode/diffs, streaming output and failed checks. It must exercise
   the public provider boundary; it cannot fabricate Hydra state or qualify a real
   provider. Verify visible results against Git/process/state evidence independently.
2. **Goal-driven discoverability trials.** Give an evaluator only the task goal,
   the rendered screen and permitted keyboard/mouse actions. No source, private
   state, exact key sequence or hints from the regression runner. Record its actions,
   backtracking, requests for help, time and stopping point. A separate checker
   verifies outcomes. A bounded screenshot-driven agent can run these trials often;
   human trials remain necessary because an expert model is not a novice user.
3. **Real terminal/provider/remote trials.** Run the same journeys in the supported
   terminal app with an authenticated provider and a disposable SSH host. Capture
   video as well as output. This detects terminal behavior a custom screen parser
   may miss and qualifies actual remote setup. Missing credentials or infrastructure
   means blocked, not passed. Use explicitly authorized hosts and bounded provider
   spend; never silently borrow an operational machine or copy credentials.

## Initial journeys and pass conditions

| Journey | Observable acceptance |
|---|---|
| Open a clean repo | `hydra` reaches the control centre; repo files/status unchanged; start work from visible controls without prior init/spawn. |
| Discuss and approve a plan | Converse, revise and approve the displayed exact plan without authoring JSON; no execution before approval. |
| Find progress and a result | Locate current work and pending input; after a two-file commit show the task diff, not an ambiguous zero-change summary. |
| Navigate every view | Shared navigation remains reachable, including statistics; Tab/Shift-Tab, Enter and Esc match displayed behavior and preserve context. |
| Attach and leave | Reach agent input, return to Hydra and close the view without stopping the agent; repeated resize/attach leaves no duplicate or residual screen. |
| Read output | Supported Unicode and colored diffs survive; long lines remain readable; captured text is clearly distinct from interactive input. |
| Remove a head | In-app confirmation and result, dirty-work protection, refreshed selection, no extra return-to-UI keypress. |
| Recover from a problem | A missing terminal or failed check explains the actual problem and offers an appropriate action without inferring completion or lost work. |
| Onboard a remote host | From SSH destination, review and authorize compatible installation, authenticate provider, select project, then run and inspect work in the same UI. |
| Understand empty views | Empty history, unsupported metrics and absent coordination are explained; unknown data is not represented as zero. |

Use these as desired-behavior tests. Keep current failures red in the usability
report instead of teaching the runner workarounds, hiding them with expected-failure
labels, or weakening assertions to match the implementation. Test collection and
infrastructure failures must remain separate from product failures.

## Evidence and measurement

Each run records build commit and installed hashes, OS/terminal/provider versions,
dimensions, fixture identity, timestamped actions, terminal bytes, screen captures,
and the first failed expectation with its surrounding frames. Record fixture or
live-provider mode prominently. Keep failure artifacts outside the tested repo.
Avoid credentials in captures; live artifacts require review before publication.

Measure task completion, time to first useful conversation, navigation actions,
backtracks, shell escapes, unexpected acknowledgement prompts and lost-context
events. Establish baselines before assigning timing budgets. Hard requirements
such as no repo clutter or no post-success acknowledgement do not need a score.
Report individual failures; an average usability score must not hide a trapped pane.

For flicker, collect timestamped clear/repaint events and changed-cell regions
during settled idle, streaming and resize. Mask only declared dynamic fields such
as clocks. Repaint counts flag regressions but cannot establish perceptual flicker;
review video from the actual terminal before closing that defect.

Use screen comparisons for structure, focus, clipping and corrupt output. Retain
reviewed reference captures tied to real states. Do not auto-accept new snapshots
after a failure or use the production renderer as the sole independent oracle.

## Small first delivery and iteration loop

Start with four regression journeys: clean entry, navigation through statistics,
attach/leave/resize, and in-app head removal. These directly reproduce reported
problems without needing a paid provider or remote host. Add one goal-driven trial
for finding and leaving agent input. Then extend to conversation-to-plan and remote
onboarding with real-provider qualification alongside repeatable fixture checks.

Proposed interface, to be implemented: `make test-usability` runs the bounded local
journeys and writes an HTML report with pass/fail/blocked results and linked captures.
Keep live provider/SSH runs explicitly selected and configured. Existing quality
checks remain independent; an open usability gate must be visible in release review.

For each fix: reproduce the failure, retain a failing journey, implement the change,
rerun that journey, inspect the evidence, then run the small core set. Use a fresh
goal-driven trial when navigation changes. Human review concentrates on the recorded
experience and visual target rather than repeating setup and every keypress.
