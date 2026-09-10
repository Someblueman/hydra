#!/bin/sh
set -eu
cat > slug.c <<'SRC'
#include "slug.h"
int slugify(const char *input, char *output, size_t capacity) {
 size_t i=0, w=0; int pending=0; if (output && capacity) output[0]='\0';
 if (!input || !output || !capacity) return -1;
 while (input[i]) { unsigned char c=(unsigned char)input[i++]; int word=((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')); if (word) { if (pending && w) { if (w+1>=capacity) return output[0]='\0',-1; output[w++]='-'; } pending=0; if (w+1>=capacity) return output[0]='\0',-1; output[w++]=(c>='A'&&c<='Z')?(char)(c-'A'+'a'):(char)c; } else if (w) pending=1; }
 output[w]='\0'; return 0;
}
SRC
make
mkdir -p "$HYDRA_WORKFLOW_OUTPUTS_DIR"
COPYFILE_DISABLE=1 tar -cf "$HYDRA_WORKFLOW_OUTPUTS_DIR/catalog.tar" main.c slug.c slug.h Makefile
