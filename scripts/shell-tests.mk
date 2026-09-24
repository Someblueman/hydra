# Independent shell acceptance cases. Each case receives a private tmux
# socket so fixed session names cannot collide across parallel workers.
SHELL_TEST_EXCLUDES = \
	tests/test_core.sh \
	tests/test_visualization.sh \
	tests/test_native_install.sh \
	tests/test_native_tui.sh \
	tests/test_fleet.sh \
	tests/test_fleet_install.sh \
	tests/test_task_package.sh \
	tests/test_task_acceptance.sh \
	tests/test_workflow_plan_task.sh \
	tests/test_workflow_plan_v3.sh \
	tests/test_headless_plan_adapter.sh \
	tests/test_workflow_task.sh \
	tests/test_workflow_data.sh \
	tests/test_workflow_plan.sh \
	tests/test_workflow_approval.sh \
	tests/test_agent_execution.sh \
	tests/test_agent_auth.sh

SHELL_TEST_FILES = $(filter-out $(SHELL_TEST_EXCLUDES),$(wildcard tests/test_*.sh))
SHELL_TEST_NAMES = $(patsubst tests/test_%.sh,%,$(SHELL_TEST_FILES))
# Dashboard uses a fixed throwaway path and is intentionally serialized after
# the private-socket cases. It remains part of the default shell suite.
SHELL_TEST_PARALLEL_NAMES = $(filter-out dashboard,$(SHELL_TEST_NAMES))
SHELL_TEST_SERIAL_NAMES = $(filter dashboard,$(SHELL_TEST_NAMES))

.PHONY: shell-tests shell-tests-parallel $(addprefix shell-case-,$(SHELL_TEST_NAMES))
shell-tests: shell-tests-parallel $(addprefix shell-case-,$(SHELL_TEST_SERIAL_NAMES))

shell-tests-parallel: $(addprefix shell-case-,$(SHELL_TEST_PARALLEL_NAMES))

$(addprefix shell-case-,$(SHELL_TEST_SERIAL_NAMES)): shell-tests-parallel

$(BUILD_DIR)/test-shell-exec: tests/c/test_shell_exec.c | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) $< -o $@

$(addprefix shell-case-,$(SHELL_TEST_NAMES)): shell-case-%: $(BUILD_DIR)/test-shell-exec
	@HYDRA_SHELL_EXEC="$(abspath $(BUILD_DIR))/test-shell-exec" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" "tests/test_$*.sh"
