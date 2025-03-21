// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

// Original: regress/large_loops/input.js
var __v_0 = 0;
var __v_1 = 0;
for (const __v_2 = 0; __v_2 < 10000; __v_2++) {
  console.log();
}
for (const __v_3 = 0; 1e5 >= __v_3; __v_3--) {
  console.log();
}
for (const __v_4 = -10000; __v_4 < 0; __v_4++) {
  console.log();
}
for (const __v_5 = 0n; __v_5 < 10000n; __v_5++) {
  console.log();
}
for (const __v_6 = -0.3; __v_6 < 1000.1; __v_6 += 0.5) {
  /* VariableOrObjectMutator: Random mutation */
  __v_6[__getRandomProperty(__v_6, 512187)] = __v_6;
  __v_6[__getRandomProperty(__v_6, 563500)] = __getRandomObject(118972);
  console.log();
}
