// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-fast-api --turbofan

console.profile();
const fast_c_api = new d8.test.FastCAPI();
const obj = { valueOf: d8.profiler.triggerSample };

function run() {
  fast_c_api.call_to_number(obj);
}

%PrepareFunctionForOptimization(run);
run();
run();
%OptimizeFunctionOnNextCall(run);
run();
