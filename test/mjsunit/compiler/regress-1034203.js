// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --opt --allow-natives-syntax

function foo(optimized) {
  class C {
    ['h']() { return 42; }
  }
  let h = C.prototype.h;
  let val = h.bind()();
  if (optimized) {
    %TurbofanStaticAssert(val === 42);
  }
}

%PrepareFunctionForOptimization(foo);
foo(false);
%OptimizeFunctionOnNextCall(foo);
foo(true);
