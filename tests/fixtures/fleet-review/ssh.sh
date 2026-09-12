#!/bin/sh
# Local receiver only. No connection to an SSH host is ever made.
set -eu
while [ "$#" -gt 2 ]; do shift; done
if [ -f "$HYDRA_REVIEW_FIXTURE/offline" ]; then exit 255; fi
request="$(cat)"
if [ -f "$HYDRA_REVIEW_FIXTURE/malformed" ]; then printf '{malformed\n'; exit 0; fi
printf '%s' "$request" | /bin/sh -c "$2"
