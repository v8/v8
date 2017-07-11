// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('crbug.com/736302');

(async function main() {
  Protocol.Runtime.enable();
  Protocol.Runtime.onConsoleAPICalled(InspectorTest.logMessage);
  let r = await Protocol.Runtime.evaluate({expression: `
  console.count({
    get [Symbol.toStringTag]() {
      throw new Error();
    }
  });`});
  InspectorTest.logMessage(r.result.result);
  InspectorTest.completeTest();
})();
