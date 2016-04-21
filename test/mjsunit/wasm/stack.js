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

builder.addImport("func", [kAstStmt]);

builder.addFunction("main", [kAstStmt])
  .addBody([kExprCallImport, 0])
  .exportAs("main");

var module = builder.instantiate({func: STACK});

(function testSimpleStack() {
  var expected_string = "Error\n" +
    // The line numbers below will change as this test gains / loses lines..
    "    at STACK (stack.js:33:11)\n" +           // --
    "    at <WASM> (<anonymous>)\n" +             // TODO(jfb): wasm stack here.
    "    at testSimpleStack (stack.js:55:18)\n" + // --
    "    at stack.js:57:3";                       // --

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
      ["testStackFrames",    66, "stack.js", "stack.js:66:18"],
      [             null,    76, "stack.js",  "stack.js:76:3"]
  ]);
})();
