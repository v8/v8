// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-mut-global

function assertGlobalIsValid(global) {
  assertSame(WebAssembly.Global.prototype, global.__proto__);
  assertSame(WebAssembly.Global, global.constructor);
  assertTrue(global instanceof Object);
  assertTrue(global instanceof WebAssembly.Global);
}

(function TestConstructor() {

  assertTrue(WebAssembly.Global instanceof Function);
  assertSame(WebAssembly.Global, WebAssembly.Global.prototype.constructor);

  assertThrows(() => new WebAssembly.Global(), TypeError);
  assertThrows(() => new WebAssembly.Global(1), TypeError);
  assertThrows(() => new WebAssembly.Global(""), TypeError);

  assertThrows(() => new WebAssembly.Global({}), TypeError);
  assertThrows(() => new WebAssembly.Global({type: 'foo'}), TypeError);
  assertThrows(() => new WebAssembly.Global({type: 'i64'}), TypeError);

  for (let type of ['i32', 'f32', 'f64']) {
    assertGlobalIsValid(new WebAssembly.Global({type}));
  }
})();
