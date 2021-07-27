// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-eh --experimental-wasm-reftypes

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestImport() {
  print(arguments.callee.name);

  assertThrows(() => new WebAssembly.Tag(), TypeError,
      /Argument 0 must be a tag type/);
  assertThrows(() => new WebAssembly.Tag({}), TypeError,
      /Argument 0 must be a tag type with 'parameters'/);
  assertThrows(() => new WebAssembly.Tag({parameters: ['foo']}), TypeError,
      /Argument 0 parameter type at index #0 must be a value type/);
  assertThrows(() => new WebAssembly.Tag({parameters: {}}), TypeError,
      /Argument 0 contains parameters without 'length'/);

  let js_except_i32 = new WebAssembly.Tag({parameters: ['i32']});
  let js_except_v = new WebAssembly.Tag({parameters: []});
  let builder = new WasmModuleBuilder();
  builder.addImportedException("m", "ex", kSig_v_i);

  assertDoesNotThrow(() => builder.instantiate({ m: { ex: js_except_i32 }}));
  assertThrows(
      () => builder.instantiate({ m: { ex: js_except_v }}), WebAssembly.LinkError,
      /imported tag does not match the expected type/);
  assertThrows(
      () => builder.instantiate({ m: { ex: js_except_v }}), WebAssembly.LinkError,
      /imported tag does not match the expected type/);
})();

(function TestExport() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addException(kSig_v_v);
  builder.addExportOfKind("ex", kExternalException, except);
  let instance = builder.instantiate();

  assertTrue(Object.prototype.hasOwnProperty.call(instance.exports, 'ex'));
  assertEquals("object", typeof instance.exports.ex);
  assertInstanceof(instance.exports.ex, WebAssembly.Tag);
  assertSame(instance.exports.ex.constructor, WebAssembly.Tag);
})();

(function TestImportExport() {
  print(arguments.callee.name);

  let js_ex_i32 = new WebAssembly.Tag({parameters: ['i32']});
  let builder = new WasmModuleBuilder();
  let index = builder.addImportedException("m", "ex", kSig_v_i);
  builder.addExportOfKind("ex", kExternalException, index);

  let instance = builder.instantiate({ m: { ex: js_ex_i32 }});
  let res = instance.exports.ex;
  assertEquals(res, js_ex_i32);
})();


(function TestExceptionConstructor() {
  print(arguments.callee.name);
  // Check errors.
  let js_tag = new WebAssembly.Tag({parameters: []});
  assertThrows(() => new WebAssembly.Exception(0), TypeError,
      /Argument 0 must be a WebAssembly tag/);
  assertThrows(() => new WebAssembly.Exception({}), TypeError,
      /Argument 0 must be a WebAssembly tag/);
  assertThrows(() => WebAssembly.Exception(js_tag), TypeError,
      /WebAssembly.Exception must be invoked with 'new'/);
  let js_exception = new WebAssembly.Exception(js_tag, []);

  // Check prototype.
  assertSame(WebAssembly.Exception.prototype, js_exception.__proto__);
  assertTrue(js_exception instanceof WebAssembly.Exception);

  // Check prototype of a thrown exception.
  let builder = new WasmModuleBuilder();
  let wasm_tag = builder.addException(kSig_v_v);
  builder.addFunction("throw", kSig_v_v)
      .addBody([kExprThrow, wasm_tag]).exportFunc();
  let instance = builder.instantiate();
  try {
    instance.exports.throw();
  } catch (e) {
    assertTrue(e instanceof WebAssembly.Exception);
  }
})();

(function TestExceptionConstructorWithPayload() {
  print(arguments.callee.name);
  let tag = new WebAssembly.Tag(
      {parameters: ['i32', 'f32', 'i64', 'f64', 'externref']});
  assertThrows(() => new WebAssembly.Exception(
      tag, [1n, 2, 3n, 4, {}]), TypeError);
  assertDoesNotThrow(() => new WebAssembly.Exception(tag, [3, 4, 5n, 6, {}]));
})();

(function TestCatchJSException() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let js_tag = new WebAssembly.Tag({parameters: []});
  let js_func_index = builder.addImport('m', 'js_func', kSig_v_v);
  let js_tag_index = builder.addImportedException("m", "js_tag", kSig_v_v);
  let tag_index = builder.addException(kSig_v_v);
  builder.addExportOfKind("wasm_tag", kExternalException, tag_index);
  builder.addFunction("catch", kSig_i_v)
      .addBody([
        kExprTry, kWasmI32,
        kExprCallFunction, js_func_index,
        kExprI32Const, 0,
        kExprCatch, js_tag_index,
        kExprI32Const, 1,
        kExprCatch, tag_index,
        kExprI32Const, 2,
        kExprEnd
      ]).exportFunc();
  let tag;
  function js_func() {
    throw new WebAssembly.Exception(tag, []);
  }
  let instance = builder.instantiate({m: {js_func, js_tag}});
  tag = js_tag;
  assertEquals(1, instance.exports.catch());
  tag = instance.exports.wasm_tag;
  assertEquals(2, instance.exports.catch());
})();

function TestCatchJS(types_str, types, values) {
  // Create a JS exception, catch it in wasm and check the unpacked value(s).
  let builder = new WasmModuleBuilder();
  let js_tag = new WebAssembly.Tag({parameters: types_str});
  let js_func_index = builder.addImport('m', 'js_func', kSig_v_v);
  let sig1 = makeSig(types, []);
  let sig2 = makeSig([], types);
  let js_tag_index = builder.addImportedException("m", "js_tag", sig1);
  let tag_index = builder.addException(sig1);
  let return_type = builder.addType(sig2);
  builder.addExportOfKind("wasm_tag", kExternalException, tag_index);
  builder.addFunction("catch", sig2)
      .addBody([
        kExprTry, return_type,
        kExprCallFunction, js_func_index,
        kExprUnreachable,
        kExprCatch, js_tag_index,
        kExprCatch, tag_index,
        kExprEnd
      ]).exportFunc();
  let exception;
  function js_func() {
    throw exception;
  }
  let expected = values.length == 1 ? values[0] : values;
  let instance = builder.instantiate({m: {js_func, js_tag}});
  exception = new WebAssembly.Exception(js_tag, values);
  assertEquals(expected, instance.exports.catch());
  exception = new WebAssembly.Exception(instance.exports.wasm_tag, values);
  assertEquals(expected, instance.exports.catch());
}

(function TestCatchJSExceptionWithPayload() {
  print(arguments.callee.name);
  TestCatchJS(['i32'], [kWasmI32], [1]);
  TestCatchJS(['i64'], [kWasmI64], [2n]);
  TestCatchJS(['f32'], [kWasmF32], [3]);
  TestCatchJS(['f64'], [kWasmF64], [4]);
  TestCatchJS(['externref'], [kWasmExternRef], [{value: 5}]);
  TestCatchJS(['i32', 'i64', 'f32', 'f64', 'externref'],
              [kWasmI32, kWasmI64, kWasmF32, kWasmF64, kWasmExternRef],
              [6, 7n, 8, 9, {value: 10}]);
})();
