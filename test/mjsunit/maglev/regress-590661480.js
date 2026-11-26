// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-maglev-escape-analysis
// Flags: --maglev-non-eager-inlining
// Flags: --max-maglev-eager-inlined-bytecode-size=0

function makeObj(x) {
  return {x, p: 7};
}

let obj = makeObj(1);
let extra = makeObj(1);
extra.tail = 1;

function noop() {}
%PrepareFunctionForOptimization(noop);

let trigger = false;
function transitionA(o) {
  if (trigger) {
    o.tail = 2;
    Object.defineProperty(o, 'x', {get: () => 42});
  }
}
function transitionB() {}

function readInPeeledLoop(o, fn) {
  o.p;
  noop();
  let res = 0;
  for (let i = 0; i < 3; i++) {
    if (i === 0) {
    } else {
      res = o.x;
      fn(o);
    }
  }
  return res;
}

%PrepareFunctionForOptimization(readInPeeledLoop);
assertEquals(1, readInPeeledLoop(obj, transitionA));
assertEquals(1, readInPeeledLoop(obj, transitionB));
%OptimizeFunctionOnNextCall(readInPeeledLoop);
assertEquals(1, readInPeeledLoop(obj, transitionB));

trigger = true;
assertEquals(42, readInPeeledLoop(obj, transitionA));
