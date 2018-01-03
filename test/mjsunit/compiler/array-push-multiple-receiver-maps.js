// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

(function singleUnreliableReceiverMap() {
  function f(a, g) {
    a.push(2, g());
  }

  f([1], () => 3);
  f([1], () => 3);
  %OptimizeFunctionOnNextCall(f);
  f([1], () => 3);
  assertOptimized(f);
})();

(function singleUnreliableReceiverMapDeopt() {
  function f(a, g) {
    a.push(2, g());
  }

  f([1], () => 3);
  f([1], () => 3);
  %OptimizeFunctionOnNextCall(f);
  f([1], () => true);
  %OptimizeFunctionOnNextCall(f);
  f([1], () => true);
  assertOptimized(f);
})();

(function multipleUnreliableReceiverMaps(){
  function f(a, g) {
    a.push(2, g());
  }
  let b = [1]
  b.x = 3;

  f([1], () => 3);
  f(b, () => 3);
  f([1], () => 3);
  f(b, () => 3);
  %OptimizeFunctionOnNextCall(f);
  f([1], () => 3);
  assertOptimized(f);
})();

(function multipleUnreliableReceiverMapsDeopt(){
  function f(a, g) {
    a.push(2, g());
  }
  let b = [1]
  b.x = 3;

  f([1], () => 3);
  f(b, () => 3);
  f([1], () => 3);
  f(b, () => 3);
  %OptimizeFunctionOnNextCall(f);
  f([0.1], () => 3);
  %OptimizeFunctionOnNextCall(f);
  f([0.1], () => 3);
  assertOptimized(f);
})();

(function multipleReliableReceiverMaps(){
  function f(a) {
    a.push(2);
  }
  let b = [1]
  b.x = 3;

  f([1]);
  f(b);
  f([1]);
  f(b);
  %OptimizeFunctionOnNextCall(f);
  f([1]);
  assertOptimized(f);
})();

(function multipleReliableReceiverMapsDeopt(){
  function f(a) {
    a.push(2);
  }
  let b = [1]
  b.x = 3;

  f([1]);
  f(b);
  f([1]);
  f(b);
  %OptimizeFunctionOnNextCall(f);
  f([0.1]);
  %OptimizeFunctionOnNextCall(f);
  f([0.1]);
  assertOptimized(f);
})();
