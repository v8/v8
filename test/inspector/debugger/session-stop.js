// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.log('Checks V8InspectorSession::stop');

InspectorTest.runAsyncTestSuite([
  async function testSessionStopResumesPause() {
    let contextGroup = new InspectorTest.ContextGroup();
    let session = contextGroup.connect();
    let Protocol = session.Protocol;

    Protocol.Debugger.enable();
    await Protocol.Debugger.pause();
    const result = Protocol.Runtime.evaluate({expression: '42'});
    contextGroup.stop();
    InspectorTest.log(
        `Evaluation returned: ${(await result).result.result.value}`);
  },
  async function testSessionStopResumesInstrumentationPause() {
    let contextGroup = new InspectorTest.ContextGroup();
    let session = contextGroup.connect();
    let Protocol = session.Protocol;

    Protocol.Debugger.enable();
    await Protocol.Debugger.setInstrumentationBreakpoint(
        {instrumentation: 'beforeScriptExecution'});
    const paused = Protocol.Debugger.oncePaused();
    const result = Protocol.Runtime.evaluate({expression: '42'});
    InspectorTest.log(`Paused: ${(await paused).params.reason}`);
    contextGroup.stop();
    InspectorTest.log(
        `Evaluation returned: ${(await result).result.result.value}`);
  },
  async function testSessionStopDisablesDebugger() {
    let contextGroup = new InspectorTest.ContextGroup();
    let session = contextGroup.connect();
    let Protocol = session.Protocol;

    await Protocol.Debugger.enable();
    contextGroup.stop();
    const pauseResult = await Protocol.Debugger.pause();
    InspectorTest.log(`Pause error(?): ${pauseResult?.error?.message}`);
  },
  async function testSessionStopDisallowsReenabling() {
    let contextGroup = new InspectorTest.ContextGroup();
    let session = contextGroup.connect();
    let Protocol = session.Protocol;

    await Protocol.Debugger.enable();
    contextGroup.stop();
    const pauseResultAfterStop = await Protocol.Debugger.pause();
    InspectorTest.log(
        `Pause error(?) after stop: ${pauseResultAfterStop?.error?.message}`);
    await Protocol.Debugger.enable();
    const pauseResult = await Protocol.Debugger.pause();
    InspectorTest.log(
        `Pause error(?) after re-enable: ${pauseResult?.error?.message}`);
  }
]);
