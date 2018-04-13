// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {Protocol} = InspectorTest.start(
    `Tests that Runtime.evaluate's timeout argument`);

(async function test(){
  {
    InspectorTest.log('Run trivial expression:');
    const {error:{message}} = await Protocol.Runtime.evaluate({
      expression: 'function foo() {} foo()',
      timeout: 0
    });
    InspectorTest.log(message);
  }
  {
    InspectorTest.log('Run expression without interrupts:');
    const {result:{result}} = await Protocol.Runtime.evaluate({
      expression: '',
      timeout: 0
    });
    InspectorTest.logMessage(result);
  }
  {
    InspectorTest.log('Run infinite loop:');
    const {error:{message}} = await Protocol.Runtime.evaluate({
      expression: 'for(;;){}',
      timeout: 0
    });
    InspectorTest.log(message);
  }
  InspectorTest.completeTest();
})();
