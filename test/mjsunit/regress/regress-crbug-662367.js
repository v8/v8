// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --fold-constants

var zero = 0;

function f() {
  return 0 / zero;
}
assertTrue(isNaN(f()));
assertTrue(isNaN(f()));
%OptimizeFunctionOnNextCall(f);
assertTrue(isNaN(f()));

function g() {
  return -0 / zero;
}
assertTrue(isNaN(g()));
assertTrue(isNaN(g()));
%OptimizeFunctionOnNextCall(g);
assertTrue(isNaN(g()));
