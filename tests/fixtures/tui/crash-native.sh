#!/bin/sh

stty -echo -icanon min 0 time 1
kill -SEGV "$$"
