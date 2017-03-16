// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-interpret-all --allow-natives-syntax

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

// The stack trace contains file path, only keep "interpreter.js".
let stripPath = s => s.replace(/[^ (]*interpreter\.js/g, 'interpreter.js');

function checkStack(stack, expected_lines) {
  print('stack: ' + stack);
  var lines = stack.split('\n');
  assertEquals(expected_lines.length, lines.length);
  for (var i = 0; i < lines.length; ++i) {
    let test =
        typeof expected_lines[i] == 'string' ? assertEquals : assertMatches;
    test(expected_lines[i], lines[i], 'line ' + i);
  }
}

(function testCallImported() {
  var stack;
  let func = () => stack = new Error('test imported stack').stack;

  var builder = new WasmModuleBuilder();
  builder.addImport('mod', 'func', kSig_v_v);
  builder.addFunction('main', kSig_v_v)
      .addBody([kExprCallFunction, 0])
      .exportFunc();
  var instance = builder.instantiate({mod: {func: func}});
  // Test that this does not mess up internal state by executing it three times.
  for (var i = 0; i < 3; ++i) {
    var interpreted_before = % WasmNumInterpretedCalls(instance);
    instance.exports.main();
    assertEquals(interpreted_before + 1, % WasmNumInterpretedCalls(instance));
    checkStack(stripPath(stack), [
      'Error: test imported stack',                           // -
      /^    at func \(interpreter.js:\d+:28\)$/,              // -
      '    at main (<WASM>[1]+1)',                            // -
      /^    at testCallImported \(interpreter.js:\d+:22\)$/,  // -
      /^    at interpreter.js:\d+:3$/
    ]);
  }
})();

(function testCallImportedWithParameters() {
  var stack;
  var passed_args = [];
  let func1 = (i, j) => (passed_args.push(i, j), 2 * i + j);
  let func2 = (f) => (passed_args.push(f), 8 * f);

  var builder = new WasmModuleBuilder();
  builder.addImport('mod', 'func1', makeSig([kWasmI32, kWasmI32], [kWasmF32]));
  builder.addImport('mod', 'func2', makeSig([kWasmF64], [kWasmI32]));
  builder.addFunction('main', makeSig([kWasmI32, kWasmF64], [kWasmF32]))
      .addBody([
        // call #0 with arg 0 and arg 0 + 1
        kExprGetLocal, 0, kExprGetLocal, 0, kExprI32Const, 1, kExprI32Add,
        kExprCallFunction, 0,
        // call #1 with arg 1
        kExprGetLocal, 1, kExprCallFunction, 1,
        // convert returned value to f32
        kExprF32UConvertI32,
        // add the two values
        kExprF32Add
      ])
      .exportFunc();
  var instance = builder.instantiate({mod: {func1: func1, func2: func2}});
  var interpreted_before = % WasmNumInterpretedCalls(instance);
  var args = [11, 0.3];
  var ret = instance.exports.main(...args);
  assertEquals(interpreted_before + 1, % WasmNumInterpretedCalls(instance));
  var passed_test_args = [...passed_args];
  var expected = func1(args[0], args[0] + 1) + func2(args[1]) | 0;
  assertEquals(expected, ret);
  assertArrayEquals([args[0], args[0] + 1, args[1]], passed_test_args);
})();

(function testTrap() {
  var builder = new WasmModuleBuilder();
  var foo_idx = builder.addFunction('foo', kSig_v_v)
                    .addBody([kExprNop, kExprNop, kExprUnreachable])
                    .index;
  builder.addFunction('main', kSig_v_v)
      .addBody([kExprNop, kExprCallFunction, foo_idx])
      .exportFunc();
  var instance = builder.instantiate();
  // Test that this does not mess up internal state by executing it three times.
  for (var i = 0; i < 3; ++i) {
    var interpreted_before = % WasmNumInterpretedCalls(instance);
    var stack;
    try {
      instance.exports.main();
      assertUnreachable();
    } catch (e) {
      stack = e.stack;
    }
    assertEquals(interpreted_before + 2, % WasmNumInterpretedCalls(instance));
    checkStack(stripPath(stack), [
      'RuntimeError: unreachable',                    // -
      '    at foo (<WASM>[0]+3)',                     // -
      '    at main (<WASM>[1]+2)',                    // -
      /^    at testTrap \(interpreter.js:\d+:24\)$/,  // -
      /^    at interpreter.js:\d+:3$/
    ]);
  }
})();
