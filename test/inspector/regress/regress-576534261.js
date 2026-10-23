// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('Regression test for crbug.com/576534261.');

contextGroup.addScript(`
let M = 60000;
let N = 20000;
globalThis.s = 'x'.repeat(N);
globalThis.arr = Array.from({length: M}, () => new RegExp(s));
`);

(async () => {
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: 'arr',
    generatePreview: false,
    serializationOptions: {
      serialization: 'deep',
    },
  }));
  InspectorTest.completeTest();
})();
