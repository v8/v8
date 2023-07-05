// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft --turboshaft-instruction-selection

function simple0() { return 42; }
%PrepareFunctionForOptimization(simple0);
assertEquals(42, simple0());
%OptimizeFunctionOnNextCall(simple0);
assertEquals(42, simple0());

function simple1(x, y) { return x + y * x - y / x; }
%PrepareFunctionForOptimization(simple1);
assertEquals(31.625, simple1(8, 3));
%OptimizeFunctionOnNextCall(simple1);
assertEquals(31.625, simple1(8, 3));
