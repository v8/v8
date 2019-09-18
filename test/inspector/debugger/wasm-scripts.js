// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

InspectorTest.log("Tests how wasm scripts are reported");

let contextGroup = new InspectorTest.ContextGroup();
let sessions = [
  // Main session.
  trackScripts(),
  // Extra session to verify that all inspectors get same messages.
  // See https://bugs.chromium.org/p/v8/issues/detail?id=9725.
  trackScripts(),
];

utils.load('test/mjsunit/wasm/wasm-module-builder.js');

// Add two empty functions. Both should be registered as individual scripts at
// module creation time.
var builder = new WasmModuleBuilder();
builder.addFunction('nopFunction', kSig_v_v).addBody([kExprNop]);
builder.addFunction('main', kSig_v_v)
    .addBody([kExprBlock, kWasmStmt, kExprI32Const, 2, kExprDrop, kExprEnd])
    .exportAs('main');
var module_bytes = builder.toArray();

function testFunction(bytes) {
  // Compilation triggers registration of wasm scripts.
  new WebAssembly.Module(new Uint8Array(bytes));
}

contextGroup.addScript(testFunction.toString(), 0, 0, 'v8://test/testFunction');
contextGroup.addScript('var module_bytes = ' + JSON.stringify(module_bytes));

InspectorTest.log(
    'Check that each inspector gets two wasm scripts at module creation time.');

sessions[0].Protocol.Runtime
    .evaluate({
      'expression': '//# sourceURL=v8://test/runTestRunction\n' +
          'testFunction(module_bytes)'
    })
    .then(() => (
      // At this point all scripts were parsed.
      // Stop tracking and wait for script sources in each session.
      Promise.all(sessions.map(session => session.getScripts()))
    ))
    .catch(err => {
      InspectorTest.log("FAIL: " + err.message);
    })
    .then(() => InspectorTest.completeTest());

function trackScripts() {
  let {id: sessionId, Protocol} = contextGroup.connect();
  let scripts = [];

  Protocol.Debugger.enable();
  Protocol.Debugger.onScriptParsed(handleScriptParsed);

  async function getScript({url, scriptId}) {
    let {result: {scriptSource}} = await Protocol.Debugger.getScriptSource({scriptId});
    InspectorTest.log(`Session #${sessionId}: Source for ${url}:`);
    InspectorTest.log(scriptSource);
    return {url, scriptSource};
  }

  function handleScriptParsed({params}) {
    let {url} = params;
    if (!url.startsWith("wasm://")) return;

    InspectorTest.log(`Session #${sessionId}: Script #${scripts.length} parsed. URL: ${url}`);
    scripts.push(getScript(params));
  }

  return {
    Protocol,
    getScripts: () => Promise.all(scripts),
  };
}
