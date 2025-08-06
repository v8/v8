// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-wasmfx --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestContNew() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let cont_index = builder.addCont(kSig_v_v);
  let gc_index = builder.addImport('m', 'gc', kSig_v_v);
  let callee = builder.addFunction('callee', kSig_v_v).addBody([]).exportFunc();
  builder.addFunction("main", kSig_v_v)
      .addBody([
          kExprRefFunc, callee.index,
          kExprContNew, cont_index,
          kExprCallFunction, gc_index,
          kExprResume, cont_index, 0,
      ]).exportFunc();
  let instance = builder.instantiate({m: {gc}});
  instance.exports.main();
})();

(function TestResumeSuspendReturn() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let cont_index = builder.addCont(kSig_v_i);
  let tag_index = builder.addTag(kSig_v_v);
  let suspend_if = builder.addFunction('suspend_if', kSig_v_i)
      .addBody([
          kExprLocalGet, 0,
          kExprIf, kWasmVoid,
            kExprSuspend, tag_index,
          kExprEnd,
      ]).exportFunc();
  const kSuspended = 0;
  const kReturned = 1;
  builder.addFunction("main", kSig_i_i)
      .addBody([
          kExprBlock, kWasmVoid,
            kExprLocalGet, 0,
            kExprRefFunc, suspend_if.index,
            kExprContNew, cont_index,
            kExprResume, cont_index, 1, kOnSuspend, tag_index, 0,
            kExprI32Const, kReturned,
            kExprReturn,
          kExprEnd,
          kExprI32Const, kSuspended,
      ]).exportFunc();
  assertTrue(WebAssembly.validate(builder.toBuffer()));
  // let instance = builder.instantiate();
  // assertEquals(kReturned, instance.exports.main(0));
  // assertEquals(kSuspended, instance.exports.main(1));
})();
