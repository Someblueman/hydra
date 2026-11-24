# Native independent PTY observers and fixtures.
$(BUILD_DIR)/fixture-lock: tests/fixture/lock.c
	@mkdir -p "$(@D)"
	$(CC) -std=c99 -Wall -Wextra $(WERROR) -pedantic $< -o $@

# Support shared by many cases is compiled once into $(BUILD_DIR)/test-support
# and linked as objects; each case still compiles its own source.
PTY_SUPPORT = tests/kettle/pty_support.c tests/kettle/screen_support.c tests/kettle/fixture_support.c
PTY_SUPPORT_OBJECTS = $(patsubst tests/kettle/%.c,$(BUILD_DIR)/test-support/kettle/%.o,$(PTY_SUPPORT))
PTY_HYDRA_FIXTURE = $(BUILD_DIR)/test-support/kettle/hydra_fixture.o
$(BUILD_DIR)/test-support/kettle/%.o: tests/kettle/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@
-include $(PTY_SUPPORT_OBJECTS:.o=.d) $(PTY_HYDRA_FIXTURE:.o=.d)
PTY_NAMES = pty program-pty statistics-pty fleet-controls fleet-recovery attached-pty attach-cycles output-fidelity terminal-restore interactive-loop plan-workspace plan-launch plan-conversation plan-assets workflow-controls runs-heads first-entry head-removal sign-in-recovery remote-setup responsive-reads refresh-cost onboarding
PTY_BINS = $(addprefix $(BUILD_DIR)/native-tests/pty-,$(PTY_NAMES))
$(BUILD_DIR)/native-tests:
	mkdir -p $@
$(foreach n,$(PTY_NAMES),$(eval $(BUILD_DIR)/native-tests/pty-$(n): tests/kettle/test_$(subst -,_,$(n)).c))
$(addprefix $(BUILD_DIR)/native-tests/pty-,attached-pty attach-cycles output-fidelity terminal-restore interactive-loop plan-workspace plan-launch plan-conversation plan-assets workflow-controls runs-heads first-entry head-removal sign-in-recovery): $(PTY_HYDRA_FIXTURE)
$(BUILD_DIR)/native-tests/pty-attached-pty: src/tui/fleet_budget.h
$(BUILD_DIR)/native-tests/pty-fleet-recovery: tests/kettle/fleet_recovery_support.c
$(addprefix $(BUILD_DIR)/native-tests/pty-,plan-workspace plan-launch plan-conversation plan-assets fleet-recovery): PTY_JSON_CFLAGS = $(FLEET_JSON_CFLAGS)
$(addprefix $(BUILD_DIR)/native-tests/pty-,plan-workspace plan-launch plan-conversation plan-assets fleet-recovery): PTY_JSON_LIB = $(FLEET_JSON_LIB)
$(PTY_BINS): $(PTY_SUPPORT_OBJECTS) $(wildcard tests/kettle/*.h) | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $(PTY_JSON_CFLAGS) $(filter %.c,$^) $(filter %.o,$^) $(PTY_JSON_LIB) -o $@
test-workspace-pty: $(BUILD_DIR)/native-tests/pty-pty
test-program-pty: $(BUILD_DIR)/native-tests/pty-program-pty
test-statistics: $(BUILD_DIR)/native-tests/pty-statistics-pty
test-fleet-controls: $(BUILD_DIR)/native-tests/pty-fleet-controls
test-fleet-recovery: $(BUILD_DIR)/native-tests/pty-fleet-recovery
test-attached-pty: $(BUILD_DIR)/native-tests/pty-attached-pty
test-attached-pty: $(BUILD_DIR)/native-tests/pty-attach-cycles
test-attached-pty: $(BUILD_DIR)/native-tests/pty-output-fidelity
test-attached-pty: $(BUILD_DIR)/native-tests/pty-terminal-restore
test-attached-pty: $(BUILD_DIR)/native-tests/pty-interactive-loop
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-plan-workspace
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-plan-launch
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-plan-conversation
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-plan-assets
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-workflow-controls
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-runs-heads
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-first-entry
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-head-removal
test-plan-workspace: $(BUILD_DIR)/native-tests/pty-sign-in-recovery
test-tui-pty: $(BUILD_DIR)/native-tests/pty-remote-setup
test-tui-pty: $(BUILD_DIR)/native-tests/pty-responsive-reads
test-tui-pty: $(BUILD_DIR)/native-tests/pty-refresh-cost
test-tui-pty: $(BUILD_DIR)/native-tests/pty-onboarding

NATIVE_TEST_NAMES = statistics-export task-announce retention workflow-task-metrics workflow-task-cohorts plan-inspection plan-evaluate plan-reuse-invalidation retention-accepted discovery enrollment enrollment-receiver enrollment-ssh plan-patterns plan-manifest plan-staged research-outcome performance-outcome plan-staged-public remote-setup-ssh
NATIVE_TEST_BINS = $(addprefix $(BUILD_DIR)/native-tests/test-,$(NATIVE_TEST_NAMES)) $(BUILD_DIR)/native-tests/workflow-contract-cases
NATIVE_SUPPORT_OBJECTS = $(addprefix $(BUILD_DIR)/test-support/native/,support.o accepted_fixture.o outcome_support.o)
$(BUILD_DIR)/test-support/native/%.o: tests/native/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) -MMD -MP -c $< -o $@
-include $(NATIVE_SUPPORT_OBJECTS:.o=.d)
$(foreach n,$(NATIVE_TEST_NAMES),$(eval $(BUILD_DIR)/native-tests/test-$(n): tests/native/test_$(subst -,_,$(n)).c))
$(addprefix $(BUILD_DIR)/native-tests/test-,plan-reuse-invalidation retention-accepted): $(BUILD_DIR)/test-support/native/accepted_fixture.o
$(BUILD_DIR)/native-tests/workflow-contract-cases: tests/native/workflow_contract_cases.c tests/native/contracts_data.c tests/native/contracts_plan.c tests/native/contracts_runtime.c
$(NATIVE_TEST_BINS): $(BUILD_DIR)/test-support/native/support.o $(wildcard tests/native/*.h) $(BUILD_DIR)/libhydra-fleet.a | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(filter %.c,$^) $(filter %.o,$^) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@
test-statistics-export: $(BUILD_DIR)/native-tests/test-statistics-export
test-workflow-contracts: $(BUILD_DIR)/native-tests/workflow-contract-cases
test-task-announce: $(BUILD_DIR)/native-tests/test-task-announce
test-retention: $(BUILD_DIR)/native-tests/test-retention
test-workflow-metrics: $(BUILD_DIR)/native-tests/test-workflow-task-metrics
test-workflow-metrics: $(BUILD_DIR)/native-tests/test-workflow-task-cohorts
test-workflow-metrics: $(BUILD_DIR)/native-tests/test-statistics-export
test-plan-inspection: $(BUILD_DIR)/native-tests/test-plan-inspection $(BUILD_DIR)/native-tests/test-plan-evaluate
test-plan-reuse: $(BUILD_DIR)/native-tests/test-plan-reuse-invalidation $(BUILD_DIR)/native-tests/test-retention-accepted

$(BUILD_DIR)/native-tests/test-discovery: tests/native/discovery_progress.c tests/native/discovery_live.c
$(BUILD_DIR)/native-tests/test-enrollment: tests/native/enrollment_batch.c tests/native/enrollment_packages.c tests/native/enrollment_revocation.c
NATIVE_FIXTURES = $(addprefix $(BUILD_DIR)/native-tests/,discovery-ssh-fixture enrollment-ssh-fixture enrollment-loopback-fixture)
$(BUILD_DIR)/native-tests/discovery-ssh-fixture: tests/native/discovery_ssh_fixture.c
$(BUILD_DIR)/native-tests/enrollment-ssh-fixture: tests/native/enrollment_ssh_fixture.c
$(NATIVE_FIXTURES): $(BUILD_DIR)/test-support/native/support.o tests/native/support.h $(BUILD_DIR)/libhydra-fleet.a | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(filter %.c,$^) $(filter %.o,$^) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@
$(BUILD_DIR)/native-tests/enrollment-receiver-fixture: tests/native/enrollment_receiver_fixture.c | $(BUILD_DIR)/native-tests
	$(CC) $(CORE_CFLAGS) $< -o $@
test-discovery: $(BUILD_DIR)/native-tests/test-discovery $(BUILD_DIR)/native-tests/discovery-ssh-fixture
test-enrollment: $(BUILD_DIR)/native-tests/test-enrollment $(BUILD_DIR)/native-tests/test-enrollment-receiver $(BUILD_DIR)/native-tests/enrollment-ssh-fixture $(BUILD_DIR)/native-tests/enrollment-receiver-fixture

$(BUILD_DIR)/native-tests/enrollment-loopback-fixture: tests/native/enrollment_loopback_fixture.c
test-enrollment-ssh: $(BUILD_DIR)/native-tests/test-enrollment-ssh $(BUILD_DIR)/native-tests/enrollment-loopback-fixture $(BUILD_DIR)/native-tests/enrollment-receiver-fixture
test-remote-setup-ssh: $(BUILD_DIR)/native-tests/test-remote-setup-ssh

OUTCOME_TEST_BINS = $(addprefix $(BUILD_DIR)/native-tests/test-,plan-patterns plan-manifest plan-staged research-outcome performance-outcome plan-staged-public)
$(OUTCOME_TEST_BINS): $(BUILD_DIR)/test-support/native/outcome_support.o tests/native/outcome_support.h
test-plan-outcomes: build-plan-example $(addprefix $(BUILD_DIR)/native-tests/test-,plan-patterns performance-outcome research-outcome plan-manifest)
test-plan-staged: build-plan-example $(BUILD_DIR)/native-tests/test-plan-staged
test-plan-staged-public: build-plan-example $(BUILD_DIR)/native-tests/test-plan-staged-public
$(BUILD_DIR)/native-tests/test-performance-outcome: tests/native/performance_fixture.c
