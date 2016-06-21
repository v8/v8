// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

let kReturnValue = 117;

let buffer = (() => {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, true);
  builder.addFunction("main", kSig_i)
    .addBody([kExprI8Const, kReturnValue])
    .exportFunc();

  return builder.toBuffer();
})()

function CheckInstance(instance) {
  assertFalse(instance === undefined);
  assertFalse(instance === null);
  assertFalse(instance === 0);
  assertEquals("object", typeof instance);

  // Check the memory is an ArrayBuffer.
  var mem = instance.exports.memory;
  assertFalse(mem === undefined);
  assertFalse(mem === null);
  assertFalse(mem === 0);
  assertEquals("object", typeof mem);
  assertTrue(mem instanceof ArrayBuffer);
  for (let i = 0; i < 4; i++) {
    instance.exports.memory = 0;  // should be ignored
    assertSame(mem, instance.exports.memory);
  }

  assertEquals(65536, instance.exports.memory.byteLength);

  // Check the properties of the main function.
  let main = instance.exports.main;
  assertFalse(main === undefined);
  assertFalse(main === null);
  assertFalse(main === 0);
  assertEquals("function", typeof main);

  assertEquals(kReturnValue, main());
}

// Deprecated experimental API.
CheckInstance(Wasm.instantiateModule(buffer));

// Official API
let module = new WebAssembly.Module(buffer);
CheckInstance(new WebAssembly.Instance(module));

let promise = WebAssembly.compile(buffer);
promise.then(module => CheckInstance(new WebAssembly.Instance(module)));
