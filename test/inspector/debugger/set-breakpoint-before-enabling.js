// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.sendCommand("Debugger.setBreakpointByUrl", { url: "http://example.com", lineNumber: 10  }, didSetBreakpointByUrlBeforeEnable);

function didSetBreakpointByUrlBeforeEnable(message)
{
  InspectorTest.log("setBreakpointByUrl error: " + JSON.stringify(message.error, null, 2));
  InspectorTest.sendCommand("Debugger.setBreakpoint", {}, didSetBreakpointBeforeEnable);
}

function didSetBreakpointBeforeEnable(message)
{
  InspectorTest.log("setBreakpoint error: " + JSON.stringify(message.error, null, 2));
  InspectorTest.completeTest();
}
