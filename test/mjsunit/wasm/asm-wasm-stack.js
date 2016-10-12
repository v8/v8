// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --validate-asm --allow-natives-syntax

var filename = '(?:[^ ]+/)?test/mjsunit/wasm/asm-wasm-stack.js';
filename = filename.replace(/\//g, '[/\\\\]');

function checkPreformattedStack(e, expected_lines) {
  print('preformatted stack: ' + e.stack);
  var lines = e.stack.split('\n');
  assertEquals(expected_lines.length, lines.length);
  for (var i = 0; i < lines.length; ++i) {
    assertMatches(expected_lines[i], lines[i], 'line ' + i);
  }
}

function checkFunctionsOnCallsites(e, locations) {
  var stack = e.stack;
  print('callsite objects (size ' + stack.length + '):');
  for (var i = 0; i < stack.length; ++i) {
    var s = stack[i];
    print(
        ' [' + i + '] ' + s.getFunctionName() + ' (' + s.getFileName() + ':' +
        s.getLineNumber() + ':' + s.getColumnNumber() + ')');
  }
  assertEquals(locations.length, stack.length, 'stack size');
  for (var i = 0; i < locations.length; ++i) {
    var cs = stack[i];
    assertMatches('^' + filename + '$', cs.getFileName(), 'file name at ' + i);
    assertEquals(
        locations[i][0], cs.getFunctionName(), 'function name at ' + i);
    assertEquals(locations[i][1], cs.getLineNumber(), 'line number at ' + i);
    assertEquals(
        locations[i][2], cs.getColumnNumber(), 'column number at ' + i);
    assertNotNull(cs.getThis(), 'receiver should be global');
    assertEquals(stack[0].getThis(), cs.getThis(), 'receiver should be global');
  }
}

function throwException() {
  throw new Error('exception from JS');
}

function generateWasmFromAsmJs(stdlib, foreign, heap) {
  'use asm';
  var throwFunc = foreign.throwFunc;
  function callThrow() {
    throwFunc();
  }
  function redirectFun() {
    callThrow();
  }
  return redirectFun;
}

(function PreformattedStackTraceFromJS() {
  var fun = generateWasmFromAsmJs(this, {throwFunc: throwException}, undefined);
  var e = null;
  try {
    fun();
  } catch (ex) {
    e = ex;
  }
  assertInstanceof(e, Error, 'exception should have been thrown');
  checkPreformattedStack(e, [
    '^Error: exception from JS$',
    '^ *at throwException \\(' + filename + ':43:9\\)$',
    '^ *at callThrow \\(' + filename + ':50:5\\)$',
    '^ *at redirectFun \\(' + filename + ':53:5\\)$',
    '^ *at PreformattedStackTraceFromJS \\(' + filename + ':62:5\\)$',
    '^ *at ' + filename + ':75:3$'
  ]);
})();

// Now collect the Callsite objects instead of just a string.
Error.prepareStackTrace = function(error, frames) {
  return frames;
};

(function CallsiteObjectsFromJS() {
  var fun = generateWasmFromAsmJs(this, {throwFunc: throwException}, undefined);
  var e = null;
  try {
    fun();
  } catch (ex) {
    e = ex;
  }
  assertInstanceof(e, Error, 'exception should have been thrown');
  checkFunctionsOnCallsites(e, [
    ['throwException', 43, 9],         // --
    ['callThrow', 50, 5],              // --
    ['redirectFun', 53, 5],            // --
    ['CallsiteObjectsFromJS', 86, 5],  // --
    [null, 98, 3]
  ]);
})();
