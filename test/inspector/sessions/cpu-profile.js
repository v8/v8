// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.log('Tests that multiple sessions can record profiles concurrently.');

var contextGroup = new InspectorTest.ContextGroup();
contextGroup.addScript(`
function foo() {
  var doSomeWork = 1;
  for (var i = 0; i < 1000; i++)
    doSomeWork += i;
  return doSomeWork;
}
//# sourceURL=test.js`, 7, 26);

(async function test() {
  var session1 = await connect(contextGroup, 1);
  var session2 = await connect(contextGroup, 2);

  InspectorTest.log('console.profile in 1');
  await session1.Protocol.Runtime.evaluate({expression: 'console.profile("one"); foo(); console.profileEnd("one");'});
  InspectorTest.log('console.profile in 2');
  await session2.Protocol.Runtime.evaluate({expression: 'console.profile("two"); foo(); console.profileEnd("two");'});

  InspectorTest.log('starting in 1');
  session1.Protocol.Profiler.start();
  InspectorTest.log('starting in 2');
  session2.Protocol.Profiler.start();
  await session1.Protocol.Runtime.evaluate({expression: 'foo();'});

  InspectorTest.log('stopping in 1');
  var message = await session1.Protocol.Profiler.stop();
  InspectorTest.log('stopped in 1');

  InspectorTest.log('stopping in 2');
  var message = await session2.Protocol.Profiler.stop();
  InspectorTest.log('stopped in 2');

  InspectorTest.completeTest();
})();

async function connect(contextGroup, num) {
  var session = contextGroup.connect();
  session.Protocol.Profiler.onConsoleProfileStarted(message => {
    InspectorTest.log(`console profile started from ${num}: ${message.params.title}`);
  });
  session.Protocol.Profiler.onConsoleProfileFinished(message => {
    InspectorTest.log(`console profile finished from ${num}: ${message.params.title}`);
  });
  await session.Protocol.Profiler.enable();
  await session.Protocol.Profiler.setSamplingInterval({interval: 100});
  return session;
}
