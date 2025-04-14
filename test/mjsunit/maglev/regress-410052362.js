// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

const ta = new Uint32Array();
function foo(a, b) {
  Object.defineProperty(a, 98, { value: 0 });
  ({'buffer':b, 'length':b,} = ta);
}
%PrepareFunctionForOptimization(foo);
try {
  foo();
} catch {
}
for (let v11 = 0; v11 < 2500; v11++) {
  foo(Int8Array);
}
