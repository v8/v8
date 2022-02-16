// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function A() {
  this.x = 0;
  for (var i = 0; i < max; ) {}
}
%EnsureFeedbackVectorForFunction(A);
function foo() {
  for (var i = 0; i < 2; i++) %OptimizeOsr(0, "concurrent");
  return new A();
}
%PrepareFunctionForOptimization(foo);
try { foo(); } catch (e) { }
