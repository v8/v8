// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function set_named_generic() {
  let iterator = new Set().values();
  iterator.x = 0;
  return iterator;
}

%PrepareFunctionForOptimization(set_named_generic);
let before = set_named_generic();
%OptimizeFunctionOnNextCall(set_named_generic);
let after = set_named_generic();
assertEquals(before, after);
assertOptimized(set_named_generic);
