# Makefile for Hydra
# POSIX-compliant build and lint tasks

.PHONY: all lint test test-all clean install uninstall test-install test-native-install smoke-onboarding \
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

# Installation prefix (no root required when writable)
PREFIX ?= /usr/local
DESTDIR ?=

# Default target
all: lint

# Lint all shell scripts for POSIX compliance
lint:
	@sh scripts/lint-shell.sh
	@echo "All checks passed!"

# Run shell-only tests; native suites have build prerequisites in their own targets.
test:
	@echo "Running tests..."
	@if [ -d tests ] && [ -n "$$(ls -A tests/test_*.sh 2>/dev/null)" ]; then \
		for test in tests/test_*.sh; do \
			case "$$test" in tests/test_core.sh|tests/test_native_install.sh|tests/test_native_tui.sh|tests/test_fleet.sh|tests/test_fleet_install.sh|tests/test_task_package.sh|tests/test_task_acceptance.sh|tests/test_workflow_data.sh|tests/test_workflow_plan.sh|tests/test_workflow_approval.sh|tests/test_agent_execution.sh|tests/test_agent_auth.sh) continue ;; esac; \
			echo "Running $$test..."; \
			sh "$$test" || exit 1; \
		done; \
	else \
		echo "No tests found in tests/"; \
	fi

# Optional read-only native helper. The shell CLI remains the mutation authority.
build-core: $(BUILD_DIR)/hydra-core

build-tui: $(BUILD_DIR)/hydra-tui

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libhydra.o: src/libhydra.c src/libhydra.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -c src/libhydra.c -o $@

$(BUILD_DIR)/libhydra.a: $(BUILD_DIR)/libhydra.o
	$(AR) rcs $@ $<

$(BUILD_DIR)/hydra-core: src/hydra_core.c src/libhydra.h $(BUILD_DIR)/libhydra.a
	$(CC) $(CORE_CFLAGS) src/hydra_core.c $(BUILD_DIR)/libhydra.a -o $@

TUI_SOURCES = $(filter src/tui/%.c,$(NATIVE_SOURCES))
TUI_OBJECTS = $(patsubst src/tui/%.c,$(BUILD_DIR)/tui/%.o,$(TUI_SOURCES))

$(BUILD_DIR)/tui/%.o: src/tui/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/hydra-tui: $(TUI_OBJECTS)
	$(CC) $(CORE_CFLAGS) $^ -o $@

-include $(TUI_OBJECTS:.o=.d)

$(BUILD_DIR)/test-libhydra: tests/c/test_libhydra.c src/libhydra.h $(BUILD_DIR)/libhydra.a
	$(CC) $(CORE_CFLAGS) tests/c/test_libhydra.c $(BUILD_DIR)/libhydra.a -o $@

$(BUILD_DIR)/test-tui-pty: tests/c/test_tui_pty.c tests/c/test_tui_mouse.inc tests/c/test_tui_themes.inc tests/c/test_tui_palette.inc | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_tui_pty.c -o $@

test-c: $(BUILD_DIR)/test-libhydra
	$(BUILD_DIR)/test-libhydra

$(BUILD_DIR)/test-tui-input: tests/c/test_tui_input.c src/tui/input.c $(filter-out $(BUILD_DIR)/tui/main.o $(BUILD_DIR)/tui/input.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

test-tui: build-tui $(BUILD_DIR)/test-tui-input
	$(BUILD_DIR)/test-tui-input
	@sh tests/test_native_tui.sh

test-tui-pty: build-tui $(BUILD_DIR)/test-tui-pty
	@mkdir -p "$(CURDIR)/$(BUILD_DIR)/test-tui-home"
	@: > "$(CURDIR)/$(BUILD_DIR)/test-tui-home/sentinel"
	@before="$$(find "$(CURDIR)/$(BUILD_DIR)/test-tui-home" -type f -exec cksum {} \; | sort | cksum)"; \
	HYDRA_HOME="$(CURDIR)/$(BUILD_DIR)/test-tui-home" \
		HYDRA_TEST_BIN="$(CURDIR)/bin/hydra" \
		HYDRA_TUI_BIN="$(CURDIR)/tests/fixtures/tui/crash-native.sh" \
		HYDRA_TEST_CRASH_DISPATCH="$(CURDIR)/tests/fixtures/tui/crash-dispatch.sh" \
		HYDRA_TEST_SLOW_HYDRA="$(CURDIR)/tests/fixtures/tui/slow-hydra.sh" \
		HYDRA_TEST_EOF_HYDRA="$(CURDIR)/tests/fixtures/tui/eof-hydra.sh" \
		$(BUILD_DIR)/test-tui-pty "$(CURDIR)/$(BUILD_DIR)/hydra-tui" \
		"$(CURDIR)/tests/fixtures/tui/fake-hydra.sh" "$(CURDIR)/tests/fixtures/tui/fake-bin"; \
	status=$$?; \
	after="$$(find "$(CURDIR)/$(BUILD_DIR)/test-tui-home" -type f -exec cksum {} \; | sort | cksum)"; \
	if [ "$$before" != "$$after" ]; then echo "native crash path changed Hydra state" >&2; exit 1; fi; \
	exit $$status

test-parity: build-core
	@sh tests/test_core.sh

test-all: lint test test-fleet test-c test-tui test-tui-pty test-parity test-install test-native-install smoke-onboarding

sanitize-core:
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-c

sanitize-tui:
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" build-tui
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" build/sanitize/test-tui-input
	@build/sanitize/test-tui-input
	@build/sanitize/hydra-tui --headless-fixture tests/fixtures/tui/native-v2.tsv --size 80x24 --frames 2 >/dev/null

sanitizer: sanitize-core

sanitize: sanitize-core sanitize-tui sanitize-fleet

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
	@echo "  make build-core - Build the optional read-only native helper"
	@echo "  make build-tui - Build the optional native mission-control TUI"
	@echo "  make test-c    - Run native library unit tests"
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

.PHONY: build-fleet test-fleet
build-fleet: $(BUILD_DIR)/hydra-fleet

# Compile shared fleet code once. Compiler dependency files track the actual
# header/.inc closure for each object and test, including sanitizer builds.
FLEET_OBJECTS = $(patsubst src/fleet/%.c,$(BUILD_DIR)/fleet/%.o,$(filter-out src/fleet/main.c,$(FLEET_SOURCES)))
FLEET_TEST_BINS = $(addprefix $(BUILD_DIR)/test-,fleet task-package task-result workflow-data agent-profile agent-auth plan)

$(BUILD_DIR)/fleet/%.o: src/fleet/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/libhydra-fleet.a: $(FLEET_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(FLEET_OBJECTS)

$(BUILD_DIR)/hydra-fleet: $(BUILD_DIR)/fleet/main.o $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $^ $(FLEET_JSON_LIB) -lm -o $@

$(BUILD_DIR)/test-fleet: tests/c/test_fleet.c
$(BUILD_DIR)/test-task-package: tests/c/test_task_package.c
$(BUILD_DIR)/test-task-result: tests/c/test_task_result.c
$(BUILD_DIR)/test-workflow-data: tests/c/test_workflow_data.c
$(BUILD_DIR)/test-agent-profile: tests/c/test_agent_profile.c
$(BUILD_DIR)/test-agent-auth: tests/c/test_agent_auth.c
$(BUILD_DIR)/test-plan: tests/c/test_plan.c

$(FLEET_TEST_BINS): $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) -MMD -MP -MF $@.d -MT $@ $(filter %.c,$^) $(BUILD_DIR)/libhydra-fleet.a $(FLEET_JSON_LIB) -lm -o $@

-include $(FLEET_OBJECTS:.o=.d) $(BUILD_DIR)/fleet/main.d $(FLEET_TEST_BINS:%=%.d)

test-fleet: build-fleet $(BUILD_DIR)/test-plan $(BUILD_DIR)/test-agent-auth $(BUILD_DIR)/test-agent-profile $(BUILD_DIR)/test-workflow-data $(BUILD_DIR)/test-fleet $(BUILD_DIR)/test-task-package $(BUILD_DIR)/test-task-result
	$(BUILD_DIR)/test-plan
	$(BUILD_DIR)/test-agent-auth
	$(BUILD_DIR)/test-agent-profile
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_agent_auth.sh
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_agent_execution.sh
	$(BUILD_DIR)/test-workflow-data
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_workflow_data.sh
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_workflow_plan.sh
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_workflow_approval.sh
	$(BUILD_DIR)/test-fleet
	$(BUILD_DIR)/test-task-package
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_fleet.sh
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_fleet_install.sh
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_task_package.sh
	HYDRA_FLEET_BIN="$(CURDIR)/$(BUILD_DIR)/hydra-fleet" sh tests/test_task_acceptance.sh

.PHONY: sanitize-fleet
sanitize-fleet:
	$(MAKE) BUILD_DIR=build/fleet-sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-fleet

# C analysis and reviewed complexity ceiling; use the native build flags.
CLANG_TIDY ?= build/quality-tools/bin/clang-tidy
QUALITY_C_SYSROOT = $(shell if [ "$$(uname -s)" = Darwin ]; then xcrun --show-sdk-path; fi)
QUALITY_C_FLAGS = $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) $(if $(QUALITY_C_SYSROOT),-isysroot $(QUALITY_C_SYSROOT))
.PHONY: quality-c test-quality-c
quality-c:
	@pkg-config --exists json-c
	@sh scripts/quality-c.sh docs/quality/cognitive-complexity.tsv $(CLANG_TIDY) $(NATIVE_SOURCES) $(wildcard tests/c/*.c) -- $(QUALITY_C_FLAGS)

test-quality-c:
	@sh tests/quality_c_cases.sh "$(CLANG_TIDY)"
