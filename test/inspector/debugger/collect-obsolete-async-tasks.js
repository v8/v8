// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.log('Checks that we collect obsolete async tasks with async stacks.');

InspectorTest.addScript(`
function test() {
  setMaxAsyncTaskStacks(128);
  var p = Promise.resolve();

  dumpAsyncTaskStacksStateForTest();
  setMaxAsyncTaskStacks(128);
  dumpAsyncTaskStacksStateForTest();

  p.then(() => 42).then(() => 239);

  dumpAsyncTaskStacksStateForTest();
  setMaxAsyncTaskStacks(128);
  dumpAsyncTaskStacksStateForTest();

  setTimeout(() => 42, 0);

  dumpAsyncTaskStacksStateForTest();
  setMaxAsyncTaskStacks(128);
  dumpAsyncTaskStacksStateForTest();
}
`);

(async function test() {
  Protocol.Debugger.enable();
  Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 128});
  await Protocol.Runtime.evaluate({expression: 'test()'});
  InspectorTest.completeTest();
})()
