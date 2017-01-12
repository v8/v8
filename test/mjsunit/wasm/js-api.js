// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --allow-natives-syntax

if ((typeof drainJobQueue) != "function") {
  drainJobQueue = () => { %RunMicrotasks() };
}

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function assertEq(val, expected) {
  assertEquals(expected, val);
}
function wasmIsSupported() {
  return (typeof WebAssembly.Module) == "function";
}
function assertErrorMessage(func, type, msg) {
//TODO   assertThrows(func, type, msg);
  assertThrows(func, type);
}

let emptyModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  return new Int8Array(builder.toBuffer());
})();

let exportingModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  builder.addFunction("f", kSig_i_v)
    .addBody([kExprI32Const, 42])
    .exportAs("f");
  return new Int8Array(builder.toBuffer());
})();

let importingModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImport("", "f", kSig_i_v);
  return new Int8Array(builder.toBuffer());
})();

let moduleBinaryImporting2Memories = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "memory1");
  builder.addImportedMemory("", "memory2");
  return new Int8Array(builder.toBuffer());
})();

let moduleBinaryWithMemSectionAndMemImport = (() => {
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, false);
  builder.addImportedMemory("", "memory1");
  return new Int8Array(builder.toBuffer());
})();

// 'WebAssembly' data property on global object
let wasmDesc = Object.getOwnPropertyDescriptor(this, 'WebAssembly');
assertEq(typeof wasmDesc.value, "object");
assertEq(wasmDesc.writable, true);
assertEq(wasmDesc.enumerable, false);
assertEq(wasmDesc.configurable, true);

// 'WebAssembly' object
assertEq(WebAssembly, wasmDesc.value);
//TODO assertEq(String(WebAssembly), "[object WebAssembly]");

// 'WebAssembly.CompileError'
let compileErrorDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'CompileError');
assertEq(typeof compileErrorDesc.value, "function");
assertEq(compileErrorDesc.writable, true);
assertEq(compileErrorDesc.enumerable, false);
assertEq(compileErrorDesc.configurable, true);
let CompileError = WebAssembly.CompileError;
assertEq(CompileError, compileErrorDesc.value);
assertEq(CompileError.length, 1);
assertEq(CompileError.name, "CompileError");
let compileError = new CompileError;
assertEq(compileError instanceof CompileError, true);
assertEq(compileError instanceof Error, true);
assertEq(compileError instanceof TypeError, false);
assertEq(compileError.message, "");
assertEq(new CompileError("hi").message, "hi");

// 'WebAssembly.RuntimeError'
let runtimeErrorDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'RuntimeError');
assertEq(typeof runtimeErrorDesc.value, "function");
assertEq(runtimeErrorDesc.writable, true);
assertEq(runtimeErrorDesc.enumerable, false);
assertEq(runtimeErrorDesc.configurable, true);
let RuntimeError = WebAssembly.RuntimeError;
assertEq(RuntimeError, runtimeErrorDesc.value);
assertEq(RuntimeError.length, 1);
assertEq(RuntimeError.name, "RuntimeError");
let runtimeError = new RuntimeError;
assertEq(runtimeError instanceof RuntimeError, true);
assertEq(runtimeError instanceof Error, true);
assertEq(runtimeError instanceof TypeError, false);
assertEq(runtimeError.message, "");
assertEq(new RuntimeError("hi").message, "hi");

// 'WebAssembly.LinkError'
let linkErrorDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'LinkError');
assertEq(typeof linkErrorDesc.value, "function");
assertEq(linkErrorDesc.writable, true);
assertEq(linkErrorDesc.enumerable, false);
assertEq(linkErrorDesc.configurable, true);
let LinkError = WebAssembly.LinkError;
assertEq(LinkError, linkErrorDesc.value);
assertEq(LinkError.length, 1);
assertEq(LinkError.name, "LinkError");
let linkError = new LinkError;
assertEq(linkError instanceof LinkError, true);
assertEq(linkError instanceof Error, true);
assertEq(linkError instanceof TypeError, false);
assertEq(linkError.message, "");
assertEq(new LinkError("hi").message, "hi");

// 'WebAssembly.Module' data property
let moduleDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Module');
assertEq(typeof moduleDesc.value, "function");
assertEq(moduleDesc.writable, true);
assertEq(moduleDesc.enumerable, false);
assertEq(moduleDesc.configurable, true);

// 'WebAssembly.Module' constructor function
let Module = WebAssembly.Module;
assertEq(Module, moduleDesc.value);
assertEq(Module.length, 1);
assertEq(Module.name, "Module");
assertErrorMessage(() => Module(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Module(), TypeError, /requires more than 0 arguments/);
assertErrorMessage(() => new Module(undefined), TypeError, "first argument must be an ArrayBuffer or typed array object");
assertErrorMessage(() => new Module(1), TypeError, "first argument must be an ArrayBuffer or typed array object");
assertErrorMessage(() => new Module({}), TypeError, "first argument must be an ArrayBuffer or typed array object");
assertErrorMessage(() => new Module(new Uint8Array()), CompileError, /failed to match magic number/);
assertErrorMessage(() => new Module(new ArrayBuffer()), CompileError, /failed to match magic number/);
assertEq(new Module(emptyModuleBinary) instanceof Module, true);
assertEq(new Module(emptyModuleBinary.buffer) instanceof Module, true);

// 'WebAssembly.Module.prototype' data property
let moduleProtoDesc = Object.getOwnPropertyDescriptor(Module, 'prototype');
assertEq(typeof moduleProtoDesc.value, "object");
assertEq(moduleProtoDesc.writable, false);
assertEq(moduleProtoDesc.enumerable, false);
assertEq(moduleProtoDesc.configurable, false);

// 'WebAssembly.Module.prototype' object
let moduleProto = Module.prototype;
assertEq(moduleProto, moduleProtoDesc.value);
assertEq(String(moduleProto), "[object Object]");
assertEq(Object.getPrototypeOf(moduleProto), Object.prototype);

// 'WebAssembly.Module' instance objects
let emptyModule = new Module(emptyModuleBinary);
let importingModule = new Module(importingModuleBinary);
let exportingModule = new Module(exportingModuleBinary);
assertEq(typeof emptyModule, "object");
//TODO assertEq(String(emptyModule), "[object WebAssembly.Module]");
assertEq(Object.getPrototypeOf(emptyModule), moduleProto);

// 'WebAssembly.Module.imports' data property
let moduleImportsDesc = Object.getOwnPropertyDescriptor(Module, 'imports');
assertEq(typeof moduleImportsDesc.value, "function");
assertEq(moduleImportsDesc.writable, true);
assertEq(moduleImportsDesc.enumerable, false);
assertEq(moduleImportsDesc.configurable, true);

// 'WebAssembly.Module.imports' method
let moduleImports = moduleImportsDesc.value;
assertEq(moduleImports.length, 1);
assertErrorMessage(() => moduleImports(), TypeError, /requires more than 0 arguments/);
assertErrorMessage(() => moduleImports(undefined), TypeError, /first argument must be a WebAssembly.Module/);
assertErrorMessage(() => moduleImports({}), TypeError, /first argument must be a WebAssembly.Module/);
var arr = moduleImports(new Module(emptyModuleBinary));
assertEq(arr instanceof Array, true);
assertEq(arr.length, 0);
let importingModuleBinary2 = (() => {
  var text = '(module (func (import "a" "b")) (memory (import "c" "d") 1) (table (import "e" "f") 1 anyfunc) (global (import "g" "⚡") i32))'
  let builder = new WasmModuleBuilder();
  builder.addImport("a", "b", kSig_i_i);
  builder.addImportedMemory("c", "d");
  builder.addImportedTable("e", "f");
  builder.addImportedGlobal("g", "x", kWasmI32);
  return new Int8Array(builder.toBuffer());
})();
var arr = moduleImports(new Module(importingModuleBinary2));
assertEq(arr instanceof Array, true);
assertEq(arr.length, 4);
assertEq(arr[0].kind, "function");
assertEq(arr[0].module, "a");
assertEq(arr[0].name, "b");
assertEq(arr[1].kind, "memory");
assertEq(arr[1].module, "c");
assertEq(arr[1].name, "d");
assertEq(arr[2].kind, "table");
assertEq(arr[2].module, "e");
assertEq(arr[2].name, "f");
assertEq(arr[3].kind, "global");
assertEq(arr[3].module, "g");
assertEq(arr[3].name, "x");

// 'WebAssembly.Module.exports' data property
let moduleExportsDesc = Object.getOwnPropertyDescriptor(Module, 'exports');
assertEq(typeof moduleExportsDesc.value, "function");
assertEq(moduleExportsDesc.writable, true);
assertEq(moduleExportsDesc.enumerable, false);
assertEq(moduleExportsDesc.configurable, true);

// 'WebAssembly.Module.exports' method
let moduleExports = moduleExportsDesc.value;
assertEq(moduleExports.length, 1);
assertErrorMessage(() => moduleExports(), TypeError, /requires more than 0 arguments/);
assertErrorMessage(() => moduleExports(undefined), TypeError, /first argument must be a WebAssembly.Module/);
assertErrorMessage(() => moduleExports({}), TypeError, /first argument must be a WebAssembly.Module/);
var arr = moduleExports(emptyModule);
assertEq(arr instanceof Array, true);
assertEq(arr.length, 0);
let exportingModuleBinary2 = (() => {
  var text =
  '(module (func (export "a")) (memory (export "b") 1) (table (export "c") 1 anyfunc) (global (export "⚡") i32 (i32.const 0)))';
  let builder = new WasmModuleBuilder();
  builder.addFunction("foo", kSig_v_v)
    .addBody([])
    .exportAs("a");
  builder.addMemory(1, 1, false);
  builder.exportMemoryAs("b");
  builder.setFunctionTableLength(1);
  builder.addExportOfKind("c", kExternalTable, 0);
  var o = builder.addGlobal(kWasmI32, false)
      .exportAs("x");
  return new Int8Array(builder.toBuffer());
})();
var arr = moduleExports(new Module(exportingModuleBinary2));
assertEq(arr instanceof Array, true);
assertEq(arr.length, 4);
assertEq(arr[0].kind, "function");
assertEq(arr[0].name, "a");
assertEq(arr[1].kind, "memory");
assertEq(arr[1].name, "b");
assertEq(arr[2].kind, "table");
assertEq(arr[2].name, "c");
assertEq(arr[3].kind, "global");
assertEq(arr[3].name, "x");

// 'WebAssembly.Instance' data property
let instanceDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Instance');
assertEq(typeof instanceDesc.value, "function");
assertEq(instanceDesc.writable, true);
assertEq(instanceDesc.enumerable, false);
assertEq(instanceDesc.configurable, true);

// 'WebAssembly.Instance' constructor function
let Instance = WebAssembly.Instance;
assertEq(Instance, instanceDesc.value);
assertEq(Instance.length, 1);
assertEq(Instance.name, "Instance");
assertErrorMessage(() => Instance(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Instance(1), TypeError, "first argument must be a WebAssembly.Module");
assertErrorMessage(() => new Instance({}), TypeError, "first argument must be a WebAssembly.Module");
assertErrorMessage(() => new Instance(emptyModule, null), TypeError, "second argument must be an object");
assertEq(new Instance(emptyModule) instanceof Instance, true);
assertEq(new Instance(emptyModule, {}) instanceof Instance, true);

// 'WebAssembly.Instance.prototype' data property
let instanceProtoDesc = Object.getOwnPropertyDescriptor(Instance, 'prototype');
assertEq(typeof instanceProtoDesc.value, "object");
assertEq(instanceProtoDesc.writable, false);
assertEq(instanceProtoDesc.enumerable, false);
assertEq(instanceProtoDesc.configurable, false);

// 'WebAssembly.Instance.prototype' object
let instanceProto = Instance.prototype;
assertEq(instanceProto, instanceProtoDesc.value);
assertEq(String(instanceProto), "[object Object]");
assertEq(Object.getPrototypeOf(instanceProto), Object.prototype);

// 'WebAssembly.Instance' instance objects
let exportingInstance = new Instance(exportingModule);
assertEq(typeof exportingInstance, "object");
//TODO:name assertEq(String(exportingInstance), "[object WebAssembly.Instance]");
//TODO assertEq(Object.getPrototypeOf(exportingInstance), instanceProto);

// 'WebAssembly.Instance' 'exports' data property
let instanceExportsDesc = Object.getOwnPropertyDescriptor(exportingInstance, 'exports');
assertEq(typeof instanceExportsDesc.value, "object");
assertEq(instanceExportsDesc.writable, true);
assertEq(instanceExportsDesc.enumerable, true);
assertEq(instanceExportsDesc.configurable, true);

// Exported WebAssembly functions
let f = exportingInstance.exports.f;
assertEq(f instanceof Function, true);
assertEq(f.length, 0);
assertEq('name' in f, true);
assertEq(Function.prototype.call.call(f), 42);
assertErrorMessage(() => new f(), TypeError, /is not a constructor/);

// 'WebAssembly.Memory' data property
let memoryDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Memory');
assertEq(typeof memoryDesc.value, "function");
assertEq(memoryDesc.writable, true);
assertEq(memoryDesc.enumerable, false);
assertEq(memoryDesc.configurable, true);

// 'WebAssembly.Memory' constructor function
let Memory = WebAssembly.Memory;
assertEq(Memory, memoryDesc.value);
assertEq(Memory.length, 1);
assertEq(Memory.name, "Memory");
assertErrorMessage(() => Memory(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Memory(1), TypeError, "first argument must be a memory descriptor");
assertErrorMessage(() => new Memory({initial:{valueOf() { throw new Error("here")}}}), Error, "here");
assertErrorMessage(() => new Memory({initial:-1}), RangeError, /bad Memory initial size/);
assertErrorMessage(() => new Memory({initial:Math.pow(2,32)}), RangeError, /bad Memory initial size/);
assertErrorMessage(() => new Memory({initial:1, maximum: Math.pow(2,32)/Math.pow(2,14) }), RangeError, /bad Memory maximum size/);
assertErrorMessage(() => new Memory({initial:2, maximum:1 }), RangeError, /bad Memory maximum size/);
assertErrorMessage(() => new Memory({maximum: -1 }), RangeError, /bad Memory maximum size/);
assertEq(new Memory({initial:1}) instanceof Memory, true);
assertEq(new Memory({initial:1.5}).buffer.byteLength, kPageSize);

// 'WebAssembly.Memory.prototype' data property
let memoryProtoDesc = Object.getOwnPropertyDescriptor(Memory, 'prototype');
assertEq(typeof memoryProtoDesc.value, "object");
assertEq(memoryProtoDesc.writable, false);
assertEq(memoryProtoDesc.enumerable, false);
assertEq(memoryProtoDesc.configurable, false);

// 'WebAssembly.Memory.prototype' object
let memoryProto = Memory.prototype;
assertEq(memoryProto, memoryProtoDesc.value);
assertEq(String(memoryProto), "[object Object]");
assertEq(Object.getPrototypeOf(memoryProto), Object.prototype);

// 'WebAssembly.Memory' instance objects
let mem1 = new Memory({initial:1});
assertEq(typeof mem1, "object");
//TODO assertEq(String(mem1), "[object WebAssembly.Memory]");
assertEq(Object.getPrototypeOf(mem1), memoryProto);

// 'WebAssembly.Memory.prototype.buffer' accessor property
let bufferDesc = Object.getOwnPropertyDescriptor(memoryProto, 'buffer');
assertEq(typeof bufferDesc.get, "function");
assertEq(bufferDesc.set, undefined);
assertEq(bufferDesc.enumerable, false);
assertEq(bufferDesc.configurable, true);

// 'WebAssembly.Memory.prototype.buffer' getter
let bufferGetter = bufferDesc.get;
assertErrorMessage(() => bufferGetter.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => bufferGetter.call({}), TypeError, /called on incompatible Object/);
assertEq(bufferGetter.call(mem1) instanceof ArrayBuffer, true);
assertEq(bufferGetter.call(mem1).byteLength, kPageSize);

// 'WebAssembly.Memory.prototype.grow' data property
let memGrowDesc = Object.getOwnPropertyDescriptor(memoryProto, 'grow');
assertEq(typeof memGrowDesc.value, "function");
assertEq(memGrowDesc.enumerable, false);
assertEq(memGrowDesc.configurable, true);

// 'WebAssembly.Memory.prototype.grow' method

if (false) { // TODO: bugs with Memory.grow
let memGrow = memGrowDesc.value;
assertEq(memGrow.length, 1);
assertErrorMessage(() => memGrow.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => memGrow.call({}), TypeError, /called on incompatible Object/);
assertErrorMessage(() => memGrow.call(mem1, -1), RangeError, /bad Memory grow delta/);
assertErrorMessage(() => memGrow.call(mem1, Math.pow(2,32)), RangeError, /bad Memory grow delta/);
var mem = new Memory({initial:1, maximum:2});
var buf = mem.buffer;
assertEq(buf.byteLength, kPageSize);
assertEq(mem.grow(0), 1);
assertEq(buf !== mem.buffer, true);
assertEq(buf.byteLength, 0);
buf = mem.buffer;
assertEq(buf.byteLength, kPageSize);
assertEq(mem.grow(1), 1);
assertEq(buf !== mem.buffer, true);
assertEq(buf.byteLength, 0);
buf = mem.buffer;
assertEq(buf.byteLength, 2 * kPageSize);
assertErrorMessage(() => mem.grow(1), Error, /failed to grow memory/);
assertEq(buf, mem.buffer);
}

// 'WebAssembly.Table' data property
let tableDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Table');
assertEq(typeof tableDesc.value, "function");
assertEq(tableDesc.writable, true);
assertEq(tableDesc.enumerable, false);
assertEq(tableDesc.configurable, true);

// 'WebAssembly.Table' constructor function
let Table = WebAssembly.Table;
assertEq(Table, tableDesc.value);
assertEq(Table.length, 1);
assertEq(Table.name, "Table");
assertErrorMessage(() => Table(), TypeError, /constructor without new is forbidden/);
assertErrorMessage(() => new Table(1), TypeError, "first argument must be a table descriptor");
assertErrorMessage(() => new Table({initial:1, element:1}), TypeError, /must be "anyfunc"/);
assertErrorMessage(() => new Table({initial:1, element:"any"}), TypeError, /must be "anyfunc"/);
assertErrorMessage(() => new Table({initial:1, element:{valueOf() { return "anyfunc" }}}), TypeError, /must be "anyfunc"/);
assertErrorMessage(() => new Table({initial:{valueOf() { throw new Error("here")}}, element:"anyfunc"}), Error, "here");
assertErrorMessage(() => new Table({initial:-1, element:"anyfunc"}), RangeError, /bad Table initial size/);
assertErrorMessage(() => new Table({initial:Math.pow(2,32), element:"anyfunc"}), RangeError, /bad Table initial size/);
assertErrorMessage(() => new Table({initial:2, maximum:1, element:"anyfunc"}), RangeError, /bad Table maximum size/);
assertErrorMessage(() => new Table({initial:2, maximum:Math.pow(2,32), element:"anyfunc"}), RangeError, /bad Table maximum size/);
assertEq(new Table({initial:1, element:"anyfunc"}) instanceof Table, true);
assertEq(new Table({initial:1.5, element:"anyfunc"}) instanceof Table, true);
assertEq(new Table({initial:1, maximum:1.5, element:"anyfunc"}) instanceof Table, true);
//TODO:maximum assertEq(new Table({initial:1, maximum:Math.pow(2,32)-1, element:"anyfunc"}) instanceof Table, true);

// 'WebAssembly.Table.prototype' data property
let tableProtoDesc = Object.getOwnPropertyDescriptor(Table, 'prototype');
assertEq(typeof tableProtoDesc.value, "object");
assertEq(tableProtoDesc.writable, false);
assertEq(tableProtoDesc.enumerable, false);
assertEq(tableProtoDesc.configurable, false);

// 'WebAssembly.Table.prototype' object
let tableProto = Table.prototype;
assertEq(tableProto, tableProtoDesc.value);
assertEq(String(tableProto), "[object Object]");
assertEq(Object.getPrototypeOf(tableProto), Object.prototype);

// 'WebAssembly.Table' instance objects
let tbl1 = new Table({initial:2, element:"anyfunc"});
assertEq(typeof tbl1, "object");
//TODO:name assertEq(String(tbl1), "[object WebAssembly.Table]");
assertEq(Object.getPrototypeOf(tbl1), tableProto);

// 'WebAssembly.Table.prototype.length' accessor data property
let lengthDesc = Object.getOwnPropertyDescriptor(tableProto, 'length');
assertEq(typeof lengthDesc.get, "function");
assertEq(lengthDesc.set, undefined);
assertEq(lengthDesc.enumerable, false);
assertEq(lengthDesc.configurable, true);

// 'WebAssembly.Table.prototype.length' getter
let lengthGetter = lengthDesc.get;
assertEq(lengthGetter.length, 0);
assertErrorMessage(() => lengthGetter.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => lengthGetter.call({}), TypeError, /called on incompatible Object/);
assertEq(typeof lengthGetter.call(tbl1), "number");
assertEq(lengthGetter.call(tbl1), 2);

// 'WebAssembly.Table.prototype.get' data property
let getDesc = Object.getOwnPropertyDescriptor(tableProto, 'get');
assertEq(typeof getDesc.value, "function");
assertEq(getDesc.enumerable, false);
assertEq(getDesc.configurable, true);

// 'WebAssembly.Table.prototype.get' method
let get = getDesc.value;
assertEq(get.length, 1);
assertErrorMessage(() => get.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => get.call({}), TypeError, /called on incompatible Object/);
assertEq(get.call(tbl1, 0), null);
assertEq(get.call(tbl1, 1), null);
assertEq(get.call(tbl1, 1.5), null);
assertErrorMessage(() => get.call(tbl1, 2), RangeError, /bad Table get index/);
assertErrorMessage(() => get.call(tbl1, 2.5), RangeError, /bad Table get index/);
assertErrorMessage(() => get.call(tbl1, -1), RangeError, /bad Table get index/);
//TODO assertErrorMessage(() => get.call(tbl1, Math.pow(2,33)), RangeError, /bad Table get index/);
assertErrorMessage(() => get.call(tbl1, {valueOf() { throw new Error("hi") }}), Error, "hi");

// 'WebAssembly.Table.prototype.set' data property
let setDesc = Object.getOwnPropertyDescriptor(tableProto, 'set');
assertEq(typeof setDesc.value, "function");
assertEq(setDesc.enumerable, false);
assertEq(setDesc.configurable, true);

// 'WebAssembly.Table.prototype.set' method
let set = setDesc.value;
assertEq(set.length, 2);
assertErrorMessage(() => set.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => set.call({}), TypeError, /called on incompatible Object/);
assertErrorMessage(() => set.call(tbl1, 0), TypeError, /requires more than 1 argument/);
assertErrorMessage(() => set.call(tbl1, 2, null), RangeError, /bad Table set index/);
assertErrorMessage(() => set.call(tbl1, -1, null), RangeError, /bad Table set index/);
//TODO assertErrorMessage(() => set.call(tbl1, Math.pow(2,33), null), RangeError, /bad Table set index/);
assertErrorMessage(() => set.call(tbl1, 0, undefined), TypeError, /can only assign WebAssembly exported functions to Table/);
assertErrorMessage(() => set.call(tbl1, 0, {}), TypeError, /can only assign WebAssembly exported functions to Table/);
assertErrorMessage(() => set.call(tbl1, 0, function() {}), TypeError, /can only assign WebAssembly exported functions to Table/);
assertErrorMessage(() => set.call(tbl1, 0, Math.sin), TypeError, /can only assign WebAssembly exported functions to Table/);
assertErrorMessage(() => set.call(tbl1, {valueOf() { throw Error("hai") }}, null), Error, "hai");
assertEq(set.call(tbl1, 0, null), undefined);
assertEq(set.call(tbl1, 1, null), undefined);

// 'WebAssembly.Table.prototype.grow' data property
let tblGrowDesc = Object.getOwnPropertyDescriptor(tableProto, 'grow');
assertEq(typeof tblGrowDesc.value, "function");
assertEq(tblGrowDesc.enumerable, false);
assertEq(tblGrowDesc.configurable, true);

// 'WebAssembly.Table.prototype.grow' method
if (false) { // TODO: Table.grow
let tblGrow = tblGrowDesc.value;
assertEq(tblGrow.length, 1);
assertErrorMessage(() => tblGrow.call(), TypeError, /called on incompatible undefined/);
assertErrorMessage(() => tblGrow.call({}), TypeError, /called on incompatible Object/);
assertErrorMessage(() => tblGrow.call(tbl1, -1), RangeError, /bad Table grow delta/);
assertErrorMessage(() => tblGrow.call(tbl1, Math.pow(2,32)), RangeError, /bad Table grow delta/);
var tbl = new Table({element:"anyfunc", initial:1, maximum:2});
assertEq(tbl.length, 1);
assertEq(tbl.grow(0), 1);
assertEq(tbl.length, 1);
assertEq(tbl.grow(1), 1);
assertEq(tbl.length, 2);
assertErrorMessage(() => tbl.grow(1), Error, /failed to grow table/);
}

// 'WebAssembly.validate' function
assertErrorMessage(() => WebAssembly.validate(), TypeError);
assertErrorMessage(() => WebAssembly.validate("hi"), TypeError);
assertEq(WebAssembly.validate(emptyModuleBinary), true);
// TODO: other ways for validate to return false.
assertEq(WebAssembly.validate(moduleBinaryImporting2Memories), false);
assertEq(WebAssembly.validate(moduleBinaryWithMemSectionAndMemImport), false);

// 'WebAssembly.compile' data property
let compileDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'compile');
assertEq(typeof compileDesc.value, "function");
assertEq(compileDesc.writable, true);
assertEq(compileDesc.enumerable, false);
assertEq(compileDesc.configurable, true);

// 'WebAssembly.compile' function
let compile = WebAssembly.compile;
assertEq(compile, compileDesc.value);
assertEq(compile.length, 1);
assertEq(compile.name, "compile");
function assertCompileError(args, err, msg) {
  var error = null;
  try {
    compile(...args).catch(e => error = e);
  } catch (e) {
    // TODO: error shouldn't be thrown, but should be globbed onto the promise.
    error = e;
  }
  drainJobQueue();
  assertEq(error instanceof err, true);
  assertEq(Boolean(error.stack.match("js-api.js")), true);
//TODO  assertEq(Boolean(error.message.match(msg)), true);
}
assertCompileError([], TypeError, /requires more than 0 arguments/);
//TODO assertCompileError([undefined], TypeError, /first argument must be an ArrayBuffer or typed array object/);
//TODO assertCompileError([1], TypeError, /first argument must be an ArrayBuffer or typed array object/);
//TODO assertCompileError([{}], TypeError, /first argument must be an ArrayBuffer or typed array object/);
//TODO assertCompileError([new Uint8Array()], CompileError, /failed to match magic number/);
//TODO assertCompileError([new ArrayBuffer()], CompileError, /failed to match magic number/);
function assertCompileSuccess(bytes) {
    var module = null;
    compile(bytes).then(m => module = m);
    drainJobQueue();
    assertEq(module instanceof Module, true);
}
assertCompileSuccess(emptyModuleBinary);
assertCompileSuccess(emptyModuleBinary.buffer);

if (false) {  // TODO: implement WebAssembly.instantiate
  // 'WebAssembly.instantiate' data property
  let instantiateDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'instantiate');
  assertEq(typeof instantiateDesc.value, "function");
  assertEq(instantiateDesc.writable, true);
  assertEq(instantiateDesc.enumerable, false);
  assertEq(instantiateDesc.configurable, true);

  // 'WebAssembly.instantiate' function
  let instantiate = WebAssembly.instantiate;
  assertEq(instantiate, instantiateDesc.value);
  assertEq(instantiate.length, 2);
  assertEq(instantiate.name, "instantiate");
  function assertInstantiateError(args, err, msg) {
    var error = null;
    try {
      instantiate(...args).catch(e => error = e);
    } catch(e) {
      error = e;
    }
    drainJobQueue();
    assertEq(error instanceof err, true);
    assertEq(Boolean(error.stack.match("jsapi.js")), true);
    assertEq(Boolean(error.message.match(msg)), true);
  }
  assertInstantiateError([], TypeError, /requires more than 0 arguments/);
  assertInstantiateError([undefined], TypeError, /first argument must be a WebAssembly.Module, ArrayBuffer or typed array object/);
  assertInstantiateError([1], TypeError, /first argument must be a WebAssembly.Module, ArrayBuffer or typed array object/);
  assertInstantiateError([{}], TypeError, /first argument must be a WebAssembly.Module, ArrayBuffer or typed array object/);
  assertInstantiateError([new Uint8Array()], CompileError, /failed to match magic number/);
  assertInstantiateError([new ArrayBuffer()], CompileError, /failed to match magic number/);
  assertInstantiateError([importingModule], TypeError, /second argument must be an object/);
  assertInstantiateError([importingModule, null], TypeError, /second argument must be an object/);
  assertInstantiateError([importingModuleBinary, null], TypeError, /second argument must be an object/);
  function assertInstantiateSuccess(module, imports) {
    var result = null;
    instantiate(module, imports).then(r => result = r);
    drainJobQueue();
    if (module instanceof Module) {
      assertEq(result instanceof Instance, true);
    } else {
      assertEq(result.module instanceof Module, true);
      assertEq(result.instance instanceof Instance, true);
    }
  }
  assertInstantiateSuccess(emptyModule);
  assertInstantiateSuccess(emptyModuleBinary);
  assertInstantiateSuccess(emptyModuleBinary.buffer);
  assertInstantiateSuccess(importingModule, {"":{f:()=>{}}});
  assertInstantiateSuccess(importingModuleBinary, {"":{f:()=>{}}});
  assertInstantiateSuccess(importingModuleBinary.buffer, {"":{f:()=>{}}});
}
