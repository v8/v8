// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --debug-in-liftoff

let {session, contextGroup, Protocol} =
    InspectorTest.start('Tests stepping through wasm scripts by byte offsets');

utils.load('test/mjsunit/wasm/wasm-module-builder.js');

var builder = new WasmModuleBuilder();

var func_a_idx =
    builder.addFunction('wasm_A', kSig_v_i).addBody([kExprNop, kExprNop]).index;

// wasm_B calls wasm_A <param0> times.
builder.addFunction('wasm_B', kSig_v_i)
    .addBody([
      // clang-format off
      kExprLoop, kWasmStmt,               // while
        kExprLocalGet, 0,                 // -
        kExprIf, kWasmStmt,               // if <param0> != 0
          kExprLocalGet, 0,               // -
          kExprI32Const, 1,               // -
          kExprI32Sub,                    // -
          kExprLocalSet, 0,               // decrease <param0>
          ...wasmI32Const(1024),          // some longer i32 const (2 byte imm)
          kExprCallFunction, func_a_idx,  // -
          kExprBr, 1,                     // continue
          kExprEnd,                       // -
        kExprEnd,                         // break
      // clang-format on
    ])
    .exportAs('main');


var module_bytes = builder.toArray();

function instantiate(bytes) {
  var buffer = new ArrayBuffer(bytes.length);
  var view = new Uint8Array(buffer);
  for (var i = 0; i < bytes.length; ++i) {
    view[i] = bytes[i] | 0;
  }

  var module = new WebAssembly.Module(buffer);
  // Set global variable.
  instance = new WebAssembly.Instance(module);
}

(async function test() {
  for (const action of ['stepInto', 'stepOver', 'stepOut', 'resume'])
    InspectorTest.logProtocolCommandCalls('Debugger.' + action);

  await Protocol.Debugger.enable();
  InspectorTest.log('Setting up global instance variable.');
  Protocol.Runtime.evaluate({
    expression: `var instance;` +
        `(${instantiate.toString()})(${JSON.stringify(module_bytes)})`
  });
  const [, {params: wasmScript}] = await Protocol.Debugger.onceScriptParsed(2);

  InspectorTest.log('Got wasm script: ' + wasmScript.url);

  // Set the breakpoint on a non-breakable position. This should resolve to the
  // next instruction.
  InspectorTest.log(
      `Setting breakpoint on offset 59 (should be propagated to 60, the ` +
      `offset of the call), url ${wasmScript.url}`);
  const bpmsg = await Protocol.Debugger.setBreakpoint({
    location: {scriptId: wasmScript.scriptId, lineNumber: 0, columnNumber: 59}
  });

  const actualLocation = bpmsg.result.actualLocation;
  InspectorTest.logMessage(actualLocation);
  Protocol.Runtime.evaluate({ expression: 'instance.exports.main(4)' });
  await waitForPauseAndStep('stepOver');  // over call to wasm_A
  await waitForPauseAndStep('resume');    // stop on breakpoint
  await waitForPauseAndStep('stepOver');  // over call
  await waitForPauseAndStep('stepOver');  // over br
  await waitForPauseAndStep('resume');    // to next breakpoint (3rd iteration)
  await waitForPauseAndStep('stepOver');  // over wasm_A
  // Step over 10 times.
  for (let i = 0; i < 10; ++i) await waitForPauseAndStep('stepOver');
  // Then just resume.
  await waitForPauseAndStep('resume');
  InspectorTest.log('exports.main returned!');
  InspectorTest.log('Finished!');
})().catch(reason => InspectorTest.log(`Failed: ${reason}`))
    .finally(InspectorTest.completeTest);

async function waitForPauseAndStep(stepAction) {
  const {params: {callFrames}} = await Protocol.Debugger.oncePaused();
  const topFrame = callFrames[0];
  InspectorTest.log(
      `Paused at ${topFrame.url}:${topFrame.location.lineNumber}:${topFrame.location.columnNumber}`);
  Protocol.Debugger[stepAction]();
}
