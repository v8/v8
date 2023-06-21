// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function callback() {}

function foo(x) {
  // array.forEach with an untagged `this` arg should compile successfully.
  x = x << 1;
  [""].forEach(callback, x);
}

%PrepareFunctionForOptimization(foo);
foo(0);
foo(0);
%OptimizeFunctionOnNextCall(foo);
foo(0);
