// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --validate-asm --allow-natives-syntax --turbofan

function Module() {
  "use asm";
  return {};
}

%PrepareFunctionForOptimization(Module);
var m = Module();
%OptimizeFunctionOnNextCall(Module);
m = Module();
