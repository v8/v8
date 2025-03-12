// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The script below will not survive a GC, which may occur in case of
// --stress-incremental-marking or any other stress mode.

// Flags: --no-stress-incremental-marking

let {session, contextGroup, Protocol} = InspectorTest.start('Tests breakpoint at last line.');

let source = `
  let a = 1;
  //# sourceURL=foo.js
  let b = 2;
`;

(async function test() {
  Protocol.Debugger.enable();
  Protocol.Runtime.evaluate({expression: source});
  let {result} = await Protocol.Debugger.setBreakpointByUrl({
    url: 'foo.js',
    lineNumber: 3,
    columnNumber: 12
  });
  InspectorTest.logMessage(result);
  ({result} = await Protocol.Debugger.setBreakpointByUrl({
    url: 'foo.js',
    lineNumber: 4
  }));
  InspectorTest.logMessage(result);
  InspectorTest.completeTest();
})();
