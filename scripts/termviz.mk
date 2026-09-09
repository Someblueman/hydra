# Standalone extraction build. No Hydra sources or runtime are required.
CC = cc
AR = ar
CFLAGS = -O2
CPPFLAGS =
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
INCLUDES = -Isrc -Isrc/termviz
CORE = canvas plot graph present workspace unicode output input tree terminal_screen terminal_csi terminal_sgr terminal_parser
SOURCES = $(addprefix src/termviz/,$(addsuffix .c,$(CORE)))
OBJECTS = $(addprefix build/,$(addsuffix .o,$(CORE)))
TESTS = test_termviz test_termviz_present test_termviz_workspace test_termviz_unicode test_termviz_terminal test_termviz_input
TEST_BINS = $(addprefix build/,$(TESTS))
HEADERS = $(wildcard src/termviz/*.h src/termviz/*.inc)

.PHONY: all examples test test-pty clean
all: build/libtermviz.a
build:
	mkdir -p $@
build/%.o: src/termviz/%.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) -c $< -o $@
build/libtermviz.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)
build/test_%: tests/c/test_%.c build/libtermviz.a $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< build/libtermviz.a -o $@
build/termviz-example: examples/termviz.c build/libtermviz.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $< build/libtermviz.a -o $@
build/termviz-workspace: examples/workspace.c examples/workspace_view.c examples/workspace_input.c examples/workspace_demo.h src/termviz/terminal_posix.c src/termviz/pty_posix.c build/libtermviz.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) examples/workspace.c examples/workspace_view.c examples/workspace_input.c src/termviz/terminal_posix.c src/termviz/pty_posix.c build/libtermviz.a -o $@
build/test-workspace-child: tests/c/test_workspace_child.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< -o $@
examples: build/termviz-example build/termviz-workspace
test: $(TEST_BINS)
	@set -e; for test in $(TEST_BINS); do "$$test"; done
test-pty: examples build/test-workspace-child
	python3 -c 'import sys; sys.path.insert(0,"tests/termviz"); from test_pty import workspace,shell,interruption; workspace(); shell(); interruption()'
clean:
	rm -rf build
