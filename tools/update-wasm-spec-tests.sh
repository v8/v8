#!/bin/bash
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

TOOLS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd ${TOOLS_DIR}/..

mkdir -p ./test/wasm-spec-tests/tests/
rm -rf ./test/wasm-spec-tests/tests/*

./tools/dev/gm.py x64.release all

cd ${TOOLS_DIR}/../test/wasm-js/interpreter
make

cd ${TOOLS_DIR}/../test/wasm-js/test/core

./run.py --wasm ${TOOLS_DIR}/../test/wasm-js/interpreter/wasm --js ${TOOLS_DIR}/../out/x64.release/d8

cp ${TOOLS_DIR}/../test/wasm-js/test/core/output/*.js ${TOOLS_DIR}/../test/wasm-spec-tests/tests

cd ${TOOLS_DIR}/../test/wasm-spec-tests
upload_to_google_storage.py -a -b v8-wasm-spec-tests tests


