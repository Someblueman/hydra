#!/bin/sh
set -eu
cat "$HYDRA_TASK_INPUT_DIR/subject" > result.txt
printf 'composed\n' >> result.txt
