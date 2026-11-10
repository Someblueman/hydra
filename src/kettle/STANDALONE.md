# kettle standalone source

This local extraction contains the C99 library, examples and their tests. It has
no Hydra runtime, configuration, Git, tmux, JSON-C or package-manager dependency.
The library remains an unpublished API under development. No semantic-version or
binary compatibility guarantee is claimed; source consumers must rebuild and
qualify when updating. Public structs expose caller-owned storage, so changes to
their layout can affect both source and binary compatibility.

```
make                    # portable C99 static library, no POSIX layer
make runtime            # also the optional POSIX runtime and its test driver
make test               # component suites, the runtime, the counter example's test
make examples           # chart, counter and workspace examples
make test-pty           # the runtime and a real shell on actual PTYs, native C observer
build/kettle-counter
build/kettle-workspace --shell
```

`CC`, `AR`, `CFLAGS` and `CPPFLAGS` can be overridden on the make command line.
The build requires GNU make. The POSIX examples need a terminal and the host PTY
interfaces. The PTY tests use a C compiler and `ps` (procps on Debian); no Python
interpreter or JSON-C library is required.
Linux owned-session cleanup uses `/proc`; deliberately detached sessions are
outside the adapter's ownership. `make clean` removes this extracted
tree's `build` directory.

## Ownership and compatibility

- [Core API](kettle.h): the caller allocates cells and presenter history.
  Views borrow storage and must not outlive it. Rendering does not allocate or
  perform OS access; output functions write to the supplied stream.
- [Layout](workspace.h): a caller-owned value object. Pane IDs remain
  stable until reinitialization. The caller owns labels, domain data and actions.
- [Input](input.h) and [key bindings](keymap.h): the caller owns the decoder,
  the binding table and the chord state. Key events carry the exact bytes
  received. Bindings hold pointers to caller strings, which must outlive them.
- [Runtime](program.h): optional, POSIX. It owns the event loop, the
  terminal's modes, signal handlers while it runs, the frame buffers and the
  child processes it starts; the application owns its model and whatever its
  `update` opens. Memory comes from the allocator in `kt_options`. Its
  [test driver](testing.h) runs the same loop without a terminal.
- [Terminal](terminal.h): the caller supplies disjoint primary,
  alternate and history buffers and feeds it the child's output.
  [Protocol limits](TERMINAL.md) are explicit; this is a bounded terminal
  subset, not a general replacement for an established terminal emulator.
- The [PTY](pty_posix.h) and [terminal size](posix.h) adapters are
  optional. The application owns a PTY child's lifecycle and cleanup.
  The example closes its owned child; an application attaching to an external
  session must distinguish its client process from the external execution owner.

The source is MIT licensed under [LICENSE](../../LICENSE). Preserve its copyright and
permission notice when copying or redistributing. The checked-in Unicode tables
carry their separate [Unicode license](UNICODE-LICENSE.txt); preserve it
and the provenance in `unicode_tables.inc`. Standard C/POSIX interfaces introduce
no bundled third-party runtime. Compiler/toolchain licensing remains the user's
installation's responsibility.

Local runtime qualification is recorded in the Hydra repository's acceptance
record. macOS arm64 (Apple Clang 17, UBSan) and Linux aarch64
(Debian GCC 12.2, ASan/UBSan) passed component and real PTY suites. Linux was
tested in a Debian Bookworm container on LinuxKit 6.12.67. Other systems remain
unqualified until their component and PTY suites run there. Windows has no
provided process/terminal adapter. A portable-core build alone does not qualify a
platform's display, terminal driver or process lifecycle.

Exporting this source does not create a Git repository or publish a package.
