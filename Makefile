# Makefile for Hydra
# POSIX-compliant build and lint tasks

.PHONY: all lint test test-fast test-all clean install uninstall test-install test-native-install smoke-onboarding \
	dev-setup bench bench-tui build-core build-tui test-c test-tui test-tui-pty test-parity sanitize-core sanitize-tui sanitizer \
	sanitize bench-core benchmark-core package-core package-tui help

CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CORE_CFLAGS = $(CFLAGS) -std=c99 -Wall -Wextra -Werror -pedantic -Isrc
BUILD_DIR ?= build
# Build and analysis share recursive discovery so domain directories cannot
# silently omit native sources from the quality gate.
NATIVE_SOURCES = $(shell find src -type f -name '*.c' | LC_ALL=C sort)
SANITIZER_FLAGS ?= $(shell if [ "$$(uname -s)" = Darwin ]; then printf '%s' '-fsanitize=undefined'; else printf '%s' '-fsanitize=address,undefined'; fi)

# Sanitizer diagnostics must fail a case even when its output is captured.
export UBSAN_OPTIONS ?= halt_on_error=1

# Installation prefix (no root required when writable)
PREFIX ?= /usr/local
DESTDIR ?=

# Default target
all: lint

# Lint all shell scripts for POSIX compliance
lint:
	@sh scripts/lint-shell.sh
	@echo "All checks passed!"

# Run CLI shell tests with their native structured-data fixture helpers.
test: build-fleet build-test-fixture $(BUILD_DIR)/native-tests/statistics-evidence $(BUILD_DIR)/test-statistics
	@echo "Running tests..."
	@if [ -d tests ] && [ -n "$$(ls -A tests/test_*.sh 2>/dev/null)" ]; then \
		for test in tests/test_*.sh; do \
			case "$$test" in tests/test_core.sh|tests/test_visualization.sh|tests/test_native_install.sh|tests/test_native_tui.sh|tests/test_fleet.sh|tests/test_fleet_install.sh|tests/test_task_package.sh|tests/test_task_acceptance.sh|tests/test_workflow_plan_task.sh|tests/test_workflow_plan_v3.sh|tests/test_headless_plan_adapter.sh|tests/test_workflow_task.sh|tests/test_workflow_data.sh|tests/test_workflow_plan.sh|tests/test_workflow_approval.sh|tests/test_agent_execution.sh|tests/test_agent_auth.sh) continue ;; esac; \
			echo "Running $$test..."; \
			sh "$$test" || exit 1; \
		done; \
	else \
		echo "No tests found in tests/"; \
	fi

# Fixed PR feedback lane. Keep the shell and native selection explicit so this
# target remains useful for every change and does not depend on changed-file
# heuristics. Shared prerequisites are built before any stateful test starts.
FAST_SHELL_TESTS = foundation paths state json_output git_simple project_trust deps tmux lifecycle
FAST_NATIVE_BINS = $(filter-out $(BUILD_DIR)/test-task-result,$(FLEET_TEST_BINS))
.PHONY: test-fast
test-fast:
	+@$(MAKE) -j$(TEST_JOBS) build-fleet build-test-fixture build-core build-tui $(BUILD_DIR)/test-statistics $(FAST_NATIVE_BINS)
	@$(MAKE) lint
	@echo "Running fast shell tests..."
	@for name in $(FAST_SHELL_TESTS); do \
		test="tests/test_$${name}.sh"; \
		echo "Running $$test..."; \
		sh "$$test" || exit 1; \
	done
	@$(MAKE) -j1 test-c test-tui test-parity test-termviz
	@$(BUILD_DIR)/test-statistics
	@echo "Running fast native fleet unit tests..."
	@for binary in $(FAST_NATIVE_BINS); do \
		echo "Running $$binary..."; \
		"$$binary" || exit 1; \
	done

# Optional read-only native helper. The shell CLI remains the mutation authority.
build-core: $(BUILD_DIR)/hydra-core

build-tui: $(BUILD_DIR)/hydra-tui

.PHONY: test-termviz example-termviz example-workspace test-workspace-pty test-visualization sanitize-workspace
test-visualization: build-tui test-termviz $(BUILD_DIR)/test-statistics
	sh tests/test_visualization.sh
$(BUILD_DIR)/test-termviz: tests/c/test_termviz.c $(TERMVIZ_SOURCES) src/termviz/termviz.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_termviz.c $(TERMVIZ_SOURCES) -o $@

$(BUILD_DIR)/test-termviz-present: tests/c/test_termviz_present.c $(TERMVIZ_SOURCES) src/termviz/termviz.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_termviz_present.c $(TERMVIZ_SOURCES) -o $@

$(BUILD_DIR)/test-termviz-workspace: tests/c/test_termviz_workspace.c src/termviz/workspace.c src/termviz/workspace.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_termviz_workspace.c src/termviz/workspace.c -o $@

$(BUILD_DIR)/test-termviz-unicode: tests/c/test_termviz_unicode.c $(TERMVIZ_SOURCES) src/termviz/termviz.h src/termviz/unicode_tables.inc | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_termviz_unicode.c $(TERMVIZ_SOURCES) -o $@

$(BUILD_DIR)/test-termviz-terminal: tests/c/test_termviz_terminal.c $(TERMVIZ_SOURCES) src/termviz/terminal.h src/termviz/terminal_internal.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_termviz_terminal.c $(TERMVIZ_SOURCES) -o $@

$(BUILD_DIR)/test-termviz-input: tests/c/test_termviz_input.c $(TERMVIZ_SOURCES) src/termviz/input.h src/termviz/tree.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_termviz_input.c $(TERMVIZ_SOURCES) -o $@

test-termviz: $(BUILD_DIR)/test-termviz $(BUILD_DIR)/test-termviz-present $(BUILD_DIR)/test-termviz-workspace $(BUILD_DIR)/test-termviz-unicode $(BUILD_DIR)/test-termviz-terminal $(BUILD_DIR)/test-termviz-input
	$(BUILD_DIR)/test-termviz
	$(BUILD_DIR)/test-termviz-present
	$(BUILD_DIR)/test-termviz-workspace
	$(BUILD_DIR)/test-termviz-unicode
	$(BUILD_DIR)/test-termviz-terminal
	$(BUILD_DIR)/test-termviz-input

$(BUILD_DIR)/termviz-example: examples/termviz.c $(TERMVIZ_SOURCES) src/termviz/termviz.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -Isrc/termviz examples/termviz.c $(TERMVIZ_SOURCES) -o $@

$(BUILD_DIR)/termviz-workspace: examples/workspace.c examples/workspace_view.c examples/workspace_input.c $(TERMVIZ_SOURCES) src/termviz/workspace.h src/termviz/input.h src/termviz/terminal_posix.c src/termviz/posix.h src/termviz/pty_posix.c src/termviz/pty_posix.h examples/workspace_demo.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -Isrc/termviz examples/workspace.c examples/workspace_view.c examples/workspace_input.c $(TERMVIZ_SOURCES) -o $@

$(BUILD_DIR)/test-workspace-child: tests/c/test_workspace_child.c | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_workspace_child.c -o $@

test-workspace-pty: example-workspace build-tui $(BUILD_DIR)/test-workspace-child
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-pty"

$(BUILD_DIR)/test-statistics: tests/c/test_statistics.c src/hydra_statistics.c src/hydra_statistics_metrics.c src/hydra_statistics.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_statistics.c src/hydra_statistics.c src/hydra_statistics_metrics.c -o $@

.PHONY: test-statistics sanitize-statistics
test-statistics: $(BUILD_DIR)/test-statistics build-tui
	$(BUILD_DIR)/test-statistics
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-statistics-pty"

sanitize-statistics:
	@$(MAKE) BUILD_DIR=build/statistics-sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-statistics

example-workspace: $(BUILD_DIR)/termviz-workspace

example-termviz: $(BUILD_DIR)/termviz-example

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libhydra.o: src/libhydra.c src/libhydra.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -c src/libhydra.c -o $@

$(BUILD_DIR)/libhydra.a: $(BUILD_DIR)/libhydra.o
	$(AR) rcs $@ $<

CORE_STATS_SOURCES = src/hydra_statistics.c src/hydra_statistics_metrics.c src/hydra_statistics_export.c src/hydra_statistics_cli.c

$(BUILD_DIR)/hydra-core: src/hydra_core.c src/libhydra.h $(BUILD_DIR)/libhydra.a $(CORE_STATS_SOURCES) src/hydra_statistics.h
	$(CC) $(CORE_CFLAGS) src/hydra_core.c $(CORE_STATS_SOURCES) $(BUILD_DIR)/libhydra.a -o $@

$(BUILD_DIR)/test-statistics-export: tests/c/test_statistics_export.c src/hydra_statistics.c src/hydra_statistics_metrics.c src/hydra_statistics_export.c src/hydra_statistics.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_statistics_export.c src/hydra_statistics.c src/hydra_statistics_metrics.c src/hydra_statistics_export.c -o $@

.PHONY: test-statistics-export
test-statistics-export: build-core $(BUILD_DIR)/test-statistics-export
	$(BUILD_DIR)/test-statistics-export
	"$(BUILD_DIR)/native-tests/test-statistics-export" $(BUILD_DIR)/hydra-core

TUI_SOURCES = $(filter src/tui/%.c,$(NATIVE_SOURCES))
TUI_OBJECTS = $(patsubst src/tui/%.c,$(BUILD_DIR)/tui/%.o,$(TUI_SOURCES))

$(BUILD_DIR)/tui/%.o: src/tui/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

TERMVIZ_SOURCES = $(filter src/termviz/%.c,$(NATIVE_SOURCES))
TERMVIZ_HEADERS = $(wildcard src/termviz/*.h src/termviz/*.inc)
TERMVIZ_OBJECTS = $(patsubst src/termviz/%.c,$(BUILD_DIR)/termviz/%.o,$(TERMVIZ_SOURCES))
TUI_DATA_OBJECTS = $(BUILD_DIR)/hydra_statistics.o $(BUILD_DIR)/hydra_statistics_metrics.o

$(BUILD_DIR)/termviz/%.o: src/termviz/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

$(TUI_DATA_OBJECTS): $(BUILD_DIR)/%.o: src/%.c src/hydra_statistics.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/hydra-tui: $(TUI_OBJECTS) $(TERMVIZ_OBJECTS) $(TUI_DATA_OBJECTS)
	$(CC) $(CORE_CFLAGS) $^ -o $@

-include $(TUI_OBJECTS:.o=.d) $(TERMVIZ_OBJECTS:.o=.d) $(TUI_DATA_OBJECTS:.o=.d)

$(BUILD_DIR)/test-libhydra: tests/c/test_libhydra.c src/libhydra.h $(BUILD_DIR)/libhydra.a
	$(CC) $(CORE_CFLAGS) tests/c/test_libhydra.c $(BUILD_DIR)/libhydra.a -o $@

$(BUILD_DIR)/test-tui-pty: tests/c/test_tui_pty.c tests/c/test_tui_mouse.inc tests/c/test_tui_themes.inc tests/c/test_tui_palette.inc tests/c/test_tui_visualization.inc tests/c/test_tui_attention.inc tests/c/test_tui_review.inc | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_tui_pty.c -o $@

test-c: $(BUILD_DIR)/test-libhydra
	$(BUILD_DIR)/test-libhydra

$(BUILD_DIR)/test-tui-input: tests/c/test_tui_input.c src/tui/input.c $(TERMVIZ_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o $(BUILD_DIR)/tui/input.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

test-tui: build-tui $(BUILD_DIR)/test-tui-input
	$(BUILD_DIR)/test-tui-input
	@HYDRA_TUI_BIN="$(abspath $(BUILD_DIR))/hydra-tui" sh tests/test_native_tui.sh

test-tui-pty: build-tui $(BUILD_DIR)/test-tui-pty
	@mkdir -p "$(abspath $(BUILD_DIR))/test-tui-home"
	@: > "$(abspath $(BUILD_DIR))/test-tui-home/sentinel"
	@before="$$(find "$(abspath $(BUILD_DIR))/test-tui-home" -type f -exec cksum {} \; | sort | cksum)"; \
	HYDRA_HOME="$(abspath $(BUILD_DIR))/test-tui-home" \
		HYDRA_TEST_BIN="$(CURDIR)/bin/hydra" \
		HYDRA_TUI_BIN="$(CURDIR)/tests/fixtures/tui/crash-native.sh" \
		HYDRA_TEST_CRASH_DISPATCH="$(CURDIR)/tests/fixtures/tui/crash-dispatch.sh" \
		HYDRA_TEST_SLOW_HYDRA="$(CURDIR)/tests/fixtures/tui/slow-hydra.sh" \
		HYDRA_TEST_EOF_HYDRA="$(CURDIR)/tests/fixtures/tui/eof-hydra.sh" \
		$(BUILD_DIR)/test-tui-pty "$(abspath $(BUILD_DIR))/hydra-tui" \
		"$(CURDIR)/tests/fixtures/tui/fake-hydra.sh" "$(CURDIR)/tests/fixtures/tui/fake-bin"; \
	status=$$?; \
	after="$$(find "$(abspath $(BUILD_DIR))/test-tui-home" -type f -exec cksum {} \; | sort | cksum)"; \
	if [ "$$before" != "$$after" ]; then echo "native crash path changed Hydra state" >&2; exit 1; fi; \
	exit $$status

test-parity: build-core
	@HYDRA_CORE="$(abspath $(BUILD_DIR))/hydra-core" sh tests/test_core.sh

test-all: test-fleet-controls test-fleet-recovery test-plan-inspection test-plan-outcomes test-statistics-export test-task-announce test-plan-reuse test-plan-workspace test-attached-pty test-termviz-export test-visualization test-workspace-pty test-statistics lint test test-fleet test-c test-tui test-tui-pty test-parity test-install test-native-install smoke-onboarding

sanitize-core:
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-c

sanitize-tui:
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" build-tui
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" build/sanitize/test-tui-input
	@build/sanitize/test-tui-input
	@build/sanitize/hydra-tui --headless-fixture tests/fixtures/tui/native-v2.tsv --size 80x24 --frames 2 >/dev/null

sanitizer: sanitize-core

sanitize: sanitize-core sanitize-tui sanitize-fleet sanitize-workspace sanitize-attached sanitize-plan-workspace sanitize-statistics

bench-core: build-core
	@sh scripts/bench-core.sh

benchmark-core: bench-core

bench-tui: build-tui $(BUILD_DIR)/test-tui-pty
	@sh scripts/bench-tui.sh

package-core: build-core
	@sh scripts/package-native.sh core

package-tui: build-tui
	@sh scripts/package-native.sh tui

# Record shell baseline timings (not a CI gate; no speedup claims)
bench:
	@sh scripts/bench.sh

# Clean temporary files
clean:
	@echo "Cleaning temporary files..."
	@find . -name "*~" -o -name "*.swp" -o -name ".*.swp" | xargs rm -f
	@rm -rf build
	@echo "Clean complete"

# Install hydra to $(PREFIX)/bin and $(PREFIX)/lib/hydra
# Same layout and verification as ./install.sh
install:
	PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" sh ./install.sh

# Remove files installed to $(PREFIX)
uninstall:
	PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" sh ./uninstall.sh

# Fresh-prefix install, verify, and uninstall
test-install:
	@HYDRA_INSTALL_CORE=never HYDRA_INSTALL_TUI=never sh tests/test_install.sh

test-native-install: build-core build-tui
	@sh tests/test_native_install.sh

# Throwaway-repository, no-agent first-head path
smoke-onboarding:
	@sh tests/test_onboarding.sh

# Set up development environment
dev-setup:
	@echo "Setting up development environment..."
	@if [ -f scripts/install-hooks.sh ]; then \
		sh scripts/install-hooks.sh; \
	else \
		echo "Warning: scripts/install-hooks.sh not found"; \
	fi
	@echo "Development environment setup complete"

# Display help
help:
	@echo "Hydra Makefile targets:"
	@echo "  make lint      - Run ShellCheck and dash syntax validation"
	@echo "  make test      - Run the shell-only test suite"
	@echo "  make test-fast - Run the fixed PR feedback test selection"
	@echo "  make build-core - Build the optional read-only native helper"
	@echo "  make build-tui - Build the optional native mission-control TUI"
	@echo "  make test-c    - Run native library unit tests"
	@echo "  make build-plan-precompile - Build native finite-example planner"
	@echo "  make test-tui  - Run deterministic native TUI acceptance"
	@echo "  make test-tui-pty - Run real pseudo-terminal safety and input acceptance"
	@echo "  make test-parity - Verify shell/native protocol parity and fallbacks"
	@echo "  make test-all  - Run shell/native, install, parity, and onboarding acceptance"
	@echo "  make sanitize  - Run supported native sanitizer tests"
	@echo "  make bench-core - Compare shell and native snapshot timings"
	@echo "  make bench-tui - Measure bounded native adapter/render at 5, 20, and 100 heads"
	@echo "  make bench     - Record list/status/doctor/TUI timings at 5 and 20 heads"
	@echo "  make clean     - Remove temporary files"
	@echo "  make install   - Install hydra to \$$PREFIX/bin (default /usr/local)"
	@echo "  make uninstall - Remove hydra from \$$PREFIX"
	@echo "  make test-install - Fresh-prefix install/uninstall tests"
	@echo "  make test-native-install - Offline native install, handshake, and rollback tests"
	@echo "  make package-core - Create a checksummed platform-qualified core artifact"
	@echo "  make package-tui - Create a checksummed platform-qualified native TUI artifact"
	@echo "  make smoke-onboarding - Throwaway-repo no-agent first-head smoke"
	@echo "  make dev-setup - Set up development environment (git hooks)"
	@echo "  make help      - Show this help message"

# Optional fleet coordinator; JSON-C is statically linked into this executable.
FLEET_SOURCES = $(filter src/fleet/%.c,$(NATIVE_SOURCES))
FLEET_JSON_CFLAGS = $(shell pkg-config --cflags json-c)
FLEET_JSON_LIB = $(shell pkg-config --variable=libdir json-c)/libjson-c.a

.PHONY: build-fleet test-fleet test-workflow-contracts
build-fleet: $(BUILD_DIR)/hydra-fleet

.PHONY: build-test-fixture
build-test-fixture: $(BUILD_DIR)/native-tests/fixture-json
FIXTURE_JSON_SOURCES = $(filter-out tests/fixture/lock.c,$(wildcard tests/fixture/*.c))
$(BUILD_DIR)/native-tests/fixture-json: $(FIXTURE_JSON_SOURCES) tests/fixture/fixture.h $(BUILD_DIR)/libhydra-fleet.a
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(FIXTURE_JSON_SOURCES) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@

$(BUILD_DIR)/native-tests/statistics-evidence: tests/native/statistics_evidence.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) $< -o $@

# Compile shared fleet code once. Compiler dependency files track the actual
# header/.inc closure for each object and test, including sanitizer builds.
FLEET_OBJECTS = $(patsubst src/fleet/%.c,$(BUILD_DIR)/fleet/%.o,$(filter-out src/fleet/main.c,$(FLEET_SOURCES)))
FLEET_TEST_BINS = $(addprefix $(BUILD_DIR)/test-,fleet task-package task-result workflow-data workflow-schedule agent-profile agent-auth plan)

$(BUILD_DIR)/fleet/%.o: src/fleet/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/libhydra-fleet.a: $(FLEET_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(FLEET_OBJECTS)

$(BUILD_DIR)/hydra-fleet: $(BUILD_DIR)/fleet/main.o $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $^ $(FLEET_JSON_LIB) -lm -o $@

.PHONY: build-plan-precompile
build-plan-precompile: $(BUILD_DIR)/plan-precompile

# Shared native precompiler for the finite planning examples; not installed.
PLAN_PRECOMPILE_SOURCES = examples/planning/native/precompile.c examples/planning/native/lower.c examples/planning/native/staged.c
$(BUILD_DIR)/plan-precompile: $(PLAN_PRECOMPILE_SOURCES) examples/planning/native/precompile.h $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(PLAN_PRECOMPILE_SOURCES) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@

.PHONY: build-plan-example
build-plan-example: $(BUILD_DIR)/plan-example
PLAN_EXAMPLE_SOURCES = $(wildcard examples/planning/native/example*.c)
$(BUILD_DIR)/plan-example: $(PLAN_EXAMPLE_SOURCES) $(wildcard examples/planning/native/example*.h) $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(PLAN_EXAMPLE_SOURCES) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@

$(BUILD_DIR)/test-fleet: tests/c/test_fleet.c
$(BUILD_DIR)/test-task-package: tests/c/test_task_package.c
$(BUILD_DIR)/test-task-result: tests/c/test_task_result.c
$(BUILD_DIR)/test-workflow-data: tests/c/test_workflow_data.c
$(BUILD_DIR)/test-agent-profile: tests/c/test_agent_profile.c
$(BUILD_DIR)/test-agent-auth: tests/c/test_agent_auth.c
$(BUILD_DIR)/test-plan: tests/c/test_plan.c
$(BUILD_DIR)/test-workflow-schedule: tests/c/test_workflow_schedule.c

$(FLEET_TEST_BINS): $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) -MMD -MP -MF $@.d -MT $@ $(filter %.c,$^) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@

-include $(FLEET_OBJECTS:.o=.d) $(BUILD_DIR)/fleet/main.d $(FLEET_TEST_BINS:%=%.d)

test-workflow-contracts: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/workflow-contract-cases" --runtime

.PHONY: test-fleet-controls test-fleet-recovery
test-fleet-controls: build-tui
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-fleet-controls"

test-fleet-recovery: build-core build-fleet build-tui
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" HYDRA_CORE="$(abspath $(BUILD_DIR))/hydra-core" BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-fleet-recovery"

.PHONY: test-plan-outcomes test-task-announce test-plan-reuse test-retention test-workflow-metrics test-plan-staged test-plan-staged-public
test-plan-outcomes: build-fleet build-plan-precompile
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-patterns"
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-performance-outcome"
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-research-outcome"
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-manifest"

test-task-announce: build-fleet
	"$(BUILD_DIR)/native-tests/test-task-announce" "$(abspath $(BUILD_DIR))/hydra-fleet"

test-plan-reuse: build-fleet
	HYDRA_TEST_PLAN_REPAIR=combine HYDRA_TEST_PLAN_REUSE=1 HYDRA_TEST_PLAN_REPAIR_FAULT=1 HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

test-retention: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-retention"

test-workflow-metrics: build-fleet build-core
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-workflow-task-metrics"
	"$(BUILD_DIR)/native-tests/test-statistics-export" "$(abspath $(BUILD_DIR))/hydra-core"

test-plan-staged: build-fleet build-plan-precompile
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-staged"

test-plan-staged-public: build-fleet build-plan-precompile
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" "$(BUILD_DIR)/native-tests/test-plan-staged-public" --fleet "$(abspath $(BUILD_DIR))/hydra-fleet" --output "$(BUILD_DIR)/staged-public.json"

.PHONY: test-plan-inspection
test-plan-inspection: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-inspection"


.PHONY: sanitize-fleet
sanitize-fleet: build-fleet
	# Exercise packaging with the deployable binary; sanitizer debug data exceeds the wire limit.
	HYDRA_TEST_PACKAGE_BINARY="$(abspath $(BUILD_DIR))/hydra-fleet" $(MAKE) BUILD_DIR=build/fleet-sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-fleet

# C analysis and reviewed complexity ceiling; use the native build flags.
CLANG_TIDY ?= scripts/clang-tidy.sh
QUALITY_C_SYSROOT = $(shell if [ "$$(uname -s)" = Darwin ]; then xcrun --show-sdk-path; fi)
QUALITY_C_FLAGS = $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(if $(QUALITY_C_SYSROOT),-isysroot $(QUALITY_C_SYSROOT))
.PHONY: quality-c test-quality-c
.PHONY: quality-c-flags
quality-c-flags:
	@printf '%s\n' $(QUALITY_C_FLAGS)

quality-c:
	@pkg-config --exists json-c
	@sh scripts/quality-c.sh docs/quality/cognitive-complexity.tsv $(CLANG_TIDY) $(NATIVE_SOURCES) $(wildcard tests/c/*.c tests/native/*.c tests/fixture/*.c tests/termviz/*.c examples/planning/native/*.c examples/planning/feature/*.c) -- $(QUALITY_C_FLAGS)

test-quality-c:
	@sh tests/quality_c_cases.sh "$(CLANG_TIDY)"

.PHONY: test-attached-pty sanitize-attached
test-attached-pty: build-tui
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-attached-pty"
sanitize-attached:
	@$(MAKE) BUILD_DIR=build/attached-sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-attached-pty

.PHONY: test-termviz-export
test-termviz-export:
	sh tests/termviz/test_export.sh

.PHONY: test-plan-workspace sanitize-plan-workspace
test-plan-workspace: build-tui build-fleet
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-plan-workspace"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-plan-launch"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-workflow-controls"
sanitize-plan-workspace:
	@$(MAKE) BUILD_DIR=build/plan-sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-plan-workspace

sanitize-workspace:
	@$(MAKE) BUILD_DIR=build/workspace-sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-termviz test-workspace-pty

$(BUILD_DIR)/termviz-workspace $(BUILD_DIR)/termviz-example $(BUILD_DIR)/test-termviz $(BUILD_DIR)/test-termviz-present $(BUILD_DIR)/test-termviz-unicode $(BUILD_DIR)/test-termviz-terminal $(BUILD_DIR)/test-termviz-input: $(TERMVIZ_HEADERS)

# Read-only discovery acceptance uses real OpenSSH config expansion and a
# controlled SSH executable; it does not use tmux or contact network hosts.
.PHONY: test-discovery
test-discovery: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-discovery" "$(abspath $(BUILD_DIR))/native-tests/discovery-ssh-fixture"

.PHONY: test-enrollment
test-enrollment: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-enrollment" "$(abspath $(BUILD_DIR))/native-tests/enrollment-ssh-fixture" "$(abspath $(BUILD_DIR))/native-tests/enrollment-receiver-fixture"
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-enrollment-receiver" "$(abspath $(BUILD_DIR))/native-tests/enrollment-receiver-fixture"

.PHONY: test-enrollment-ssh
test-enrollment-ssh: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-enrollment-ssh" "$(abspath $(BUILD_DIR))/native-tests/enrollment-loopback-fixture" "$(abspath $(BUILD_DIR))/native-tests/enrollment-receiver-fixture"

include scripts/native-tests.mk

include scripts/fleet-tests.mk
