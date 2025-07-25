// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

function bar(x, y){
  return [...x, y]
}
function foo(x) {
  return bar`123${x}`
}

%PrepareFunctionForOptimization(foo);
assertEquals(["123", "", 1], foo(1));
assertEquals(["123", "", 2], foo(2));
%OptimizeMaglevOnNextCall(foo);
assertEquals(["123", "", 1], foo(1));
assertEquals(["123", "", 2], foo(2));
assertTrue(isMaglevved(foo));
