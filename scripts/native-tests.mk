# Native independent PTY observers and fixtures.
$(BUILD_DIR)/fixture-lock: tests/fixture/lock.c
	@mkdir -p "$(@D)"
	$(CC) -std=c99 -Wall -Wextra -Werror -pedantic $< -o $@

PTY_SUPPORT = tests/termviz/pty_support.c tests/termviz/screen_support.c tests/termviz/fixture_support.c
PTY_NAMES = pty statistics-pty fleet-controls fleet-recovery attached-pty plan-workspace plan-launch workflow-controls
PTY_BINS = $(addprefix $(BUILD_DIR)/native-tests/pty-,$(PTY_NAMES))
$(BUILD_DIR)/native-tests:
	mkdir -p $@
$(foreach n,$(PTY_NAMES),$(eval $(BUILD_DIR)/native-tests/pty-$(n): tests/termviz/test_$(subst -,_,$(n)).c))
$(addprefix $(BUILD_DIR)/native-tests/pty-,attached-pty plan-workspace plan-launch workflow-controls): tests/termviz/hydra_fixture.c
$(BUILD_DIR)/native-tests/pty-fleet-recovery: tests/termviz/fleet_recovery_support.c
$(addprefix $(BUILD_DIR)/native-tests/pty-,plan-workspace plan-launch fleet-recovery): PTY_JSON_CFLAGS = $(FLEET_JSON_CFLAGS)
$(addprefix $(BUILD_DIR)/native-tests/pty-,plan-workspace plan-launch fleet-recovery): PTY_JSON_LIB = $(FLEET_JSON_LIB)
$(PTY_BINS): $(PTY_SUPPORT) $(wildcard tests/termviz/*.h) | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $(PTY_JSON_CFLAGS) $(filter %.c,$^) $(PTY_JSON_LIB) -o $@
test-workspace-pty: $(BUILD_DIR)/native-tests/pty-pty
test-statistics: $(BUILD_DIR)/native-tests/pty-statistics-pty
test-fleet-controls: $(BUILD_DIR)/native-tests/pty-fleet-controls
test-fleet-recovery: $(BUILD_DIR)/native-tests/pty-fleet-recovery
test-attached-pty: $(BUILD_DIR)/native-tests/pty-attached-pty
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-plan-workspace
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-plan-launch
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-workflow-controls

NATIVE_TEST_NAMES = statistics-export task-announce retention workflow-task-metrics plan-inspection plan-reuse-invalidation retention-accepted discovery enrollment enrollment-receiver enrollment-ssh plan-patterns plan-manifest plan-staged research-outcome performance-outcome plan-staged-public
NATIVE_TEST_BINS = $(addprefix $(BUILD_DIR)/native-tests/test-,$(NATIVE_TEST_NAMES)) $(BUILD_DIR)/native-tests/workflow-contract-cases
$(foreach n,$(NATIVE_TEST_NAMES),$(eval $(BUILD_DIR)/native-tests/test-$(n): tests/native/test_$(subst -,_,$(n)).c))
$(addprefix $(BUILD_DIR)/native-tests/test-,plan-reuse-invalidation retention-accepted): tests/native/accepted_fixture.c
$(BUILD_DIR)/native-tests/workflow-contract-cases: tests/native/workflow_contract_cases.c tests/native/contracts_data.c tests/native/contracts_plan.c tests/native/contracts_runtime.c
$(NATIVE_TEST_BINS): tests/native/support.c $(wildcard tests/native/*.h) $(BUILD_DIR)/libhydra-fleet.a | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(filter %.c,$^) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@
test-statistics-export: $(BUILD_DIR)/native-tests/test-statistics-export
test-workflow-contracts: $(BUILD_DIR)/native-tests/workflow-contract-cases
test-task-announce: $(BUILD_DIR)/native-tests/test-task-announce
test-retention: $(BUILD_DIR)/native-tests/test-retention
test-workflow-metrics: $(BUILD_DIR)/native-tests/test-workflow-task-metrics
test-workflow-metrics: $(BUILD_DIR)/native-tests/test-statistics-export
test-plan-inspection: $(BUILD_DIR)/native-tests/test-plan-inspection

$(BUILD_DIR)/native-tests/test-discovery: tests/native/discovery_progress.c
$(BUILD_DIR)/native-tests/test-enrollment: tests/native/enrollment_batch.c tests/native/enrollment_packages.c
NATIVE_FIXTURES = $(addprefix $(BUILD_DIR)/native-tests/,discovery-ssh-fixture enrollment-ssh-fixture enrollment-loopback-fixture)
$(BUILD_DIR)/native-tests/discovery-ssh-fixture: tests/native/discovery_ssh_fixture.c
$(BUILD_DIR)/native-tests/enrollment-ssh-fixture: tests/native/enrollment_ssh_fixture.c
$(NATIVE_FIXTURES): tests/native/support.c tests/native/support.h $(BUILD_DIR)/libhydra-fleet.a | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(filter %.c,$^) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@
$(BUILD_DIR)/native-tests/enrollment-receiver-fixture: tests/native/enrollment_receiver_fixture.c | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $< -o $@
test-discovery: $(BUILD_DIR)/native-tests/test-discovery $(BUILD_DIR)/native-tests/discovery-ssh-fixture
test-enrollment: $(BUILD_DIR)/native-tests/test-enrollment $(BUILD_DIR)/native-tests/test-enrollment-receiver $(BUILD_DIR)/native-tests/enrollment-ssh-fixture $(BUILD_DIR)/native-tests/enrollment-receiver-fixture

$(BUILD_DIR)/native-tests/enrollment-loopback-fixture: tests/native/enrollment_loopback_fixture.c
test-enrollment-ssh: $(BUILD_DIR)/native-tests/test-enrollment-ssh $(BUILD_DIR)/native-tests/enrollment-loopback-fixture $(BUILD_DIR)/native-tests/enrollment-receiver-fixture

OUTCOME_TEST_BINS = $(addprefix $(BUILD_DIR)/native-tests/test-,plan-patterns plan-manifest plan-staged research-outcome performance-outcome plan-staged-public)
$(OUTCOME_TEST_BINS): tests/native/outcome_support.c tests/native/outcome_support.h
test-plan-outcomes: build-plan-example $(addprefix $(BUILD_DIR)/native-tests/test-,plan-patterns performance-outcome research-outcome plan-manifest)
test-plan-staged: build-plan-example $(BUILD_DIR)/native-tests/test-plan-staged
test-plan-staged-public: build-plan-example $(BUILD_DIR)/native-tests/test-plan-staged-public
$(BUILD_DIR)/native-tests/test-performance-outcome: tests/native/performance_fixture.c
