#!/bin/sh
objdump --demangle -t obj/debug/$1 | \
    grep '\.bss\|\.data\|\.rodata' | \
    grep -x -v -f tools/statics.whitelist
