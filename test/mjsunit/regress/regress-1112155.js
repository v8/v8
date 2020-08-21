// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --dynamic-map-checks --allow-natives-syntax --opt --no-always-opt

function f(v) {
  return v.b;
}
var v = { a: 10, b: 10.23 };
%PrepareFunctionForOptimization(f);
f(v);
%OptimizeFunctionOnNextCall(f);
f(v);
assertOptimized(f);
v.b = {x: 20};
assertEquals(f(v).x, 20);
// Must deoptimize because of field-rep changes for field 'b'
assertUnoptimized(f);
