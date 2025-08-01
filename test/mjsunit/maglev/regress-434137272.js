// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo(c) {
  return new c();
}
%PrepareFunctionForOptimization(foo);
foo(String);
%OptimizeMaglevOnNextCall(foo);
assertThrows(() => { foo(); });
