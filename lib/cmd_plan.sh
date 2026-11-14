#!/bin/sh
# The short planning flow: an agent proposes a short plan, the user reviews and
# approves it, follows the run and reviews its result. Expert planning stays
# under hydra workflow plan.

cmd_plan_schema() {
    cat <<'EOF'
Hydra short plan form (simple-1). Publish it with: hydra plan propose <plan.json>

{
  "plan": "simple-1",
  "objective": "Add a greeting file and a test for it",
  "steps": [
    {"id": "implement", "agent": "Create greeting.txt with a friendly greeting and tests/greeting.sh that fails unless it is non-empty."},
    {"id": "check", "run": ["make", "check"]}
  ],
  "done_when": [
    {"criterion": "make check passes with the new test", "check": "check"}
  ]
}

plan        Always "simple-1".
objective   What the plan achieves, in a sentence or two.
steps       1 to 63 steps in order. Each has a unique id (lowercase letters and
            digits, separated by single - or _) and exactly one of:
              agent  the worker agent's complete instructions. When it finishes,
                     Hydra commits what it changed on the worker branch.
              run    a command as a list of strings, run in the worker's checkout.
            Optional per step:
              needs    ids of earlier steps it waits for (default: the step before)
              timeout  seconds (default 1800 for agent, 600 for run)
              profile  the agent profile, when the policy allows more than one
done_when   1 to 64 criteria: {"criterion": text, "check": id of a run step}.
            A check runs after an agent step and passes when its command exits 0
            on the commit that agent step left.
id, worker  Optional plan name and worker branch (default: from the objective;
            the worker branch is <id>-work and must not exist yet).
context     Optional repository files the plan relies on (paths).
questions   Open questions for the user; a plan with any cannot be approved.

Every step runs on the one new worker branch; your checkout and other branches
are not changed. The project's plan policy (hydra plan policy) limits the tools
and time a plan may use. The full expert schema: hydra workflow plan schema.
EOF
}

cmd_plan_usage() {
    printf '%s\n' \
        'Usage: hydra plan schema' \
        '       hydra plan propose <plan.json> [--branch <head>] [--asset NAME=FILE]...' \
        'Expert planning (full schema, policies, compile, explain and compare): hydra workflow plan --help'
}

cmd_plan() {
    _cp_action="${1:-}"
    case "$_cp_action" in
        schema)
            [ "$#" -eq 1 ] || { cli_error plan invalid_arguments 'schema takes no arguments' 'run hydra plan schema'; return 1; }
            cmd_plan_schema
            ;;
        propose)
            cmd_workflow_plan "$@"
            ;;
        ''|-h|--help)
            cmd_plan_usage
            [ -n "$_cp_action" ]
            ;;
        *)
            cli_error plan invalid_arguments "unknown plan command '$_cp_action'" 'run hydra plan --help'
            return 1
            ;;
    esac
}
