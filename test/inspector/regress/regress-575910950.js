// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Tests that returnByValue serialization charges undefined-valued ' +
    'properties against the protocol value budget.');

contextGroup.addScript(`
  globalThis.o = {};
  for (let i = 0; i < 10000; i++) {
    o['p' + i] = undefined;
  }
`);

(async function test() {
  const response = await Protocol.Runtime.evaluate({
    expression: 'Array(5000).fill(o)',
    returnByValue: true,
  });
  InspectorTest.logMessage(response.error ?? response.result.result);
  InspectorTest.completeTest();
})();
