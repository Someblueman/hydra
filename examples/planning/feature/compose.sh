#!/bin/sh
set -eu
# Composition supplies the fixed interface and a complete final source line.
{
    printf '#include "slug.h"\n'
    cat "$HYDRA_WORKFLOW_INPUTS_DIR/implementation"
    printf '\n'
} > slug.c
make
test "$(./catalog-slug 'Hydra CATALOG 42')" = hydra-catalog-42
tar -cf "$HYDRA_WORKFLOW_OUTPUTS_DIR/catalog.tar" main.c slug.c slug.h Makefile
