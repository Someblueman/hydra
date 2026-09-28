# Independent fleet acceptance cases. Build shared helpers before starting workers.
TEST_JOBS ?= 4
# Start the longest scenarios early so workers do not leave a long serial tail.
FLEET_CASES = shell-task-acceptance \
	shell-workflow-plan-v3 \
	enrollment \
	plan-repair-combine \
	shell-agent-execution \
	plan-repair-pass \
	plan-repair-pass-plan-source-1-plan-repair-fault-1 \
	report-v2-1 \
	shell-workflow-plan \
	headless-1 \
	plan-repair-exhaust \
	plan-repair-same \
	plan-source-1 \
	plan-task-verdict-pass \
	dag-parallelism-1 \
	dag-crash-2 \
	shell-workflow-approval \
	discovery \
	dag-result-lost-1 \
	plan-source-dirty \
	plan-task-verdict-inconclusive \
	plan-task-verdict-stale-subject \
	plan-task-verdict-missing-coverage \
	native-workflow-contract-cases \
	plan-task-verdict-fail \
	plan-task-verdict-stale-validator \
	plan-repair-crash \
	dag-replay-1 \
	dag-lost-ack-1 \
	plan-task-verdict-crash \
	plan-task-verdict-bad-artifact \
	plan-task-verdict-changed-harness \
	dag-lost-ack-1-dag-fault-cancel \
	shell-workflow-data \
	dag-source-1-dag-source-tamper-1 \
	dag-lost-ack-1-dag-fault-dispatch \
	shell-headless-plan-adapter \
	dag-result-bad-1 \
	dag-lost-ack-1-dag-fault-key \
	dag-lost-ack-1-dag-fault-attempt \
	dag-lost-ack-1-dag-fault-placement \
	dag-lost-ack-1-dag-fault-cancel-offline \
	shell-fleet \
	shell-agent-auth \
	shell-agent-locate \
	shell-remote-agents \
	native-agent-recipes \
	native-plan \
	shell-fleet-install \
	shell-task-package \
	native-fleet \
	native-workflow-schedule \
	native-agent-auth \
	native-agent-profile \
	native-workflow-data \
	plan-repair-pass-plan-repair-budget-1 \
	retention \
	workflow-metrics \
	plan-staged \
	native-task-package \
	native-remote-setup \
	shell-remote-setup \
	native-fleet-assets \
	shell-remote-provision

# Shell cases run through scripts/run-shell-test.sh like make test: private
# TMPDIR/TMUX_TMPDIR, no inherited TMUX, and native binaries from BUILD_DIR.
# These are also used by the shell cases; none may compile concurrently with a case.
FLEET_NATIVE_CASE_BINS = $(addprefix $(BUILD_DIR)/native-tests/,test-discovery test-enrollment test-enrollment-receiver test-retention test-workflow-task-metrics test-statistics-export test-plan-staged workflow-contract-cases discovery-ssh-fixture enrollment-ssh-fixture enrollment-receiver-fixture statistics-evidence)
.PHONY: fleet-test-build
fleet-test-build: build-fleet build-core build-tui build-test-fixture $(BUILD_DIR)/test-shell-exec build-plan-example build-plan-precompile $(FLEET_TEST_BINS) $(FLEET_NATIVE_CASE_BINS) $(BUILD_DIR)/test-statistics $(BUILD_DIR)/fixture-lock

test-fleet: fleet-test-build
	+@case "$(MAKEFLAGS)" in \
		*jobserver*) exec $(MAKE) test-fleet-cases ;; \
		*) exec $(MAKE) -j$(TEST_JOBS) test-fleet-cases ;; \
	esac

.PHONY: test-fleet-cases $(addprefix fleet-case-,$(FLEET_CASES))
# CI divides this same ordered inventory across two runners; local runs keep all cases.
FLEET_SHARD ?= all
ifeq ($(FLEET_SHARD),all)
FLEET_SELECTED_CASES = $(FLEET_CASES)
else ifeq ($(FLEET_SHARD),odd)
FLEET_SELECTED_CASES = $(shell printf '%s\n' $(FLEET_CASES) | awk 'NR % 2')
else ifeq ($(FLEET_SHARD),even)
FLEET_SELECTED_CASES = $(shell printf '%s\n' $(FLEET_CASES) | awk '!(NR % 2)')
else
$(error FLEET_SHARD must be all, odd or even)
endif
test-fleet-cases: $(addprefix fleet-case-,$(FLEET_SELECTED_CASES))

fleet-case-native-plan:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-plan

fleet-case-native-workflow-schedule:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-workflow-schedule

fleet-case-native-agent-auth:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-agent-auth

fleet-case-native-agent-profile:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-agent-profile

fleet-case-shell-agent-auth:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_agent_auth.sh

fleet-case-shell-agent-locate:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_agent_locate.sh

fleet-case-shell-remote-agents:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_remote_agents.sh

fleet-case-native-agent-recipes:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-agent-recipes

fleet-case-shell-agent-execution:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_agent_execution.sh

fleet-case-native-workflow-data:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-workflow-data

fleet-case-shell-workflow-data:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_data.sh

fleet-case-native-workflow-contract-cases:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/workflow-contract-cases" --runtime

fleet-case-dag-replay-1:
	@HYDRA_TEST_DAG_REPLAY=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1:
	@HYDRA_TEST_DAG_LOST_ACK=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-result-lost-1:
	@HYDRA_TEST_DAG_RESULT_LOST=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-result-bad-1:
	@HYDRA_TEST_DAG_RESULT_BAD=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-parallelism-1:
	@HYDRA_TEST_DAG_PARALLELISM=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-crash-2:
	@HYDRA_TEST_DAG_CRASH=2 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-source-1-dag-source-tamper-1:
	@HYDRA_TEST_DAG_SOURCE=1 HYDRA_TEST_DAG_SOURCE_TAMPER=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-plan-source-1:
	@HYDRA_TEST_PLAN_SOURCE=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-source-dirty:
	@HYDRA_TEST_PLAN_SOURCE=dirty BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-dag-lost-ack-1-dag-fault-dispatch:
	@HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="dispatch" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-key:
	@HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="key" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-attempt:
	@HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="attempt" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-placement:
	@HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="placement" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-cancel:
	@HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="cancel" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-cancel-offline:
	@HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="cancel-offline" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_task.sh

fleet-case-headless-1:
	@HYDRA_TEST_HEADLESS=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan.sh

fleet-case-shell-headless-plan-adapter:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_headless_plan_adapter.sh

fleet-case-shell-workflow-plan:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan.sh

fleet-case-shell-workflow-plan-v3:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_v3.sh

fleet-case-plan-task-verdict-pass:
	@HYDRA_TEST_PLAN_TASK_VERDICT="pass" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-fail:
	@HYDRA_TEST_PLAN_TASK_VERDICT="fail" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-inconclusive:
	@HYDRA_TEST_PLAN_TASK_VERDICT="inconclusive" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-stale-subject:
	@HYDRA_TEST_PLAN_TASK_VERDICT="stale-subject" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-stale-validator:
	@HYDRA_TEST_PLAN_TASK_VERDICT="stale-validator" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-missing-coverage:
	@HYDRA_TEST_PLAN_TASK_VERDICT="missing-coverage" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-crash:
	@HYDRA_TEST_PLAN_TASK_VERDICT="crash" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-bad-artifact:
	@HYDRA_TEST_PLAN_TASK_VERDICT="bad-artifact" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-changed-harness:
	@HYDRA_TEST_PLAN_TASK_VERDICT="changed-harness" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-pass-plan-repair-budget-1:
	@HYDRA_TEST_PLAN_REPAIR=pass HYDRA_TEST_PLAN_REPAIR_BUDGET=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-pass:
	@HYDRA_TEST_PLAN_REPAIR="pass" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-exhaust:
	@HYDRA_TEST_PLAN_REPAIR="exhaust" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-same:
	@HYDRA_TEST_PLAN_REPAIR="same" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-crash:
	@HYDRA_TEST_PLAN_REPAIR="crash" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-combine:
	@HYDRA_TEST_PLAN_REPAIR="combine" BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-plan-repair-pass-plan-source-1-plan-repair-fault-1:
	@HYDRA_TEST_PLAN_REPAIR=pass HYDRA_TEST_PLAN_SOURCE=1 HYDRA_TEST_PLAN_REPAIR_FAULT=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan_task.sh

fleet-case-report-v2-1:
	@HYDRA_TEST_REPORT_V2=1 BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_plan.sh

fleet-case-shell-workflow-approval:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_workflow_approval.sh

fleet-case-native-fleet:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-fleet

fleet-case-native-task-package:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-task-package

fleet-case-shell-fleet:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_fleet.sh

fleet-case-shell-fleet-install:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_fleet_install.sh

fleet-case-shell-task-package:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_task_package.sh

fleet-case-shell-task-acceptance:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_task_acceptance.sh

fleet-case-discovery:
	+@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" $(MAKE) test-discovery

fleet-case-enrollment:
	+@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" $(MAKE) test-enrollment

fleet-case-retention:
	+@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" $(MAKE) test-retention

fleet-case-workflow-metrics:
	+@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" $(MAKE) test-workflow-metrics

fleet-case-plan-staged:
	+@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" $(MAKE) test-plan-staged

fleet-case-native-remote-setup:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-remote-setup

fleet-case-shell-remote-setup:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_remote_setup.sh

fleet-case-native-fleet-assets:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-fleet-assets

fleet-case-shell-remote-provision:
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" tests/test_remote_provision.sh
