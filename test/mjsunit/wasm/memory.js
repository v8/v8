// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

// Basic tests.

var outOfUint32RangeValue = 1e12;

(function TestConstructor() {
  assertTrue(WebAssembly.Memory instanceof Function);
  assertSame(WebAssembly.Memory, WebAssembly.Memory.prototype.constructor);
  assertTrue(WebAssembly.Memory.prototype.grow instanceof Function);
  let desc = Object.getOwnPropertyDescriptor(WebAssembly.Memory.prototype, 'buffer');
  assertTrue(desc.get instanceof Function);
  assertSame(undefined, desc.set);

  assertThrows(() => new WebAssembly.Memory(), TypeError);
  assertThrows(() => new WebAssembly.Memory(1), TypeError);
  assertThrows(() => new WebAssembly.Memory(""), TypeError);

  assertThrows(() => new WebAssembly.Memory({initial: -1}), RangeError);
  assertThrows(() => new WebAssembly.Memory({initial: outOfUint32RangeValue}), RangeError);

  assertThrows(() => new WebAssembly.Memory({initial: 10, maximum: -1}), RangeError);
  assertThrows(() => new WebAssembly.Memory({initial: 10, maximum: outOfUint32RangeValue}), RangeError);
  assertThrows(() => new WebAssembly.Memory({initial: 10, maximum: 9}), RangeError);

  let memory = new WebAssembly.Memory({initial: 1});
  assertSame(WebAssembly.Memory.prototype, memory.__proto__);
  assertSame(WebAssembly.Memory, memory.constructor);
  assertTrue(memory instanceof Object);
  assertTrue(memory instanceof WebAssembly.Memory);
})();

(function TestConstructorWithMaximum() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 10});
  assertSame(WebAssembly.Memory.prototype, memory.__proto__);
  assertSame(WebAssembly.Memory, memory.constructor);
  assertTrue(memory instanceof Object);
  assertTrue(memory instanceof WebAssembly.Memory);
})();

(function TestBuffer() {
  let memory = new WebAssembly.Memory({initial: 1});
  assertTrue(memory.buffer instanceof Object);
  assertTrue(memory.buffer instanceof ArrayBuffer);
  assertThrows(() => {'use strict'; memory.buffer = memory.buffer}, TypeError)
  assertThrows(() => ({__proto__: memory}).buffer, TypeError)
})();
