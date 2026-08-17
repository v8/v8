// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbolev-escape-analysis
// Flags: --turbofan

let shouldThrow = false;

function* myGen() {
  while (true) {
    if (shouldThrow) throw new Error("Boom");
    yield 1;
  }
}

function test(g) {
  // Eliding this allocation makes the escape analysis rewrite -- and thus
  // clone -- every deopt frame of the function, including the with-catch
  // continuation frame of the generator resume below.
  let o = {x: 1};
  let value = g.next().value;
  %AssertEscapeAnalysisElided(o);
  return value + o.x;
}
%PrepareFunctionForOptimization(test);

let g = myGen();

// Warmup:
assertEquals(2, test(g));
assertEquals(2, test(g));

// Optimization:
%OptimizeFunctionOnNextCall(test);
assertEquals(2, test(g));
assertOptimized(test);

// The generator throws: the cloned continuation frame still has to close the
// generator and rethrow.
shouldThrow = true;
assertThrows(() => test(g), Error, "Boom");

// The generator is closed.
let res = g.next();
assertEquals(undefined, res.value);
assertEquals(true, res.done);
