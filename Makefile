# Makefile for Hydra
# POSIX-compliant build and lint tasks

.PHONY: all lint test test-fast test-all clean install uninstall test-install test-native-install smoke-onboarding \
	dev-setup bench bench-tui build-core build-tui test-c test-tui test-tui-pty test-parity sanitize-core sanitize-tui sanitizer \
	sanitize bench-core benchmark-core package-core package-tui help

CC ?= cc
AR ?= ar
CFLAGS ?= -O2
# Warnings are errors for developers and CI. A build on a user's machine
# (install.sh) passes WERROR= so a newer compiler's new warning cannot break it.
WERROR ?= -Werror
CORE_CFLAGS = $(CFLAGS) -std=c99 -Wall -Wextra $(WERROR) -pedantic -Isrc
BUILD_DIR ?= build
# Build and analysis share recursive discovery so domain directories cannot
# silently omit native sources from the quality gate.
NATIVE_SOURCES = $(shell find src -type f -name '*.c' | LC_ALL=C sort)
# Named in prerequisite lists below, which make expands as it reads them.
# src/kettle/tools holds standalone programs with their own main(); they
# link the library but are never part of it.
KETTLE_SOURCES = $(filter-out src/kettle/tools/%,$(filter src/kettle/%.c,$(NATIVE_SOURCES)))
# Shared code is compiled once per BUILD_DIR. Tests and examples link these
# objects (kettle through its archive) instead of recompiling the sources.
KETTLE_HEADERS = $(wildcard src/kettle/*.h src/kettle/*.inc)
KETTLE_OBJECTS = $(patsubst src/kettle/%.c,$(BUILD_DIR)/kettle/%.o,$(KETTLE_SOURCES))
KETTLE_LIB = $(BUILD_DIR)/libkettle.a
CORE_STATS_SOURCES = src/hydra_statistics.c src/hydra_statistics_metrics.c src/hydra_statistics_cohorts.c src/hydra_statistics_export.c src/hydra_statistics_export_cohorts.c src/hydra_statistics_cli.c
CORE_STATS_OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(CORE_STATS_SOURCES))
TUI_DATA_OBJECTS = $(BUILD_DIR)/hydra_statistics.o $(BUILD_DIR)/hydra_statistics_metrics.o $(BUILD_DIR)/hydra_statistics_cohorts.o
STATS_EXPORT_OBJECTS = $(TUI_DATA_OBJECTS) $(BUILD_DIR)/hydra_statistics_export.o $(BUILD_DIR)/hydra_statistics_export_cohorts.o
SANITIZER_FLAGS ?= $(shell if [ "$$(uname -s)" = Darwin ]; then printf '%s' '-fsanitize=undefined'; else printf '%s' '-fsanitize=address,undefined'; fi)

# Sanitizer diagnostics must fail a case even when its output is captured.
export UBSAN_OPTIONS ?= halt_on_error=1

# hydra-tui draws Unicode only when the locale's character set is UTF-8 and
# falls back to ASCII glyphs otherwise; the TUI and PTY suites compare the
# Unicode frames. A caller's UTF-8 locale is kept. Under any other locale these
# targets run with LC_ALL set to the first installed of C.UTF-8 and en_US.UTF-8
# (macOS releases without C.UTF-8); with neither installed nothing changes.
TUI_TEST_LOCALE := $(shell if [ "$$(locale charmap 2>/dev/null)" != UTF-8 ]; then locale -a 2>/dev/null | awk '{ name = tolower($$0); sub(/utf-8$$/, "utf8", name) } name == "c.utf8" { c = $$0 } name == "en_us.utf8" { e = $$0 } END { print (c != "" ? c : e) }'; fi)
TUI_TEST_TARGETS = test-tui test-tui-pty test-tui-smoke test-statistics test-program-pty test-workspace-pty \
	test-attached-pty test-plan-workspace test-fleet-controls test-fleet-recovery test-visualization
ifneq ($(TUI_TEST_LOCALE),)
$(TUI_TEST_TARGETS): export LC_ALL := $(TUI_TEST_LOCALE)
endif

# Installation prefix (no root required when writable)
PREFIX ?= /usr/local
DESTDIR ?=

# Default target
all: lint

# Lint all shell scripts for POSIX compliance
lint:
	@sh scripts/lint-shell.sh
	@sh scripts/lint-extra.sh
	@sh scripts/check-fleet-assets.sh release/fleet-assets.tsv
	@echo "All checks passed!"

# Run CLI shell tests with their native structured-data fixture helpers.
# Every shell case runs through scripts/run-shell-test.sh, which exports these
# binaries from BUILD_DIR (never ./build) and gives the case private temp/tmux dirs.
SHELL_TEST_PREREQS = build-fleet build-tui build-core build-test-fixture $(BUILD_DIR)/native-tests/statistics-evidence $(BUILD_DIR)/test-statistics $(BUILD_DIR)/test-shell-exec
test: $(SHELL_TEST_PREREQS)
	@echo "Running tests..."
	+@case "$(MAKEFLAGS)" in \
		*jobserver*) exec $(MAKE) shell-tests ;; \
		*) exec $(MAKE) -j$(TEST_JOBS) shell-tests ;; \
	esac

# Fixed PR feedback lane. Keep the shell and native selection explicit so this
# target remains useful for every change and does not depend on changed-file
# heuristics. Shared prerequisites are built before any stateful test starts.
FAST_SHELL_TESTS = foundation paths state json_output git_simple project_trust deps tmux lifecycle
FAST_NATIVE_BINS = $(filter-out $(BUILD_DIR)/test-task-result,$(FLEET_TEST_BINS))
test-fast:
	+@$(MAKE) -j$(TEST_JOBS) $(SHELL_TEST_PREREQS) $(FAST_NATIVE_BINS) $(BUILD_DIR)/test-libhydra $(TUI_TEST_BINS) $(KETTLE_TEST_BINS)
	@$(MAKE) lint
	@echo "Running fast shell tests..."
	@for name in $(FAST_SHELL_TESTS); do \
		BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "fast-$$name" "tests/test_$${name}.sh" || exit 1; \
	done
	@$(MAKE) -j1 test-c test-tui test-parity test-kettle
	@$(BUILD_DIR)/test-statistics
	@echo "Running fast native fleet unit tests..."
	@for binary in $(FAST_NATIVE_BINS); do \
		echo "Running $$binary..."; \
		"$$binary" || exit 1; \
	done

# Run one shell test file the way make test does: make test-one T=kill
# (T may also be test_kill or tests/test_kill.sh). The case log is printed.
.PHONY: test-one
test-one: $(SHELL_TEST_PREREQS)
	@name="$(T)"; name="$${name#tests/}"; name="$${name#test_}"; name="$${name%.sh}"; \
	if [ -z "$$name" ] || [ ! -f "tests/test_$$name.sh" ]; then \
		echo "usage: make test-one T=<name>   (runs tests/test_<name>.sh)" >&2; exit 2; \
	fi; \
	BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" "test-one-$$name" "tests/test_$$name.sh" && \
		cat "$(BUILD_DIR)/test-logs/test-one-$$name.log"

# Group-writable umask lane (Ubuntu's default 002): Hydra state stays private
# and readers keep refusing shared state, while worktrees follow the umask.
UMASK_FLEET_CASES = dag-crash-2 dag-parallelism-1
.PHONY: test-umask
test-umask: fleet-test-build
	@umask 002 && $(MAKE) test-fast
	@umask 002 && $(MAKE) $(addprefix fleet-case-,$(UMASK_FLEET_CASES))
	@umask 002 && $(MAKE) test-one T=umask_state

# Optional read-only native helper. The shell CLI remains the mutation authority.
build-core: $(BUILD_DIR)/hydra-core

build-tui: $(BUILD_DIR)/hydra-tui

.PHONY: test-kettle example-kettle example-workspace example-counter example-themes example-components test-workspace-pty test-program-pty test-visualization sanitize-workspace
test-visualization: build-tui test-kettle $(BUILD_DIR)/test-statistics
	BUILD_DIR="$(abspath $(BUILD_DIR))" sh tests/test_visualization.sh
# Kettle is compiled once (the $(BUILD_DIR)/kettle/%.o rule below) and archived.
# Its tests and examples link the archive, so each takes only the members it
# references; hydra-tui does the same.
$(KETTLE_LIB): $(KETTLE_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(KETTLE_OBJECTS)

# One translation unit per binary (an example's test #includes its example).
# The compiler writes $@.d, so every header, .inc and included .c is tracked;
# the recipe therefore compiles only the first .c prerequisite, the source named
# on the lines below (the $@.d files are read after them).
KETTLE_UNIT_TESTS = kettle kettle-present kettle-style kettle-draw kettle-unicode kettle-terminal kettle-input kettle-keymap kettle-program kettle-components kettle-widgets
KETTLE_EXAMPLE_TESTS = $(addprefix $(BUILD_DIR)/test-kettle-,counter themes components-example)
KETTLE_EXAMPLE_BINS = $(addprefix $(BUILD_DIR)/kettle-,example counter themes components)
KETTLE_LINKED_BINS = $(addprefix $(BUILD_DIR)/test-,$(KETTLE_UNIT_TESTS)) $(KETTLE_EXAMPLE_TESTS) $(KETTLE_EXAMPLE_BINS) $(BUILD_DIR)/kettle-program-fixture
KETTLE_TEST_BINS = $(addprefix $(BUILD_DIR)/test-,$(KETTLE_UNIT_TESTS)) $(BUILD_DIR)/test-kettle-workspace $(KETTLE_EXAMPLE_TESTS)

$(foreach n,$(KETTLE_UNIT_TESTS),$(eval $(BUILD_DIR)/test-$(n): tests/c/test_$(subst -,_,$(n)).c))
# The counter, themes and components examples' own tests, written with the headless driver.
$(BUILD_DIR)/test-kettle-counter: examples/counter_test.c
$(BUILD_DIR)/test-kettle-themes: examples/themes_test.c
$(BUILD_DIR)/test-kettle-components-example: examples/components_test.c
$(BUILD_DIR)/kettle-example: examples/kettle.c
$(BUILD_DIR)/kettle-counter: examples/counter.c
$(BUILD_DIR)/kettle-themes: examples/themes.c
$(BUILD_DIR)/kettle-components: examples/components.c
# A runtime application driven on real PTYs by tests/kettle/test_program_pty.c.
$(BUILD_DIR)/kettle-program-fixture: tests/kettle/program_fixture.c
$(KETTLE_EXAMPLE_TESTS) $(KETTLE_EXAMPLE_BINS): KETTLE_INCLUDES = -Isrc/kettle

$(KETTLE_LINKED_BINS): $(KETTLE_LIB) | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) $(KETTLE_INCLUDES) -MMD -MP -MF $@.d -MT $@ $(firstword $(filter %.c,$^)) $(KETTLE_LIB) -o $@

# workspace.c stands alone: this case links that one object, not the archive.
$(BUILD_DIR)/test-kettle-workspace: tests/c/test_kettle_workspace.c $(BUILD_DIR)/kettle/workspace.o
	$(CC) $(CORE_CFLAGS) -MMD -MP -MF $@.d -MT $@ tests/c/test_kettle_workspace.c $(BUILD_DIR)/kettle/workspace.o -o $@

$(BUILD_DIR)/kettle-workspace: examples/workspace.c examples/workspace_view.c examples/workspace_input.c examples/workspace_demo.h $(KETTLE_HEADERS) $(KETTLE_LIB)
	$(CC) $(CORE_CFLAGS) -Isrc/kettle $(filter %.c,$^) $(KETTLE_LIB) -o $@

-include $(KETTLE_LINKED_BINS:%=%.d) $(BUILD_DIR)/test-kettle-workspace.d

test-kettle: $(KETTLE_TEST_BINS)
	$(BUILD_DIR)/test-kettle
	$(BUILD_DIR)/test-kettle-present
	$(BUILD_DIR)/test-kettle-style
	$(BUILD_DIR)/test-kettle-draw
	$(BUILD_DIR)/test-kettle-workspace
	$(BUILD_DIR)/test-kettle-unicode
	$(BUILD_DIR)/test-kettle-terminal
	$(BUILD_DIR)/test-kettle-input
	$(BUILD_DIR)/test-kettle-keymap
	$(BUILD_DIR)/test-kettle-program
	$(BUILD_DIR)/test-kettle-counter
	$(BUILD_DIR)/test-kettle-themes
	$(BUILD_DIR)/test-kettle-components
	$(BUILD_DIR)/test-kettle-widgets
	$(BUILD_DIR)/test-kettle-components-example

test-program-pty: $(BUILD_DIR)/kettle-program-fixture $(BUILD_DIR)/kettle-components
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-program-pty"

$(BUILD_DIR)/test-workspace-child: tests/c/test_workspace_child.c | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_workspace_child.c -o $@

test-workspace-pty: test-program-pty example-workspace build-tui $(BUILD_DIR)/test-workspace-child
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-pty"

$(BUILD_DIR)/test-statistics: tests/c/test_statistics.c src/hydra_statistics.h $(TUI_DATA_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_statistics.c $(TUI_DATA_OBJECTS) -o $@

.PHONY: test-statistics
test-statistics: $(BUILD_DIR)/test-statistics build-tui
	$(BUILD_DIR)/test-statistics
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-statistics-pty"

example-workspace: $(BUILD_DIR)/kettle-workspace

example-kettle: $(BUILD_DIR)/kettle-example

example-counter: $(BUILD_DIR)/kettle-counter

example-themes: $(BUILD_DIR)/kettle-themes

example-components: $(BUILD_DIR)/kettle-components

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libhydra.o: src/libhydra.c src/libhydra.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -c src/libhydra.c -o $@

$(BUILD_DIR)/libhydra.a: $(BUILD_DIR)/libhydra.o
	$(AR) rcs $@ $<

# The statistics sources are compiled once and shared by hydra-core, hydra-tui
# and both statistics tests.
$(CORE_STATS_OBJECTS): $(BUILD_DIR)/%.o: src/%.c src/hydra_statistics.h | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/hydra-core: src/hydra_core.c src/libhydra.h src/hydra_statistics.h $(CORE_STATS_OBJECTS) $(BUILD_DIR)/libhydra.a
	$(CC) $(CORE_CFLAGS) src/hydra_core.c $(CORE_STATS_OBJECTS) $(BUILD_DIR)/libhydra.a -o $@

$(BUILD_DIR)/test-statistics-export: tests/c/test_statistics_export.c src/hydra_statistics.h $(STATS_EXPORT_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_statistics_export.c $(STATS_EXPORT_OBJECTS) -o $@

.PHONY: test-statistics-export
test-statistics-export: build-core $(BUILD_DIR)/test-statistics-export
	$(BUILD_DIR)/test-statistics-export
	"$(BUILD_DIR)/native-tests/test-statistics-export" $(BUILD_DIR)/hydra-core

TUI_SOURCES = $(filter src/tui/%.c,$(NATIVE_SOURCES))
TUI_OBJECTS = $(patsubst src/tui/%.c,$(BUILD_DIR)/tui/%.o,$(TUI_SOURCES))

$(BUILD_DIR)/tui/%.o: src/tui/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/kettle/%.o: src/kettle/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) -MMD -MP -c $< -o $@

# Linking the archive rather than every object leaves out kettle code the app
# never references (unused components, the headless test driver).
$(BUILD_DIR)/hydra-tui: $(TUI_OBJECTS) $(TUI_DATA_OBJECTS) $(KETTLE_LIB)
	$(CC) $(CORE_CFLAGS) $(TUI_OBJECTS) $(TUI_DATA_OBJECTS) $(KETTLE_LIB) -o $@

-include $(TUI_OBJECTS:.o=.d) $(KETTLE_OBJECTS:.o=.d) $(CORE_STATS_OBJECTS:.o=.d)

$(BUILD_DIR)/test-libhydra: tests/c/test_libhydra.c src/libhydra.h $(BUILD_DIR)/libhydra.a
	$(CC) $(CORE_CFLAGS) tests/c/test_libhydra.c $(BUILD_DIR)/libhydra.a -o $@

$(BUILD_DIR)/test-tui-pty: tests/c/test_tui_pty.c tests/c/test_tui_mouse.inc tests/c/test_tui_themes.inc tests/c/test_tui_palette.inc tests/c/test_tui_visualization.inc tests/c/test_tui_attention.inc tests/c/test_tui_review.inc tests/c/test_tui_measure.inc tests/c/test_tui_measure_session.inc tests/c/test_tui_layout.inc tests/c/test_tui_remote.inc $(KETTLE_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CORE_CFLAGS) tests/c/test_tui_pty.c $(KETTLE_OBJECTS) -o $@

test-c: $(BUILD_DIR)/test-libhydra
	$(BUILD_DIR)/test-libhydra

# The loop runs over a model the test builds by hand: no state store is read
# (tests/c/tui_no_snapshot.c takes the place of the snapshot).
$(BUILD_DIR)/test-tui-input: tests/c/test_tui_input.c tests/c/tui_no_snapshot.c $(KETTLE_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o $(BUILD_DIR)/tui/snapshot.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $(filter %.c,$^) $(filter %.o,$^) -o $@

$(BUILD_DIR)/test-tui-actions: tests/c/test_tui_actions.c src/tui/actions.c src/tui/action_capture.c $(KETTLE_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o $(BUILD_DIR)/tui/actions.o $(BUILD_DIR)/tui/action_capture.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

.PHONY: test-tui-actions
test-tui-actions: $(BUILD_DIR)/test-tui-actions
	$(BUILD_DIR)/test-tui-actions

# These two link only the named app objects, already compiled for hydra-tui.
TUI_PLANNING_OBJECTS = $(addprefix $(BUILD_DIR)/tui/,task_name.o plan_diagnostics.o plan_summary.o plan_draft.o setup_json.o)
$(BUILD_DIR)/test-tui-planning: tests/c/test_tui_planning.c $(TUI_PLANNING_OBJECTS)
	$(CC) $(CORE_CFLAGS) -MMD -MP -MF $@.d -MT $@ tests/c/test_tui_planning.c $(TUI_PLANNING_OBJECTS) -o $@

$(BUILD_DIR)/test-tui-transcript: tests/c/test_tui_transcript.c $(BUILD_DIR)/tui/transcript.o $(KETTLE_OBJECTS)
	$(CC) $(CORE_CFLAGS) -MMD -MP -MF $@.d -MT $@ tests/c/test_tui_transcript.c $(BUILD_DIR)/tui/transcript.o $(KETTLE_OBJECTS) -o $@

-include $(BUILD_DIR)/test-tui-planning.d $(BUILD_DIR)/test-tui-transcript.d

$(BUILD_DIR)/test-tui-setup: tests/c/test_tui_setup.c $(KETTLE_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

$(BUILD_DIR)/test-tui-keymap: tests/c/test_tui_keymap.c $(KETTLE_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

$(BUILD_DIR)/test-tui-setup-upgrade: tests/c/test_tui_setup_upgrade.c $(KETTLE_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

$(BUILD_DIR)/test-tui-onboarding: tests/c/test_tui_onboarding.c $(KETTLE_OBJECTS) $(TUI_DATA_OBJECTS) $(filter-out $(BUILD_DIR)/tui/main.o,$(TUI_OBJECTS))
	$(CC) $(CORE_CFLAGS) $< $(filter %.o,$^) -o $@

TUI_TEST_BINS = $(addprefix $(BUILD_DIR)/test-tui-,input keymap actions planning transcript setup setup-upgrade onboarding)
test-tui: build-tui $(TUI_TEST_BINS)
	$(BUILD_DIR)/test-tui-input
	$(BUILD_DIR)/test-tui-keymap
	$(BUILD_DIR)/test-tui-actions
	$(BUILD_DIR)/test-tui-planning
	$(BUILD_DIR)/test-tui-transcript
	$(BUILD_DIR)/test-tui-setup tests/fixtures/tui/setup "$(abspath $(BUILD_DIR))/setup-screens"
	$(BUILD_DIR)/test-tui-setup-upgrade tests/fixtures/tui/setup
	$(BUILD_DIR)/test-tui-onboarding tests/fixtures/tui/onboarding "$(abspath $(BUILD_DIR))/onboarding-screens"
	@HYDRA_TUI_BIN="$(abspath $(BUILD_DIR))/hydra-tui" sh tests/test_native_tui.sh
	@HYDRA_TUI_BIN="$(abspath $(BUILD_DIR))/hydra-tui" sh tests/test_native_tui_i4.sh

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
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-remote-setup"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-responsive-reads"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-refresh-cost"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-onboarding"

test-parity: build-core build-tui
	@HYDRA_CORE="$(abspath $(BUILD_DIR))/hydra-core" sh tests/test_core.sh
	@HYDRA_TUI_BIN="$(abspath $(BUILD_DIR))/hydra-tui" sh tests/test_tui_store_rows.sh

test-all: test-fleet-controls test-fleet-recovery test-plan-inspection test-plan-outcomes test-statistics-export test-task-announce test-plan-reuse test-plan-workspace test-attached-pty test-kettle-export test-visualization test-workspace-pty test-statistics lint test test-fleet test-c test-tui test-tui-pty test-parity test-install test-native-install smoke-onboarding

# Sanitizer lanes. Every lane compiles with the same flags, so they share one
# tree, $(BUILD_DIR)/sanitize, and each object is built once. A lane is the
# ordinary test goals below run by a sub-make with that BUILD_DIR and CFLAGS.
# `make sanitize` runs the lanes in SANITIZE_LANES, in order, in one sub-make;
# `make sanitize-<lane>` runs one. Because the tree is shared, do not run
# several sanitize-<lane> goals under one `make -j`; use
# `make sanitize SANITIZE_LANES="core tui"` instead.
SANITIZE_DIR = $(BUILD_DIR)/sanitize
SANITIZE_CFLAGS = -O1 -g $(SANITIZER_FLAGS) -fno-omit-frame-pointer
SANITIZE_ARGS = BUILD_DIR="$(SANITIZE_DIR)" CFLAGS="$(SANITIZE_CFLAGS)"
SANITIZE_LANES ?= core tui fleet workspace attached plan-workspace statistics
SANITIZE_GOALS_core = test-c
SANITIZE_GOALS_tui = test-tui-smoke
SANITIZE_GOALS_fleet = test-fleet
SANITIZE_GOALS_workspace = test-kettle test-workspace-pty
SANITIZE_GOALS_attached = test-attached-pty
SANITIZE_GOALS_plan-workspace = test-plan-workspace
SANITIZE_GOALS_statistics = test-statistics
# Fleet cases package the deployable helper from the ordinary build: sanitizer
# debug data exceeds the wire limit. Only fleet cases read this variable.
SANITIZE_FLEET_ENV = HYDRA_TEST_PACKAGE_BINARY="$(abspath $(BUILD_DIR))/hydra-fleet"

# The TUI subset the sanitizer lane runs: two unit suites and a headless render.
.PHONY: test-tui-smoke
test-tui-smoke: build-tui $(BUILD_DIR)/test-tui-input $(BUILD_DIR)/test-tui-actions
	@$(BUILD_DIR)/test-tui-input
	@$(BUILD_DIR)/test-tui-actions
	@$(BUILD_DIR)/hydra-tui --headless-fixture tests/fixtures/tui/native-v2.tsv --size 80x24 --frames 2 >/dev/null

.PHONY: sanitize-fleet sanitize-attached sanitize-plan-workspace sanitize-statistics
sanitize-core sanitize-tui sanitize-workspace sanitize-attached sanitize-plan-workspace sanitize-statistics: sanitize-%:
	@$(MAKE) $(SANITIZE_ARGS) $(SANITIZE_GOALS_$*)

sanitize-fleet: build-fleet
	$(SANITIZE_FLEET_ENV) $(MAKE) $(SANITIZE_ARGS) test-fleet

sanitizer: sanitize-core

sanitize: $(if $(filter fleet,$(SANITIZE_LANES)),build-fleet)
	@$(if $(filter fleet,$(SANITIZE_LANES)),$(SANITIZE_FLEET_ENV)) $(MAKE) $(SANITIZE_ARGS) $(foreach lane,$(SANITIZE_LANES),$(if $(SANITIZE_GOALS_$(lane)),$(SANITIZE_GOALS_$(lane)),$(error unknown sanitizer lane '$(lane)')))

bench-core: build-core
	@sh scripts/bench-core.sh

benchmark-core: bench-core

.PHONY: bench-i1 test-bench-i1
bench-i1: build-core build-tui build-fleet $(BUILD_DIR)/test-tui-pty
	@sh scripts/bench-i1.sh --build "$(abspath $(BUILD_DIR))" $(BENCH_I1_ARGS)

test-bench-i1: build-core build-tui build-fleet $(BUILD_DIR)/test-tui-pty
	@HYDRA_ITEM10_BUILD="$(abspath $(BUILD_DIR))" sh tests/test_bench_i1.sh

bench-tui: build-tui $(BUILD_DIR)/test-tui-pty
	@sh scripts/bench-tui.sh

# Item 10 matrix over the I1 fixture: measurement only, never a gate.
# Example: make perf-matrix PERF_MATRIX_ARGS="--output /tmp/hydra-perf --heads 1 10"
.PHONY: perf-matrix perf-attention test-perf-matrix
perf-matrix: build-core build-tui build-fleet $(BUILD_DIR)/test-tui-pty
	@LANG=C.UTF-8 LC_ALL=C.UTF-8 PYTHONDONTWRITEBYTECODE=1 python3 scripts/perf-matrix.py --build "$(abspath $(BUILD_DIR))" $(PERF_MATRIX_ARGS)

perf-attention: build-fleet
	@LANG=C.UTF-8 LC_ALL=C.UTF-8 PYTHONDONTWRITEBYTECODE=1 python3 scripts/perf_matrix_attention.py --build "$(abspath $(BUILD_DIR))" $(PERF_ATTENTION_ARGS)

test-perf-matrix: build-core build-tui build-fleet $(BUILD_DIR)/test-tui-pty
	@HYDRA_PERF_BUILD="$(abspath $(BUILD_DIR))" sh tests/test_perf_matrix.sh

package-core: build-core
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/package-native.sh core

package-tui: build-tui
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/package-native.sh tui

# Record shell baseline timings (not a CI gate; no speedup claims)
bench:
	@sh scripts/bench.sh

# Clean temporary files
clean:
	@echo "Cleaning temporary files..."
	@find . -name "*~" -o -name "*.swp" -o -name ".*.swp" | xargs rm -f
	@dir="$(abspath $(BUILD_DIR))"; \
	case "$(CURDIR)/" in "$${dir%/}/"*) \
		echo "Error: refusing to remove BUILD_DIR='$(BUILD_DIR)': it is or contains this checkout" >&2; exit 1 ;; \
	esac; \
	rm -rf "$$dir"
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
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh tests/test_native_install.sh

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
	@echo "Hydra Makefile targets (outputs go to BUILD_DIR, default ./build):"
	@echo "  make lint      - Run ShellCheck and dash syntax validation, ruff (Python) and actionlint (workflows)"
	@echo "  make test      - Build the three native helpers and test fixtures, then run the shell test suite (workflow runs are driven by hydra-fleet)"
	@echo "  make test-fast - Run the fixed PR feedback test selection (lint, fast shell cases, native unit tests)"
	@echo "  make test-one T=<name> - Run tests/test_<name>.sh like make test does"
	@echo "  make test-umask - Run test-fast, umask state and DAG fleet cases under umask 002"
	@echo "  make test-t2-headless - Qualify remote execution with tmux absent on the receiver"
	@echo "  make test-t3-distributed - Qualify the two-host plan with tmux absent (plus loopback SSH)"
	@echo "  make build-core - Build the optional read-only native helper"
	@echo "  make build-tui - Build the optional native mission-control TUI"
	@echo "  make build-fleet - Build the optional fleet coordinator (needs JSON-C development files and pkg-config)"
	@echo "  make test-c    - Run native library unit tests"
	@echo "  make test-kettle - Run the kettle terminal library unit tests"
	@echo "  make test-fleet - Run fleet acceptance (TEST_JOBS workers, FLEET_SHARD=all|odd|even)"
	@echo "  make build-plan-precompile - Build native finite-example planner"
	@echo "  make test-tui  - Run deterministic native TUI acceptance"
	@echo "  make test-tui-pty - Run real pseudo-terminal safety and input acceptance"
	@echo "  make test-parity - Verify shell/native core parity and the control centre's rows from the state store"
	@echo "  make test-all  - Run shell/native, fleet, install, parity, and onboarding acceptance"
	@echo "  make sanitize  - Run the sanitizer lanes in BUILD_DIR/sanitize (SANITIZE_LANES=\"core tui ...\" selects)"
	@echo "  make sanitize-<lane> - One lane: core, tui, fleet, workspace, attached, plan-workspace or statistics"
	@echo "  make check-opt-levels - Compile every native source at -O0, -Og, -O1, -O2, -O3 and -Os with warnings as errors"
	@echo "  make quality-c - Run clang-tidy analysis and the complexity ceiling (test-quality-c checks the policy)"
	@echo "  make bench-core - Compare shell and native snapshot timings"
	@echo "  make bench-tui - Measure bounded native adapter/render at 5, 20, and 100 heads"
	@echo "  make bench     - Record list/status/doctor timings at 5 and 20 heads"
	@echo "  make perf-matrix - Run the item 10 matrix (PERF_MATRIX_ARGS=\"--output DIR ...\")"
	@echo "  make test-perf-matrix - Smallest matrix cells, attention corpus and schema checks"
	@echo "  make clean     - Remove editor temporary files and BUILD_DIR"
	@echo "  make install   - Install hydra to \$$PREFIX/bin (default /usr/local)"
	@echo "  make uninstall - Remove hydra from \$$PREFIX"
	@echo "  make test-install - Fresh-prefix install/uninstall tests"
	@echo "  make test-native-install - Offline native install, handshake, and rollback tests"
	@echo "  make package-core - Create a checksummed platform-qualified core artifact"
	@echo "  make package-tui - Create a checksummed platform-qualified native TUI artifact"
	@echo "  make build-fleet-static [ARCH=x86_64|aarch64] - Reproducible static Linux fleet helper (Docker)"
	@echo "  make check-fleet-assets - Validate the pinned release/fleet-assets.tsv digests table"
	@echo "  make smoke-onboarding - Throwaway-repo no-agent first-head smoke"
	@echo "  make visual-review [VISUAL_BASELINE=dir] - Capture, render and lint the native TUI screens into one contact sheet"
	@echo "  make test-visual-lint - Run the bounded layout lint over the visual-review captures"
	@echo "  make test-u10-loopback - Guided remote setup of a throwaway account over loopback SSH through the control centre"
	@echo "  make dev-setup - Set up development environment (git hooks)"
	@echo "  make help      - Show this help message"

# Optional fleet coordinator; JSON-C is statically linked into this executable.
FLEET_SOURCES = $(filter src/fleet/%.c,$(NATIVE_SOURCES))
FLEET_JSON_CFLAGS = $(shell pkg-config --cflags json-c)
FLEET_JSON_LIB = $(shell pkg-config --variable=libdir json-c)/libjson-c.a

.PHONY: build-fleet test-fleet test-workflow-contracts build-fleet-static check-fleet-assets
build-fleet: $(BUILD_DIR)/hydra-fleet
# Link flags for the helper only (the static release build passes -static).
FLEET_LDFLAGS ?=

# Reproducible static musl helper for a Linux release asset (Docker; CI uses
# the same script). Writes $(BUILD_DIR)/static/hydra-fleet-linux-$(ARCH).
ARCH ?= $(shell uname -m | sed -e 's/^arm64$$/aarch64/' -e 's/^amd64$$/x86_64/')
build-fleet-static:
	sh scripts/build-fleet-static.sh "$(ARCH)" "$(abspath $(BUILD_DIR))/static"

# Pinned release digests: header, row syntax, names and duplicates.
check-fleet-assets:
	@sh scripts/check-fleet-assets.sh release/fleet-assets.tsv

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
FLEET_TEST_BINS = $(addprefix $(BUILD_DIR)/test-,fleet task-package task-result workflow-data workflow-schedule workflow-drive agent-profile agent-auth plan plan-expansion remote-setup agent-recipes fleet-assets placement)

$(BUILD_DIR)/fleet/%.o: src/fleet/%.c
	@mkdir -p "$(@D)"
	$(CC) $(CORE_CFLAGS) $(FLEET_JSON_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/libhydra-fleet.a: $(FLEET_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(FLEET_OBJECTS)

$(BUILD_DIR)/hydra-fleet: $(BUILD_DIR)/fleet/main.o $(BUILD_DIR)/libhydra-fleet.a
	$(CC) $(CORE_CFLAGS) $(FLEET_LDFLAGS) $^ $(FLEET_JSON_LIB) -lm -o $@

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
$(BUILD_DIR)/test-plan-expansion: tests/c/test_plan_expansion.c tests/c/test_plan_expansion_cases.inc
$(BUILD_DIR)/test-workflow-schedule: tests/c/test_workflow_schedule.c
$(BUILD_DIR)/test-workflow-drive: tests/c/test_workflow_drive.c
$(BUILD_DIR)/test-remote-setup: tests/c/test_remote_setup.c
$(BUILD_DIR)/test-agent-recipes: tests/c/test_agent_recipes.c
$(BUILD_DIR)/test-fleet-assets: tests/c/test_fleet_assets.c
$(BUILD_DIR)/test-placement: tests/c/test_placement.c

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
	HYDRA_TEST_PLAN_REPAIR=combine HYDRA_TEST_PLAN_REUSE=1 HYDRA_TEST_PLAN_REPAIR_FAULT=1 BUILD_DIR="$(abspath $(BUILD_DIR))" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" sh tests/test_workflow_plan_task.sh

test-retention: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-retention"

test-workflow-metrics: build-fleet build-core
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-workflow-task-metrics"
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-workflow-task-cohorts"
	"$(BUILD_DIR)/native-tests/test-statistics-export" "$(abspath $(BUILD_DIR))/hydra-core"

# Bounded expansions (roadmap 9F): native admission cases over fabricated run
# records, then the public CLI over real base and round runs.
.PHONY: test-plan-expansion
test-plan-expansion: build-fleet $(BUILD_DIR)/test-plan-expansion $(SHELL_TEST_PREREQS)
	$(BUILD_DIR)/test-plan-expansion
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" plan-expansion tests/test_workflow_plan_expansion.sh

test-plan-staged: build-fleet build-plan-precompile
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-staged"

test-plan-staged-public: build-fleet build-plan-precompile
	HYDRA_PLAN_EXAMPLE_BIN="$(abspath $(BUILD_DIR))/plan-example" HYDRA_PLAN_PRECOMPILE_BIN="$(abspath $(BUILD_DIR))/plan-precompile" "$(BUILD_DIR)/native-tests/test-plan-staged-public" --fleet "$(abspath $(BUILD_DIR))/hydra-fleet" --output "$(BUILD_DIR)/staged-public.json"

# Planning-quality evaluation (roadmap 9E): archived pilot cases plus held-out
# additions through the independent oracle, one executed run per arm and the
# serial/fork-join scheduling patterns. The report lands in build/plan-eval/.
.PHONY: test-plan-eval
test-plan-eval: build-fleet build-plan-example
	python3 scripts/plan-eval.py --execute 1 --fleet "$(abspath $(BUILD_DIR))/hydra-fleet" --plan-example "$(abspath $(BUILD_DIR))/plan-example" --output "$(abspath $(BUILD_DIR))/plan-eval/report.json"

.PHONY: test-plan-inspection
test-plan-inspection: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-inspection"
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-plan-evaluate"


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
	@sh scripts/quality-c.sh docs/quality/cognitive-complexity.tsv $(CLANG_TIDY) $(NATIVE_SOURCES) $(wildcard tests/c/*.c tests/native/*.c tests/fixture/*.c tests/kettle/*.c examples/planning/native/*.c examples/planning/feature/*.c) -- $(QUALITY_C_FLAGS)

test-quality-c:
	@sh tests/quality_c_cases.sh "$(CLANG_TIDY)"

# Compile-only check of every native source at each optimisation level. Some
# warnings (uninitialised use, truncation, bounds) depend on the optimiser, and
# sanitizer, debug and packager builds do not use the default -O2. Warnings are
# always errors here. Nothing is linked or kept: a unit leaves only the
# diagnostics of a failed compile under $(BUILD_DIR)/opt-levels, and every
# failure is reported before the target fails. Run it with -j.
OPT_LEVELS ?= O0 Og O1 O2 O3 Os
OPT_CHECK_DIR = $(BUILD_DIR)/opt-levels
OPT_CHECK_UNITS = $(foreach level,$(OPT_LEVELS),$(addprefix opt-check/$(level)/,$(NATIVE_SOURCES)))
# A unit is opt-check/<level>/<source>.
OPT_CHECK_LEVEL = $(word 2,$(subst /, ,$@))
OPT_CHECK_SOURCE = $(patsubst opt-check/$(OPT_CHECK_LEVEL)/%,%,$@)
.PHONY: check-opt-levels opt-check-reset $(OPT_CHECK_UNITS)
# Diagnostics left by an earlier run must not be reported again.
opt-check-reset:
	@mkdir -p "$(OPT_CHECK_DIR)"
	@find "$(OPT_CHECK_DIR)" -type f -name '*.err' -exec rm -f {} +

$(OPT_CHECK_UNITS): | opt-check-reset
	@out="$(OPT_CHECK_DIR)/$(OPT_CHECK_LEVEL)/$(OPT_CHECK_SOURCE)"; mkdir -p "$${out%/*}"; \
	if $(CC) $(CORE_CFLAGS) -Werror $(FLEET_JSON_CFLAGS) -$(OPT_CHECK_LEVEL) -c $(OPT_CHECK_SOURCE) -o "$$out.o" 2>"$$out.err"; then \
		rm -f "$$out.err"; \
	fi; \
	rm -f "$$out.o"

check-opt-levels: $(OPT_CHECK_UNITS)
	@failed="$$(find "$(OPT_CHECK_DIR)" -type f -name '*.err' | LC_ALL=C sort)"; \
	if [ -z "$$failed" ]; then \
		echo "check-opt-levels: $(words $(NATIVE_SOURCES)) sources compile without warnings at $(addprefix -,$(OPT_LEVELS))"; \
		exit 0; \
	fi; \
	for log in $$failed; do \
		unit="$${log#$(OPT_CHECK_DIR)/}"; unit="$${unit%.err}"; \
		echo "== -$${unit%%/*} $${unit#*/}"; cat "$$log"; \
	done; \
	echo "check-opt-levels: failing sites ($(CC), warnings as errors):"; \
	for log in $$failed; do \
		unit="$${log#$(OPT_CHECK_DIR)/}"; unit="$${unit%.err}"; \
		grep -E '^[^ :]+:[0-9]+:[0-9]+: (fatal error|error|warning):' "$$log" | \
			awk -v level="$${unit%%/*}" -v source="$${unit#*/}" ' \
				{ found = 1; printf "  -%s %s%s\n", level, (index($$0, source ":") == 1 ? "" : source ": in "), $$0 } \
				END { if (!found) printf "  -%s %s: failed without a located diagnostic (see above)\n", level, source }'; \
	done; \
	exit 1

.PHONY: test-attached-pty
test-attached-pty: build-tui
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-attached-pty"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-attach-cycles"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-output-fidelity"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-terminal-restore"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-interactive-loop"

.PHONY: test-kettle-export
test-kettle-export:
	sh tests/kettle/test_export.sh

.PHONY: test-plan-workspace
test-plan-workspace: build-tui build-fleet
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-plan-workspace"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-plan-launch"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-plan-conversation"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-plan-assets"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-workflow-controls"
	BUILD_DIR="$(abspath $(BUILD_DIR))" "$(BUILD_DIR)/native-tests/pty-runs-heads"
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

# Host key trust and preflight against a private loopback sshd (skips without one).
.PHONY: test-remote-setup-ssh
test-remote-setup-ssh: build-fleet
	HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" "$(BUILD_DIR)/native-tests/test-remote-setup-ssh"

# T2/T3 qualification with tmux absent on the execution hosts (the coordinator
# keeps its PATH). See docs/T2_T3_QUALIFICATION.md. The loopback SSH variant
# prints SKIP and exits 0 without an OpenSSH server, like test-remote-setup-ssh.
.PHONY: test-t2-headless test-t3-distributed test-t3-distributed-ssh
test-t2-headless: fleet-test-build
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" t2-headless tests/test_t2_headless.sh
test-t3-distributed: fleet-test-build
	@BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" t3-distributed tests/test_t3_distributed.sh
	@$(MAKE) test-t3-distributed-ssh
test-t3-distributed-ssh: fleet-test-build
	@HYDRA_TEST_T3_TRANSPORT=ssh BUILD_DIR="$(abspath $(BUILD_DIR))" sh scripts/run-shell-test.sh "$(BUILD_DIR)/test-logs" t3-distributed-ssh tests/test_t3_distributed.sh
	@grep -E '^(SKIP T3 distributed SSH|T3 distributed over loopback SSH passed)' "$(BUILD_DIR)/test-logs/t3-distributed-ssh.log"

include scripts/native-tests.mk

include scripts/fleet-tests.mk
include scripts/shell-tests.mk

.PHONY: test-usability
USABILITY_OUTPUT ?= $(BUILD_DIR)/usability-$(shell date +%Y%m%d-%H%M%S)
test-usability: build-core build-tui build-fleet $(BUILD_DIR)/test-tui-pty
	PYTHONPATH=scripts python3 -m usability --output "$(USABILITY_OUTPUT)" $(USABILITY_ARGS)

# Replays captured terminal bytes through the product's own terminal
# interpreter (src/kettle) and prints exact cells; see docs/VISUAL-REVIEW.md.
$(BUILD_DIR)/kettle-cells: src/kettle/tools/cells.c $(KETTLE_OBJECTS) src/kettle/terminal.h
	$(CC) $(CORE_CFLAGS) src/kettle/tools/cells.c $(KETTLE_OBJECTS) -o $@

# Visual review without manual runs (U4, I4, 2.9.0 Hosts screens): drives the
# prefix-installed native TUI over a PTY through fixture states at 80x24 and
# 140x40 in the dark, light and no-colour themes, renders each capture to
# HTML/SVG with exact colours, lints the layouts and writes one contact sheet
# to $(VISUAL_REVIEW_OUTPUT)/index.html. VISUAL_BASELINE=dir adds before/after
# columns from an earlier output directory.
VISUAL_REVIEW_OUTPUT ?= $(BUILD_DIR)/visual-review
.PHONY: visual-review test-visual-lint
visual-review: build-core build-tui build-fleet $(BUILD_DIR)/kettle-cells
	python3 scripts/visual_review/run.py --output "$(abspath $(VISUAL_REVIEW_OUTPUT))" \
		--cells "$(abspath $(BUILD_DIR))/kettle-cells" $(if $(VISUAL_BASELINE),--baseline "$(VISUAL_BASELINE)")

# The bounded, deterministic layout lint over the captures (captures them
# first when the output directory has none).
test-visual-lint: $(BUILD_DIR)/kettle-cells
	@if [ ! -f "$(VISUAL_REVIEW_OUTPUT)/manifest.json" ]; then $(MAKE) visual-review VISUAL_REVIEW_OUTPUT="$(VISUAL_REVIEW_OUTPUT)"; fi
	python3 scripts/visual_review/lint.py "$(abspath $(VISUAL_REVIEW_OUTPUT))"

# U10 loopback acceptance: a private sshd on a loopback port serves a fresh
# throwaway account (root or sudo -n needed; otherwise the case prints SKIP),
# and the control centre's Hosts tab is driven over a PTY through the whole
# guided setup, a real fixture-agent task, reconnection and the existing
# installation, missing prerequisites, failed sign-in and interrupted cases.
.PHONY: test-u10-loopback
test-u10-loopback: build-fleet build-tui build-core $(BUILD_DIR)/kettle-cells
	@BUILD_DIR="$(abspath $(BUILD_DIR))" HYDRA_FLEET_BIN="$(abspath $(BUILD_DIR))/hydra-fleet" \
		HYDRA_TUI_BIN="$(abspath $(BUILD_DIR))/hydra-tui" sh tests/test_u10_loopback.sh

# Installed-build acceptance for the open roadmap items (docs/ACCEPTANCE.md):
# one HTML+JSON matrix mapping each acceptance bullet to its checks and evidence.
.PHONY: test-acceptance
ACCEPTANCE_OUTPUT ?= $(BUILD_DIR)/acceptance-$(shell date +%Y%m%d-%H%M%S)
test-acceptance: build-core build-tui build-fleet $(BUILD_DIR)/test-tui-pty
	PYTHONPATH=scripts python3 -m acceptance --output "$(ACCEPTANCE_OUTPUT)" $(ACCEPTANCE_ARGS)
