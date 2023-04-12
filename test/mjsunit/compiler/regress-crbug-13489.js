// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-fast-api

const fast_c_api = new d8.test.FastCAPI();

function is_null_buffer(uint8_ta) {
  return fast_c_api.is_null_buffer(uint8_ta);
}

%PrepareFunctionForOptimization(is_null_buffer);
const slow_result = is_null_buffer(new Uint8Array());
%OptimizeFunctionOnNextCall(is_null_buffer);
const fast_result = is_null_buffer(new Uint8Array());
if (slow_result !== fast_result) {
  throw new Error("Fast call received different value");
}
