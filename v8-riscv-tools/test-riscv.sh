#!/bin/sh

SCRIPT=$(readlink -f "$0")
DIR=$(dirname "$SCRIPT")
$DIR/../tools/run-tests.py --outdir=out/riscv64.sim cctest \
                                                    unittests \
                                                    wasm-api-tests \
                                                    mjsunit \
                                                    intl \
                                                    message \
                                                    debugger \
                                                    inspector \
                                                    mkgrokdump
