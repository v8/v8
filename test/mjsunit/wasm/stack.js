// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

// The stack trace contains file path, only keep "stack.js".
function stripPath(s) {
  return s.replace(/[^ (]*stack\.js/g, "stack.js");
}

function verifyStack(frames, expected) {
  assertEquals(expected.length, frames.length, "number of frames mismatch");
  expected.forEach(function(exp, i) {
    assertEquals(exp[0], frames[i].getFunctionName(),
        "["+i+"].getFunctionName()");
    assertEquals(exp[1], frames[i].getLineNumber(),
        "["+i+"].getLineNumber()");
    assertContains(exp[2], frames[i].getFileName(),
        "["+i+"].getFileName()");
    assertContains(exp[3], frames[i].toString(),
        "["+i+"].toString()");
  });
}


var stack;
function STACK() {
  var e = new Error();
  stack = e.stack;
}

var builder = new WasmModuleBuilder();

builder.addImport("func", kSig_v_v);

builder.addFunction("main", kSig_v_v)
  .addBody([kExprCallImport, kArity0, 0])
  .exportAs("main");

builder.addFunction("exec_unreachable", kSig_v_v)
  .addBody([kExprUnreachable])
  .exportAs("exec_unreachable");

// make this function unnamed, just to test also this case
var mem_oob_func = builder.addFunction(undefined, kSig_v_v)
  // access the memory at offset -1
  .addBody([kExprI32Const, 0x7f, kExprI32LoadMem8S, 0, 0])
  .exportAs("mem_out_of_bounds");

// call the mem_out_of_bounds function, in order to have two WASM stack frames
builder.addFunction("call_mem_out_of_bounds", kSig_v_v)
  .addBody([kExprCallFunction, kArity0, mem_oob_func.index])
  .exportAs("call_mem_out_of_bounds");

var module = builder.instantiate({func: STACK});

(function testSimpleStack() {
  var expected_string = "Error\n" +
    // The line numbers below will change as this test gains / loses lines..
    "    at STACK (stack.js:33:11)\n" +           // --
    "    at <WASM> (<anonymous>)\n" +             // TODO(jfb): wasm stack here.
    "    at testSimpleStack (stack.js:70:18)\n" + // --
    "    at stack.js:72:3";                       // --

  module.exports.main();
  assertEquals(expected_string, stripPath(stack));
})();

// For the remaining tests, collect the Callsite objects instead of just a
// string:
Error.prepareStackTrace = function(error, frames) {
  return frames;
};

(function testStackFrames() {
  module.exports.main();

  // TODO(clemensh): add a isWasm() method or similar, and test it
  verifyStack(stack, [
      //        function   line        file          toString
      [          "STACK",    33, "stack.js", "stack.js:33:11"],
      [         "<WASM>",  null,       null,         "<WASM>"],
      ["testStackFrames",    81, "stack.js", "stack.js:81:18"],
      [             null,    91, "stack.js",  "stack.js:91:3"]
  ]);
})();

(function testWasmUnreachable() {
  try {
    module.exports.exec_unreachable();
    fail("expected wasm exception");
  } catch (e) {
    assertContains("unreachable", e.message);
    verifyStack(e.stack, [
        //            function   line        file           toString
        [             "<WASM>",  null,       null,          "<WASM>"],
        ["testWasmUnreachable",    95, "stack.js",  "stack.js:95:20"],
        [                 null,   106, "stack.js",  "stack.js:106:3"]
    ]);
  }
})();

(function testWasmMemOutOfBounds() {
  try {
    module.exports.call_mem_out_of_bounds();
    fail("expected wasm exception");
  } catch (e) {
    assertContains("out of bounds", e.message);
    verifyStack(e.stack, [
        //               function   line        file           toString
        [                "<WASM>",  null,       null,          "<WASM>"],
        [                "<WASM>",  null,       null,          "<WASM>"],
        ["testWasmMemOutOfBounds",   110, "stack.js", "stack.js:110:20"],
        [                    null,   122, "stack.js",  "stack.js:122:3"]
    ]);
  }
})();
