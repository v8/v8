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

// The resume site is inside a try block, but the handler has never seen an
// exception, so the resume goes through ResumeGeneratorTrampoline directly and
// relies on the with-catch lazy deopt continuation.
function test(g) {
  try {
    return g.next().value;
  } catch (e) {
    return e.message;
  }
}
%PrepareFunctionForOptimization(test);

let g = myGen();

// Warmup:
assertEquals(1, test(g));
assertEquals(1, test(g));

// Optimization:
%OptimizeMaglevOnNextCall(test);
assertEquals(1, test(g));
assertOptimized(test);

// The generator throws: the continuation closes the generator and rethrows,
// and the catch handler at the resume site sees the exception.
shouldThrow = true;
assertEquals("Boom", test(g));

// The generator is closed.
let res = g.next();
assertEquals(undefined, res.value);
assertEquals(true, res.done);
