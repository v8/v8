// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Tests that a debug-evaluate store over a leaked TdzHole does not ' +
    'install a ContextCell in a context slot without a cell.');

// `param` is context-allocated but never assigned in the source, so compiled
// code reads its slot without expecting a ContextCell.
contextGroup.addScript(`
function host(param) {
  globalThis.readParam = () => param;
  debugger;
}`);

(async function test() {
  await Protocol.Debugger.enable();

  // %LeakHole(4) is the TdzHole.
  Protocol.Runtime.evaluate({expression: 'host(%LeakHole(4))'});
  const {params: {callFrames: [{callFrameId}]}} =
      await Protocol.Debugger.oncePaused();
  await Protocol.Debugger.evaluateOnCallFrame(
      {callFrameId, expression: 'param = 42'});
  await Protocol.Debugger.resume();

  const {result: {result}} =
      await Protocol.Runtime.evaluate({expression: 'readParam() === 42'});
  InspectorTest.log(`readParam() === 42: ${result.value}`);

  await Protocol.Debugger.disable();
  InspectorTest.completeTest();
})();
