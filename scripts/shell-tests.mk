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
	tests/test_agent_auth.sh \
	tests/test_agent_locate.sh \
	tests/test_remote_agents.sh \
	tests/test_remote_setup.sh \
	tests/test_remote_provision.sh \
	tests/test_bench_i1.sh

SHELL_TEST_FILES = $(filter-out $(SHELL_TEST_EXCLUDES),$(wildcard tests/test_*.sh))
SHELL_TEST_NAMES = $(patsubst tests/test_%.sh,%,$(SHELL_TEST_FILES))
# Make starts prerequisites in order, so list the slowest cases first (as
# FLEET_CASES does) and let the short cases fill the remaining workers.
# Keep roughly ordered by measured duration; unlisted cases follow.
SHELL_TEST_SLOW = kill fleet_review workflow_run_heads workflow_runtime \
	admission_heads group_messaging plan_conversation workflow_result_review \
	plan_proposal spawn_ux workflow_review_plan workflow_review operations \
	kill_all workflow_e2e verified_integration spawn_bootstrap merge_train \
	orphan_worktrees admission_execution integration_safety parallel \
	fixture_cleanup workflow_attention lifecycle workflow_review_bindings \
	json_output init_source_tree dashboard admission
SHELL_TEST_ORDER = $(filter $(SHELL_TEST_NAMES),$(SHELL_TEST_SLOW)) \
	$(filter-out $(SHELL_TEST_SLOW),$(SHELL_TEST_NAMES))

.PHONY: shell-tests $(addprefix shell-case-,$(SHELL_TEST_NAMES))
shell-tests: $(addprefix shell-case-,$(SHELL_TEST_ORDER))

$(BUILD_DIR)/test-shell-exec: tests/c/test_shell_exec.c | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) $< -o $@

$(addprefix shell-case-,$(SHELL_TEST_NAMES)): shell-case-%: $(BUILD_DIR)/test-shell-exec
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "$@" "tests/test_$*.sh"
