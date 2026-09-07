# termviz

A small C99 visualization module: cell canvas, clipped panels and text, fractional
bars, Braille/ASCII plots, and stable layered DAG layout. It has no Hydra data
types, storage paths, subprocesses, clock, terminal modes, input handling, or heap
allocation. The application owns storage and provides an optional style callback
when writing rows.

This is an internal module, not a published stable API. Its header documents
ownership, limits, chart semantics, and failure behavior.

## Standalone build

From the repository root:

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -Isrc/termviz \
  examples/termviz.c src/termviz/canvas.c src/termviz/plot.c \
  src/termviz/graph.c -o /tmp/termviz-example
/tmp/termviz-example
/tmp/termviz-example --ascii
```

The example uses explicitly synthetic data and needs no Hydra installation.
`make test-termviz example-termviz` builds the same sources and boundary tests.

## Extraction boundary

To start a standalone repository, copy this directory, `examples/termviz.c`, and
`tests/c/test_termviz.c`; retain the applicable Hydra license and attribution.
Adjust the test's include path from `termviz/termviz.h` to `termviz.h` or preserve
the directory layout. Build the three C translation units directly. No generator,
Hydra configuration, private module, JSON library, or terminal library is needed.
The root Makefile targets are convenient wrappers, not runtime dependencies.

Hydra-specific aggregation, freshness, selection, and actions belong in Hydra's
adapter and dashboard code. They must not migrate into this library. A future
terminal backend can consume the same canvas cells without taking ownership of
the application's event loop.

## Current limits

Canvas dimensions are bounded to 4096 cells in each direction and additionally
by caller-provided storage. DAGs allow 128 nodes and 512 edges. Graph layout uses
fixed-size nodes and a deterministic layered ordering; dense edge crossings are
not minimized. Drawing clips to the viewport. Plot samples are equally spaced;
the caller labels them as samples unless it supplies an actual regular time grid.

Untrusted text is printable ASCII with replacement characters. The Unicode mode
adds generated single-cell borders, block elements, and Braille plots; it does
not claim general Unicode text shaping. ANSI styling is delegated to the caller.
Missing samples and zero samples have different representations.
