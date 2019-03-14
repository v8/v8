// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks that inspector collects old collected scripts.\n');

(async function main() {
  const maxCollectedScriptsSize = 10e6;
  Protocol.Debugger.enable({maxCollectedScriptsSize});
  const scriptIds = [];
  Protocol.Debugger.onScriptParsed(message => scriptIds.push(message.params.scriptId));

  InspectorTest.log('Generate 5 scripts 1MB each');
  await Protocol.Runtime.evaluate({
    expression: `for (let i = 0; i < 5; ++i) {
      eval("'" + new Array(1e5).fill(12345).join('') + "'.length");
    }`
  });

  const aScriptId = scriptIds[scriptIds.length - 1];

  InspectorTest.log('Generate 30 more scripts 1MB each');
  await Protocol.Runtime.evaluate({
    expression: `for (let i = 0; i < 30; ++i) {
      eval("'" + new Array(1e5).fill(12345).join('') + "'.length");
    }`
  });

  await Protocol.HeapProfiler.collectGarbage();

  InspectorTest.log('Check that latest script is still available');
  let result = await Protocol.Debugger.getScriptSource({scriptId: scriptIds[scriptIds.length - 1]});
  InspectorTest.logMessage(`Last script length: ${result.result && result.result.scriptSource.length}`);

  InspectorTest.log('Check that an earlier script is not available');
  result = await Protocol.Debugger.getScriptSource({scriptId: aScriptId});
  InspectorTest.logMessage(`Script is not found: ${result.error && result.error.message.includes('No script for id')}`);

  InspectorTest.completeTest();
})();
