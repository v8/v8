// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

// Basic tests.

var outOfUint32RangeValue = 1e12;

(function TestConstructor() {
  assertTrue(WebAssembly.Table instanceof Function);
  assertSame(WebAssembly.Table, WebAssembly.Table.prototype.constructor);
  assertTrue(WebAssembly.Table.prototype.grow instanceof Function);
  assertTrue(WebAssembly.Table.prototype.get instanceof Function);
  assertTrue(WebAssembly.Table.prototype.set instanceof Function);
  let desc = Object.getOwnPropertyDescriptor(WebAssembly.Table.prototype, 'length');
  assertTrue(desc.get instanceof Function);
  assertSame(undefined, desc.set);

  assertThrows(() => new WebAssembly.Table(), TypeError);
  assertThrows(() => new WebAssembly.Table(1), TypeError);
  assertThrows(() => new WebAssembly.Table(""), TypeError);

  assertThrows(() => new WebAssembly.Table({}), TypeError);
  assertThrows(() => new WebAssembly.Table({initial: 10}), TypeError);

  assertThrows(() => new WebAssembly.Table({element: 0, initial: 10}), TypeError);
  assertThrows(() => new WebAssembly.Table({element: "any", initial: 10}), TypeError);

  assertThrows(() => new WebAssembly.Table({element: "anyfunc", initial: -1}), RangeError);
  assertThrows(() => new WebAssembly.Table({element: "anyfunc", initial: outOfUint32RangeValue}), RangeError);

  assertThrows(() => new WebAssembly.Table({element: "anyfunc", initial: 10, maximum: -1}), RangeError);
  assertThrows(() => new WebAssembly.Table({element: "anyfunc", initial: 10, maximum: outOfUint32RangeValue}), RangeError);
  assertThrows(() => new WebAssembly.Table({element: "anyfunc", initial: 10, maximum: 9}), RangeError);

  let table = new WebAssembly.Table({element: "anyfunc", initial: 1});
  assertSame(WebAssembly.Table.prototype, table.__proto__);
  assertSame(WebAssembly.Table, table.constructor);
  assertTrue(table instanceof Object);
  assertTrue(table instanceof WebAssembly.Table);
})();

(function TestConstructorWithMaximum() {
  let table = new WebAssembly.Table({element: "anyfunc", initial: 1, maximum: 10});
  assertSame(WebAssembly.Table.prototype, table.__proto__);
  assertSame(WebAssembly.Table, table.constructor);
  assertTrue(table instanceof Object);
  assertTrue(table instanceof WebAssembly.Table);
})();
