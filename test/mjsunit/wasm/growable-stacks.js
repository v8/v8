// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-growable-stacks
// Flags: --expose-gc --wasm-staging --stack-size=400

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function flatRange(upperBound, gen) {
  const res = [];
  for (let i = 0; i < upperBound; i++) {
    res.push(gen(i));
  }
  return res.flat();
}

function growAndShrinkTwice(depth, paramType, constFn, addOp, result, heavy = false) {
  const builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true).exportAs('depth');
  // To cover the large frame stack check, make the import return 512 extra
  // i64s. This makes the frame statically bigger than 4kb on both compilers.
  let extra_returns = heavy ? 512 : 0;
  const numIntArgs = 10;
  const numTypes = Array(numIntArgs).fill(paramType);
  var sig = makeSig(numTypes, numTypes);
  let import_index = builder.addImport("m", "import",
      makeSig(numTypes, numTypes.concat(Array(extra_returns).fill(kWasmI64))));
  let sig_if = builder.addType(makeSig([], numTypes));
  let deep_calc = builder.addFunction("deep_calc", sig);
  deep_calc
    .addBody([
      // decrement global
      kExprGlobalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprGlobalSet, 0,
      // should we recurse deeper?
      kExprGlobalGet, 0,
      kExprIf, sig_if,
      // then
      kExprLocalGet, 9,
      ...constFn(1),
      addOp,
      ...flatRange(numIntArgs - 1, i => [kExprLocalGet, i]),
      kExprCallFunction, deep_calc.index,
      // else
      kExprElse,
      ...flatRange(numIntArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, import_index,
      ...Array(extra_returns).fill(kExprDrop),
      kExprEnd
    ])
  builder.addFunction("test", makeSig([], numTypes))
    .addBody([
      // do stack expensive job
      ...flatRange(numIntArgs, constFn),
      kExprCallFunction, deep_calc.index,
      ...flatRange(numIntArgs, () => kExprDrop),
      // Reset the recursion budget.
      ...wasmI32Const(depth),
      kExprGlobalSet, 0,
      // do more stack expensive job
      // to check limits was updated properly on shrink
      ...flatRange(numIntArgs, constFn),
      kExprCallFunction, deep_calc.index,
    ]).exportFunc();
  const js_import = new WebAssembly.Suspending((...args) => {
    return Promise.resolve(args.concat(Array(extra_returns).fill(0n)))
  })
  const instance = builder.instantiate({ m: { import: js_import } });
  const wrapper = WebAssembly.promising(instance.exports.test);
  instance.exports.depth.value = depth;
  assertPromiseResult(
    wrapper(),
    r => result.forEach((v, i) => assertEquals(r[i], v))
  );
}

(function TestStackGrowI32() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    430,
    kWasmI32,
    wasmI32Const,
    kExprI32Add,
    [44, 45, 46, 47, 48, 49, 50, 51, 52, 42]
  );
})();

(function TestStackGrowI64() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    430,
    kWasmI64,
    wasmI64Const,
    kExprI64Add,
    [44n, 45n, 46n, 47n, 48n, 49n, 50n, 51n, 52n, 42n]
  );
})();

(function TestStackGrowF32() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    430,
    kWasmF32,
    wasmF32Const,
    kExprF32Add,
    [44, 45, 46, 47, 48, 49, 50, 51, 52, 42]
  );
})();

(function TestStackGrowF64() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    430,
    kWasmF64,
    wasmF64Const,
    kExprF64Add,
    [44, 45, 46, 47, 48, 49, 50, 51, 52, 42]
  );
})();

(function TestStackGrowHeavyFrameI32() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    20,
    kWasmI32,
    wasmI32Const,
    kExprI32Add,
    [3, 4, 5, 6, 7, 8, 9, 10, 11, 1],
    true
  );
})();

(function TestStackGrowHeavyFrameI64() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    20,
    kWasmI64,
    wasmI64Const,
    kExprI64Add,
    [3n, 4n, 5n, 6n, 7n, 8n, 9n, 10n, 11n, 1n],
    true
  );
})();

(function TestStackGrowHeavyFrameF32() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    20,
    kWasmF32,
    wasmF32Const,
    kExprF32Add,
    [3, 4, 5, 6, 7, 8, 9, 10, 11, 1],
    true
  );
})();

(function TestStackGrowHeavyFrameF64() {
  print(arguments.callee.name);
  growAndShrinkTwice(
    20,
    kWasmF64,
    wasmF64Const,
    kExprF64Add,
    [3, 4, 5, 6, 7, 8, 9, 10, 11, 1],
    true
  );
})();

(function TestStackGrowWithGCAndRefParams() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const call_stack_depth = 430;
  builder.addGlobal(kWasmI32, true).exportAs('depth');
  const numRefArgs = 10;
  const refTypes = Array(numRefArgs).fill(kWasmExternRef);
  var sig = makeSig(refTypes, refTypes);
  const imp = builder.addImport('m', 'import', sig);
  let sig_if = builder.addType(makeSig([], refTypes));
  let deep_calc = builder.addFunction("deep_calc", sig);
  deep_calc.addBody([
      // decrement global
      kExprGlobalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprGlobalSet, 0,
      // should we recurse deeper?
      kExprGlobalGet, 0,
      kExprIf, sig_if,
      // then
      ...flatRange(numRefArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, deep_calc.index,
      // else
      kExprElse,
      ...flatRange(numRefArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, imp,
      kExprEnd
    ]);
  builder.addFunction("test", makeSig([kWasmExternRef], refTypes))
    .addBody([
      // do stack expensive job
      ...flatRange(numRefArgs, () => [kExprLocalGet, 0]),
      kExprCallFunction, deep_calc.index,
      ...flatRange(numRefArgs, () => kExprDrop),
      // Reset the recursion budget.
      ...wasmI32Const(call_stack_depth),
      kExprGlobalSet, 0,
      // do more stack expensive job
      // to check limits was updated properly on shrink
      ...flatRange(numRefArgs, () => [kExprLocalGet, 0]),
      kExprCallFunction, deep_calc.index,
    ]).exportFunc();
  const js_import = new WebAssembly.Suspending((...args) => {
    gc();
    return Promise.resolve(args)
  });
  const instance = builder.instantiate({ m: { import: js_import } });
  const wrapper = WebAssembly.promising(instance.exports.test);
  instance.exports.depth.value = call_stack_depth;
  const ref = {};
  assertPromiseResult(
    wrapper(ref),
    res => res.forEach(r => assertEquals(r, ref))
  );
})();

(function TestStackOverflow() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("test", kSig_i_v)
      .addBody([
          kExprCallFunction, 0
          ]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = WebAssembly.promising(instance.exports.test);
  assertThrowsAsync(wrapper(), RangeError, /Maximum call stack size exceeded/);
})();

(function TestStackOverflowWithHeavyFrames() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("test", kSig_i_r)
      .addLocals(kWasmI64, 512) // make a frame bigger than 4kb
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, 0
          ]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = WebAssembly.promising(instance.exports.test);
  assertThrowsAsync(wrapper(), RangeError, /Maximum call stack size exceeded/);
})();

// Test growing the stack for a frame larger than the next default segment size.
(function TestVeryLargeFrame() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let main = builder.addFunction("main", kSig_v_v);
  main.addLocals(kWasmI64, 8000);
  main.addBody([
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  WebAssembly.promising(wasm.main)();
})();

(function TestMaxParameters() {
  print(arguments.callee.name);
  let types = [kWasmI32, kWasmI64, kWasmS128];
  let default_value = [[kExprI32Const, 0], [kExprI64Const, 0],
                       [kExprI32Const, 0, kSimdPrefix, kExprI32x4Splat]];
  for (let i = 0; i < types.length; ++i) {
    let builder = new WasmModuleBuilder();
    let sig = builder.addType(makeSig(Array(kSpecMaxFunctionParams).fill(types[i]), []));
    let g = builder.addFunction('g', sig).addBody([]);
    let f = builder.addFunction('f', kSig_v_v);
    let f_body = [];
    for (let j = 0; j < kSpecMaxFunctionParams; j++) {
      f_body.push(...default_value[i]);
    }
    f_body.push(kExprCallFunction, g.index);
    f.addBody(f_body).exportFunc();
    let instance = builder.instantiate();
    WebAssembly.promising(instance.exports.f)();
  }
})();
