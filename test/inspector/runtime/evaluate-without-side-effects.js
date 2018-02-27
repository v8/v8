// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start("Tests that Runtime.evaluate can run without side effects.");

Protocol.Runtime.enable();
Protocol.Debugger.enable();
(async function() {
  InspectorTest.log("Test throwOnSideEffect: false");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "var x = 2; x;",
    throwOnSideEffect: false
  }));

  InspectorTest.log("Test expression with side-effect, with throwOnSideEffect: true");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "x = 3; x;",
    throwOnSideEffect: true
  }));

  InspectorTest.log("Test expression without side-effect, with throwOnSideEffect: true");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "x * 2",
    throwOnSideEffect: true
  }));

  InspectorTest.completeTest();
})();
