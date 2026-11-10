# Standalone extraction build. No Hydra sources or runtime are required.
CC = cc
AR = ar
CFLAGS = -O2
CPPFLAGS =
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
INCLUDES = -Isrc -Isrc/kettle
CORE = canvas plot graph present workspace unicode output input tree terminal_screen terminal_csi terminal_sgr terminal_parser
SOURCES = $(addprefix src/kettle/,$(addsuffix .c,$(CORE)))
OBJECTS = $(addprefix build/,$(addsuffix .o,$(CORE)))
TESTS = test_kettle test_kettle_present test_kettle_workspace test_kettle_unicode test_kettle_terminal test_kettle_input
TEST_BINS = $(addprefix build/,$(TESTS))
HEADERS = $(wildcard src/kettle/*.h src/kettle/*.inc)

.PHONY: all examples test test-pty clean
all: build/libkettle.a
build:
	mkdir -p $@
build/%.o: src/kettle/%.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) -c $< -o $@
build/libkettle.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)
build/test_%: tests/c/test_%.c build/libkettle.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< build/libkettle.a -o $@
build/kettle-example: examples/kettle.c build/libkettle.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< build/libkettle.a -o $@
build/kettle-workspace: examples/workspace.c examples/workspace_view.c examples/workspace_input.c examples/workspace_demo.h src/kettle/terminal_posix.c src/kettle/pty_posix.c build/libkettle.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) examples/workspace.c examples/workspace_view.c examples/workspace_input.c src/kettle/terminal_posix.c src/kettle/pty_posix.c build/libkettle.a -o $@
build/test-workspace-child: tests/c/test_workspace_child.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< -o $@
examples: build/kettle-example build/kettle-workspace
test: $(TEST_BINS)
	@set -e; for test in $(TEST_BINS); do "$$test"; done
build/test-pty: tests/kettle/test_pty.c tests/kettle/pty_support.c tests/kettle/screen_support.c tests/kettle/fixture_support.c tests/kettle/pty_support.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(filter %.c,$^) -o $@
test-pty: examples build/test-workspace-child build/test-pty
	build/test-pty --standalone
clean:
	rm -rf build
