// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

"use strict";

function inner() {
  new Error();
}

function outer(cond) {
  let val = cond ? 3.14 : 2.72;
  val * 2.5; // Float64 use
  return inner.call(val);
}

%PrepareFunctionForOptimization(inner);
%PrepareFunctionForOptimization(outer);
outer(1);

%OptimizeMaglevOnNextCall(outer);
outer(1);
