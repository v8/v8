// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// crbug.com/1026974
(function() {
  function store(obj, key) {
    obj[key] = 10;
  }
  %PrepareFunctionForOptimization(store);
  for (let i = 0; i < 3; i++) {
    let obj = {}
    store(obj, 1152921506754330624);
    assertEquals(["1152921506754330600"], Object.keys(obj));
  }
})();
(function() {
  function store(obj, key) {
    obj[key] = 10;
  }
  %PrepareFunctionForOptimization(store);
  for (let i = 0; i < 3; i++) {
    let obj2 = new Int32Array(0);
    store(obj2, 1152921506754330624);
    assertEquals([], Object.keys(obj2));
    store(obj2, "1152921506754330624");
    assertEquals(["1152921506754330624"], Object.keys(obj2));
  }
})();

// crbug.com/1026729
(function() {
  let key = 0xFFFFFFFF;
  let object = {};
  assertFalse(object.hasOwnProperty(key));
  let proxy = new Proxy({}, {});
  assertFalse(proxy.hasOwnProperty(key));
})();

// crbug.com/1026909
(function() {
  function load(obj, key) {
    return obj[key];
  }
  %PrepareFunctionForOptimization(load);
  const array = new Float64Array();
  assertEquals(undefined, load(array, 'monomorphic'));
  assertEquals(undefined, load(array, '4294967296'));
})();

// crbug.com/1026856
(function() {
  let key = 0xFFFFFFFF;
  let receiver = new Int32Array();
  var value = {};
  var target = {};
  Reflect.set(target, key, value, receiver);
})();
