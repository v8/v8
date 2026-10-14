// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Tests that an async WebAssembly.compile resolution after session ' +
    'disconnect does not crash.');

const source = `
const kMinimalWasm = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d,
  0x01, 0x00, 0x00, 0x00,
]);
let compiledCount = 0;
function triggerCompile(sessionId) {
  const promise = WebAssembly.compile(kMinimalWasm).then((mod) => {
    if (mod instanceof WebAssembly.Module) {
      ++compiledCount;
    }
  });
  if (sessionId !== undefined) {
    inspector.disconnectSession(sessionId);
  }
  return promise;
}
`;

(async function test() {
  contextGroup.addScript(source);
  Protocol.Runtime.evaluate({expression: `triggerCompile(${session.id})`});
  InspectorTest._sessions.delete(session);

  const session2 = contextGroup.connect();
  session2.Protocol.Runtime.evaluate({expression: 'triggerCompile()'});
  await InspectorTest.waitForPendingTasks();
  session.sendRawCommand(1, '{"id":1,"method":"Runtime.enable"}', () => {});
  const {result: {result}} = await session2.Protocol.Runtime.evaluate({
    expression: 'compiledCount',
    returnByValue: true,
  });
  InspectorTest.log('compiledCount: ' + result.value);
  InspectorTest.completeTest();
})();
