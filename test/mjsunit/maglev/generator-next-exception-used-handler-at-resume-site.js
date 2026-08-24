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

function test(g) {
  try {
    return g.next().value;
  } catch (e) {
    return e.message;
  }
}
%PrepareFunctionForOptimization(test);

// Mark the catch handler as used while still unoptimized, so that the optimized
// resume uses the ResumeGeneratorTrampoline_WithCatch wrapper instead of the
// with-catch lazy deopt continuation.
shouldThrow = true;
assertEquals("Boom", test(myGen()));
shouldThrow = false;

let g = myGen();

// Warmup:
assertEquals(1, test(g));
assertEquals(1, test(g));

// Optimization:
%OptimizeMaglevOnNextCall(test);
assertEquals(1, test(g));
assertOptimized(test);

// The wrapper closes the generator and rethrows into the optimized catch block,
// so a throwing generator doesn't deoptimize the resume site.
shouldThrow = true;
assertEquals("Boom", test(g));
assertOptimized(test);

// The generator is closed.
let res = g.next();
assertEquals(undefined, res.value);
assertEquals(true, res.done);
