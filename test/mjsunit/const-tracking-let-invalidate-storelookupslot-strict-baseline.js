// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --script-context-cells --allow-natives-syntax
// Flags: --turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let a = 0;

function read() {
  return a;
}

%PrepareFunctionForOptimization(read);
read();
%OptimizeFunctionOnNextCall(read);
read();
assertOptimized(read);

// This will use the StoreLookupSlot bytecode.
function write(newA) {
  eval();
  function inner() {
    'use strict';
    a = newA;
  }
  inner();
}

%PrepareFunctionForOptimization(write);
%CompileBaseline(write);

write(1);
assertUnoptimized(read);
assertEquals(1, read());
