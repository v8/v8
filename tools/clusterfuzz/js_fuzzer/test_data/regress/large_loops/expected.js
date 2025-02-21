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
  /* VariableOrObjectMutator: Random mutation */
  __v_2[__getRandomProperty(__v_2, 512187)] = __v_1;
  __v_2[__getRandomProperty(__v_2, 563500)] = __getRandomObject(118972);
  console.log();
}
for (const __v_3 = 0; 1e5 >= __v_3; __v_3--) {
  console.log();
}
for (const __v_4 = -10000; __v_4 < 0; __v_4++) {
  console.log();
}
for (const __v_5 = 0n; __v_5 < 10000n; __v_5++) {
  /* VariableOrObjectMutator: Random mutation */
  __v_1[__getRandomProperty(__v_1, 727773)];
  /* CrossOverMutator: Crossover from foo */
  async (...__v_0) => {
    const __v_5 = 0;
  };
  console.log();
  /* VariableOrObjectMutator: Random mutation */
  __v_0[__getRandomProperty(__v_0, 81149)], __callGC(true);
}
for (const __v_6 = -0.3; __v_6 < 1000.1; __v_6 += 0.5) {
  console.log();
  /* VariableOrObjectMutator: Random mutation */
  __v_1[__getRandomProperty(__v_1, 446763)] = __v_6;
}
