// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

(function TestCallableObjectAsPropertyGetterOrSetter() {
  let log = [];
  let document_all_property = 42;

  Object.defineProperty(
      globalThis, "document_all_property",
      {
        get() { log.push("get"); return document_all_property; },
        set(v) { log.push("set"); document_all_property = v; },
        configurable: true
      });

  let callable_obj = d8.dom.Document_all;
  log.push(callable_obj());
  document_all_property = 55;
  log.push(callable_obj(153));
  assertEquals(document_all_property, 153);

  assertEquals(["get", 42, "set", true], log);

  let p = {};
  Object.defineProperty(
      p, "x",
      {
        get: callable_obj,
        set: callable_obj,
        configurable: true
      });

  // Test accessors on receiver.
  log = [];
  function f(o, v) {
    o.x = v;
    return o.x;
  }
  %PrepareFunctionForOptimization(f);
  for (let i = 0; i < 5; i++) {
    log.push(document_all_property);
    log.push(f(p, i));
  }
  %OptimizeFunctionOnNextCall(f);
  log.push(f(p, 572));

  assertEquals(
      [
        153, "set", "get", 0,
        0, "set", "get", 1,
        1, "set", "get", 2,
        2, "set", "get", 3,
        3, "set", "get", 4,
        "set", "get", 572
      ],
      log);

  // Test accessors on the prototype chain.
  log = [];
  function f(o, v) {
    o.x = v;
    return o.x;
  }

  let o = Object.create(p);

  %PrepareFunctionForOptimization(f);
  for (let i = 0; i < 5; i++) {
    log.push(document_all_property);
    log.push(f(o, i));
  }
  %OptimizeFunctionOnNextCall(f);
  log.push(f(o, 157));

  assertEquals(
      [
        572, "set", "get", 0,
        0, "set", "get", 1,
        1, "set", "get", 2,
        2, "set", "get", 3,
        3, "set", "get", 4,
        "set", "get", 157
      ],
      log);

})();
