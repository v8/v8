// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
    InspectorTest.start('stepOut async function');

session.setupScriptMap();
contextGroup.addInlineScript(`
async function test() {
  await Promise.resolve();
  await foo();
}

async function foo() {
  await Promise.resolve();
  await bar();
}

async function bar() {
  await Promise.resolve();
  debugger;
}
`, 'test.js');

(async function test() {
  Protocol.Runtime.enable();
  Protocol.Runtime.onConsoleAPICalled(
      msg => InspectorTest.log(msg.params.args[0].value));
  Protocol.Debugger.enable();
  Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 128});
  let finished =
      Protocol.Runtime.evaluate({expression: 'test()', awaitPromise: true})
          .then(() => false);
  while (true) {
    const r = await Promise.race([finished, waitPauseAndDumpStack()]);
    if (!r) break;
    Protocol.Debugger.stepOut();
  }
  InspectorTest.completeTest();
})()

    async function
    waitPauseAndDumpStack() {
      const {params} = await Protocol.Debugger.oncePaused();
      session.logCallFrames(params.callFrames);
      session.logAsyncStackTrace(params.asyncStackTrace);
      InspectorTest.log('');
      return true;
    }
