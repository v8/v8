// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

let shouldThrow = false;

function* myGen() {
  while (true) {
    if (shouldThrow) throw new Error("Boom");
    yield 1;
  }
}

// The resume site is inlined, so the with-catch continuation frame sits on top
// of the inlined frame of `resume` and the frame of `test`.
function resume(g) {
  return g.next().value;
}

function test(g) {
  try {
    return resume(g) + 1;
  } catch (e) {
    return e.message;
  }
}
%PrepareFunctionForOptimization(resume);
%PrepareFunctionForOptimization(test);

let g = myGen();

// Warmup:
assertEquals(2, test(g));
assertEquals(2, test(g));

// Optimization:
%OptimizeMaglevOnNextCall(test);
assertEquals(2, test(g));
assertOptimized(test);

// The generator throws: the continuation closes the generator and rethrows into
// the handler of the (deoptimized) caller.
shouldThrow = true;
assertEquals("Boom", test(g));

// The generator is closed.
let res = g.next();
assertEquals(undefined, res.value);
assertEquals(true, res.done);
