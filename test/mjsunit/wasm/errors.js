// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --allow-natives-syntax

load("test/mjsunit/wasm/wasm-module-builder.js");

function module(bytes) {
  let buffer = bytes;
  if (typeof buffer === 'string') {
    buffer = new ArrayBuffer(bytes.length);
    let view = new Uint8Array(buffer);
    for (let i = 0; i < bytes.length; ++i) {
      view[i] = bytes.charCodeAt(i);
    }
  }
  return new WebAssembly.Module(buffer);
}

function instance(bytes, imports = {}) {
  return new WebAssembly.Instance(module(bytes), imports);
}

// instantiate should succeed but run should fail.
function instantiateAndFailAtRuntime(bytes, imports = {}) {
  var instance = new WebAssembly.Instance(module(bytes), imports);
  instance.exports.run();
}

function builder() {
  return new WasmModuleBuilder;
}

function assertCompileError(bytes, msg) {
  if (typeof msg === 'string') msg = 'WebAssembly.Module(): ' + msg;
  assertThrows(() => module(bytes), WebAssembly.CompileError, msg);
}

// default imports to {} so we get LinkError by default, thus allowing us to
// distinguish the TypeError we want to catch
function assertTypeError(bytes, imports = {}, msg) {
  assertThrows(() => instance(bytes, imports), TypeError, msg);
}

function assertLinkError(bytes, imports, msg) {
  assertThrows(() => instance(bytes, imports), WebAssembly.LinkError, msg);
}

function assertConversionError(bytes, imports, msg) {
  assertThrows(
      () => instantiateAndFailAtRuntime(bytes, imports), TypeError, msg);
}

(function TestDecodingError() {
  print(arguments.callee.name);
  assertCompileError("", /is empty/);
  assertCompileError("X", /expected 4 bytes, fell off end @\+0/);
  assertCompileError(
    "\0x00asm", /expected magic word 00 61 73 6d, found 00 78 30 30 @\+0/);
})();

(function TestValidationError() {
  print(arguments.callee.name);
  assertCompileError(
      builder().addFunction('f', kSig_i_v).end().toBuffer(),
      'Compiling wasm function "f" failed: ' +
          'function body must end with "end" opcode @+24');
  assertCompileError(
      builder().addFunction('f', kSig_i_v).addBody([kExprReturn])
          .end().toBuffer(),
      /expected 1 elements on the stack for return, found 0 @/);
  assertCompileError(builder().addFunction('f', kSig_v_v).addBody([
    kExprGetLocal, 0
  ]).end().toBuffer(), /invalid local index: 0 @/);
  assertCompileError(
      builder().addStart(0).toBuffer(), /function index 0 out of bounds/);
})();

(function TestTypeError() {
  print(arguments.callee.name);
  let b;
  b = builder();
  b.addImport("foo", "bar", kSig_v_v);
  assertTypeError(b.toBuffer(), {}, /module is not an object or function/);

  b = builder();
  b.addImportedGlobal("foo", "bar", kWasmI32);
  assertTypeError(b.toBuffer(), {}, /module is not an object or function/);

  b = builder();
  b.addImportedMemory("foo", "bar");
  assertTypeError(b.toBuffer(), {}, /module is not an object or function/);
})();

(function TestLinkingError() {
  print(arguments.callee.name);
  let b;

  b = builder();
  b.addImport("foo", "bar", kSig_v_v);
  assertLinkError(
      b.toBuffer(), {foo: {}}, /function import requires a callable/);
  b = builder();
  b.addImport("foo", "bar", kSig_v_v);
  assertLinkError(
      b.toBuffer(), {foo: {bar: 9}}, /function import requires a callable/);

  b = builder();
  b.addImportedGlobal("foo", "bar", kWasmI32);
  assertLinkError(b.toBuffer(), {foo: {}}, /global import must be a number/);
  b = builder();
  b.addImportedGlobal("foo", "bar", kWasmI32);
  assertLinkError(
      b.toBuffer(), {foo: {bar: ""}}, /global import must be a number/);
  b = builder();
  b.addImportedGlobal("foo", "bar", kWasmI32);
  assertLinkError(
      b.toBuffer(), {foo: {bar: () => 9}}, /global import must be a number/);

  b = builder();
  b.addImportedMemory("foo", "bar");
  assertLinkError(
      b.toBuffer(), {foo: {}},
      /memory import must be a WebAssembly\.Memory object/);
  b = builder();
  b.addImportedMemory("foo", "bar", 1);
  assertLinkError(
      b.toBuffer(), {foo: {bar: () => new WebAssembly.Memory({initial: 0})}},
      /memory import must be a WebAssembly\.Memory object/);
})();

(function TestTrapUnreachable() {
  print(arguments.callee.name);
  let instance = builder().addFunction('run', kSig_v_v)
    .addBody([kExprUnreachable]).exportFunc().end().instantiate();
  assertTraps(kTrapUnreachable, instance.exports.run);
})();

(function TestTrapDivByZero() {
  print(arguments.callee.name);
  let instance = builder().addFunction('run', kSig_v_v).addBody(
     [kExprI32Const, 1, kExprI32Const, 0, kExprI32DivS, kExprDrop])
    .exportFunc().end().instantiate();
  assertTraps(kTrapDivByZero, instance.exports.run);
})();

(function TestUnreachableInStart() {
  print(arguments.callee.name);

  let b = builder().addFunction("start", kSig_v_v).addBody(
     [kExprUnreachable]).end().addStart(0);
  assertTraps(kTrapUnreachable, () => b.instantiate());
})();

(function TestConversionError() {
  print(arguments.callee.name);
  let b = builder();
  b.addImport('foo', 'bar', kSig_v_l);
  let buffer = b.addFunction('run', kSig_v_v)
                   .addBody([kExprI64Const, 0, kExprCallFunction, 0])
                   .exportFunc()
                   .end()
                   .toBuffer();
  assertConversionError(
      buffer, {foo: {bar: (l) => {}}}, kTrapMsgs[kTrapTypeError]);

  buffer = builder()
               .addFunction('run', kSig_l_v)
               .addBody([kExprI64Const, 0])
               .exportFunc()
               .end()
               .toBuffer();
  assertConversionError(buffer, {}, kTrapMsgs[kTrapTypeError]);
})();


(function InternalDebugTrace() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  var sig = builder.addType(kSig_i_dd);
  builder.addImport("mod", "func", sig);
  builder.addFunction("main", sig)
    .addBody([kExprGetLocal, 0, kExprGetLocal, 1, kExprCallFunction, 0])
    .exportAs("main");
  var main = builder.instantiate({
    mod: {
      func: ()=>{%DebugTrace();}
    }
  }).exports.main;
  main();
})();
