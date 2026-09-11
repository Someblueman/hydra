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
	native-task-package

# These are also used by the shell cases; none may compile concurrently with a case.
FLEET_NATIVE_CASE_BINS = $(addprefix $(BUILD_DIR)/native-tests/,test-discovery test-enrollment test-enrollment-receiver test-retention test-workflow-task-metrics test-statistics-export test-plan-staged workflow-contract-cases discovery-ssh-fixture enrollment-ssh-fixture enrollment-receiver-fixture statistics-evidence)
.PHONY: fleet-test-build
fleet-test-build: build-fleet build-core build-test-fixture build-plan-example build-plan-precompile $(FLEET_TEST_BINS) $(FLEET_NATIVE_CASE_BINS) $(BUILD_DIR)/test-statistics $(BUILD_DIR)/fixture-lock

test-fleet: fleet-test-build
	+$(MAKE) -j$(TEST_JOBS) test-fleet-cases

.PHONY: test-fleet-cases $(addprefix fleet-case-,$(FLEET_CASES))
test-fleet-cases: $(addprefix fleet-case-,$(FLEET_CASES))

fleet-case-native-plan:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-plan

fleet-case-native-workflow-schedule:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-workflow-schedule

fleet-case-native-agent-auth:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-agent-auth

fleet-case-native-agent-profile:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-agent-profile

fleet-case-shell-agent-auth:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_agent_auth.sh

fleet-case-shell-agent-execution:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_agent_execution.sh

fleet-case-native-workflow-data:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-workflow-data

fleet-case-shell-workflow-data:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_data.sh

fleet-case-native-workflow-contract-cases:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/workflow-contract-cases" --runtime

fleet-case-dag-replay-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_REPLAY=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-result-lost-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_RESULT_LOST=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-result-bad-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_RESULT_BAD=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-parallelism-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_PARALLELISM=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-crash-2:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_CRASH=2 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-source-1-dag-source-tamper-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_SOURCE=1 HYDRA_TEST_DAG_SOURCE_TAMPER=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-plan-source-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_SOURCE=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-source-dirty:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_SOURCE=dirty HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-dag-lost-ack-1-dag-fault-dispatch:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="dispatch" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-key:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="key" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-attempt:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="attempt" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-placement:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="placement" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-cancel:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="cancel" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-dag-lost-ack-1-dag-fault-cancel-offline:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT="cancel-offline" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_task.sh

fleet-case-headless-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_HEADLESS=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan.sh

fleet-case-shell-headless-plan-adapter:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_headless_plan_adapter.sh

fleet-case-shell-workflow-plan:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan.sh

fleet-case-shell-workflow-plan-v3:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_v3.sh

fleet-case-plan-task-verdict-pass:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="pass" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-fail:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="fail" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-inconclusive:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="inconclusive" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-stale-subject:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="stale-subject" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-stale-validator:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="stale-validator" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-missing-coverage:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="missing-coverage" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-crash:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="crash" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-bad-artifact:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="bad-artifact" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-task-verdict-changed-harness:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_TASK_VERDICT="changed-harness" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-pass-plan-repair-budget-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR=pass HYDRA_TEST_PLAN_REPAIR_BUDGET=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-pass:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR="pass" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-exhaust:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR="exhaust" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-same:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR="same" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-crash:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR="crash" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-combine:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR="combine" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-plan-repair-pass-plan-source-1-plan-repair-fault-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_PLAN_REPAIR=pass HYDRA_TEST_PLAN_SOURCE=1 HYDRA_TEST_PLAN_REPAIR_FAULT=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

fleet-case-report-v2-1:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_TEST_REPORT_V2=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan.sh

fleet-case-shell-workflow-approval:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_approval.sh

fleet-case-native-fleet:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-fleet

fleet-case-native-task-package:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env $(BUILD_DIR)/test-task-package

fleet-case-shell-fleet:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_fleet.sh

fleet-case-shell-fleet-install:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_fleet_install.sh

fleet-case-shell-task-package:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_task_package.sh

fleet-case-shell-task-acceptance:
	@sh scripts/run-test.sh "$(BUILD_DIR)/test-logs" "$@" env HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_task_acceptance.sh

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

