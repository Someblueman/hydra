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
SANITIZER_FLAGS ?= $(shell if [ "$$(uname -s)" = Darwin ]; then printf '%s' '-fsanitize=undefined'; else printf '%s' '-fsanitize=address,undefined'; fi)

# Installation prefix (no root required when writable)
PREFIX ?= /usr/local
DESTDIR ?=

# Default target
all: lint

# Lint all shell scripts for POSIX compliance
lint:
	@echo "Running ShellCheck for POSIX compliance..."
	@find . -name "*.sh" -o -path "./bin/hydra" | while read -r file; do \
		echo "Checking $$file..."; \
		shellcheck --shell=sh --severity=style "$$file" || exit 1; \
	done
	@echo "Running dash syntax check..."
	@find . -name "*.sh" -o -path "./bin/hydra" | while read -r file; do \
		echo "Validating $$file..."; \
		dash -n "$$file" || exit 1; \
	done
	@echo "All checks passed!"

# Run tests (skip helpers.sh — it is a library, not a suite)
test:
	@echo "Running tests..."
	@if [ -d tests ] && [ -n "$$(ls -A tests/test_*.sh 2>/dev/null)" ]; then \
		for test in tests/test_*.sh; do \
			case "$$test" in tests/test_core.sh|tests/test_native_install.sh) continue ;; esac; \
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

$(BUILD_DIR)/hydra-tui: src/hydra_tui.c | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) src/hydra_tui.c -o $@

$(BUILD_DIR)/test-libhydra: tests/c/test_libhydra.c src/libhydra.h $(BUILD_DIR)/libhydra.a
	$(CC) $(CORE_CFLAGS) tests/c/test_libhydra.c $(BUILD_DIR)/libhydra.a -o $@

$(BUILD_DIR)/test-tui-pty: tests/c/test_tui_pty.c | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_tui_pty.c -o $@

test-c: $(BUILD_DIR)/test-libhydra
	$(BUILD_DIR)/test-libhydra

test-tui: build-tui
	@sh tests/test_native_tui.sh

test-tui-pty: build-tui $(BUILD_DIR)/test-tui-pty
	@mkdir -p "$(CURDIR)/$(BUILD_DIR)/test-tui-home"
	@: > "$(CURDIR)/$(BUILD_DIR)/test-tui-home/tags"
	@: > "$(CURDIR)/$(BUILD_DIR)/test-tui-home/map"
	@before="$$(find "$(CURDIR)/$(BUILD_DIR)/test-tui-home" -type f -exec cksum {} \; | sort | cksum)"; \
	HYDRA_HOME="$(CURDIR)/$(BUILD_DIR)/test-tui-home" \
		HYDRA_TEST_BIN="$(CURDIR)/bin/hydra" \
		HYDRA_TUI_BIN="$(CURDIR)/tests/fixtures/tui/crash-native.sh" \
		HYDRA_TEST_CRASH_DISPATCH="$(CURDIR)/tests/fixtures/tui/crash-dispatch.sh" \
		HYDRA_TEST_SLOW_HYDRA="$(CURDIR)/tests/fixtures/tui/slow-hydra.sh" \
		$(BUILD_DIR)/test-tui-pty "$(CURDIR)/$(BUILD_DIR)/hydra-tui" \
		"$(CURDIR)/tests/fixtures/tui/fake-hydra.sh" "$(CURDIR)/tests/fixtures/tui/fake-bin"; \
	status=$$?; \
	after="$$(find "$(CURDIR)/$(BUILD_DIR)/test-tui-home" -type f -exec cksum {} \; | sort | cksum)"; \
	if [ "$$before" != "$$after" ]; then echo "native crash path changed Hydra state" >&2; exit 1; fi; \
	exit $$status

test-parity: build-core
	@sh tests/test_core.sh

test-all: lint test test-c test-tui test-tui-pty test-parity test-install test-native-install smoke-onboarding

sanitize-core:
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" test-c

sanitize-tui:
	@$(MAKE) BUILD_DIR=build/sanitize CFLAGS="-O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer" build-tui
	@build/sanitize/hydra-tui --headless-fixture tests/fixtures/tui/native-v2.tsv --size 80x24 --frames 2 >/dev/null

sanitizer: sanitize-core

sanitize: sanitize-core sanitize-tui

bench-core: build-core
	@sh scripts/bench-core.sh

benchmark-core: bench-core

bench-tui: build-tui $(BUILD_DIR)/test-tui-pty
	@sh scripts/bench-tui.sh

package-core: build-core
	@sh scripts/package-core.sh

package-tui: build-tui
	@sh scripts/package-tui.sh

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
