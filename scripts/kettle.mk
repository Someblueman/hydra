# Standalone extraction build. No Hydra sources or runtime are required.
CC = cc
AR = ar
CFLAGS = -O2
CPPFLAGS =
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
INCLUDES = -Isrc -Isrc/kettle
CORE = canvas color theme draw layout plot graph present workspace unicode output input keymap tree terminal_screen terminal_csi terminal_sgr terminal_parser
# The optional POSIX runtime (program.h) and its headless test driver (testing.h).
RUNTIME = program program_frame program_child program_posix testing
# Components (components.h): built on the runtime's messages and commands.
COMPONENTS = component fuzzy help textinput list
SOURCES = $(addprefix src/kettle/,$(addsuffix .c,$(CORE)))
OBJECTS = $(addprefix build/,$(addsuffix .o,$(CORE)))
RUNTIME_OBJECTS = $(addprefix build/,$(addsuffix .o,$(RUNTIME)))
COMPONENT_OBJECTS = $(addprefix build/,$(addsuffix .o,$(COMPONENTS)))
LIBRARIES = build/libkettle-components.a build/libkettle-program.a build/libkettle.a
TESTS = test_kettle test_kettle_present test_kettle_style test_kettle_draw test_kettle_workspace test_kettle_unicode test_kettle_terminal test_kettle_input test_kettle_keymap test_kettle_program test_kettle_counter test_kettle_themes test_kettle_components test_kettle_widgets
TEST_BINS = $(addprefix build/,$(TESTS))
HEADERS = $(wildcard src/kettle/*.h src/kettle/*.inc)
PTY_SUPPORT = tests/kettle/pty_support.c tests/kettle/screen_support.c tests/kettle/fixture_support.c

.PHONY: all runtime examples test test-pty clean
all: build/libkettle.a
runtime: $(LIBRARIES)
build:
	mkdir -p $@
build/%.o: src/kettle/%.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) -c $< -o $@
build/libkettle.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)
build/libkettle-program.a: $(RUNTIME_OBJECTS)
	$(AR) rcs $@ $(RUNTIME_OBJECTS)
build/libkettle-components.a: $(COMPONENT_OBJECTS)
	$(AR) rcs $@ $(COMPONENT_OBJECTS)
build/test_%: tests/c/test_%.c build/libkettle.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< build/libkettle.a -o $@
build/test_kettle_program: tests/c/test_kettle_program.c $(LIBRARIES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/test_kettle_components: tests/c/test_kettle_components.c $(LIBRARIES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/test_kettle_widgets: tests/c/test_kettle_widgets.c $(LIBRARIES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/test_kettle_counter: examples/counter_test.c examples/counter.c $(LIBRARIES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/test_kettle_themes: examples/themes_test.c examples/themes.c $(LIBRARIES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/kettle-example: examples/kettle.c build/libkettle.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< build/libkettle.a -o $@
build/kettle-counter: examples/counter.c $(LIBRARIES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/kettle-themes: examples/themes.c $(LIBRARIES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
build/kettle-workspace: examples/workspace.c examples/workspace_view.c examples/workspace_input.c examples/workspace_demo.h src/kettle/pty_posix.c $(LIBRARIES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) examples/workspace.c examples/workspace_view.c examples/workspace_input.c src/kettle/pty_posix.c $(LIBRARIES) -o $@
build/test-workspace-child: tests/c/test_workspace_child.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< -o $@
build/kettle-program-fixture: tests/kettle/program_fixture.c $(LIBRARIES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARIES) -o $@
examples: build/kettle-example build/kettle-counter build/kettle-themes build/kettle-workspace
test: $(TEST_BINS)
	@set -e; for test in $(TEST_BINS); do "$$test"; done
build/test-pty: tests/kettle/test_pty.c $(PTY_SUPPORT) tests/kettle/pty_support.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(filter %.c,$^) -o $@
build/test-program-pty: tests/kettle/test_program_pty.c $(PTY_SUPPORT) tests/kettle/pty_support.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(filter %.c,$^) -o $@
test-pty: examples build/test-workspace-child build/test-pty build/kettle-program-fixture build/test-program-pty
	build/test-program-pty
	build/test-pty --standalone
clean:
	rm -rf build
