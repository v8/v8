// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-frontend

function simple1() {
  return 0;
}

%PrepareFunctionForOptimization(simple1);
simple1();
%OptimizeFunctionOnNextCall(simple1);
simple1();
