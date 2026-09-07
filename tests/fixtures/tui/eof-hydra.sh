#!/bin/sh
# A child that stops producing output but does not exit.
exec 1>&-
sleep 10
