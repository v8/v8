// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc

function g(b) {
  if (b) gc();
}

function f(b) {
  let a = {};
  a.a = 1;
  g(b);
  a.b = 2;
  return a;
}

%PrepareFunctionForOptimization(f);
%PrepareFunctionForOptimization(g);
for (let i = 0; i < 100; i++) f(false);
%OptimizeMaglevOnNextCall(f);
f(true);
