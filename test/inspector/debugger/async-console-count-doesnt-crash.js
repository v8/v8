// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("setTimeout(console.count, 0) doesn't crash with enabled async stacks.")

InspectorTest.sendCommand("Debugger.enable", {});
InspectorTest.sendCommand("Debugger.setAsyncCallStackDepth", { maxDepth: 1 });
InspectorTest.sendCommand("Runtime.evaluate", { expression: "setTimeout(console.count, 0)" });
InspectorTest.completeTestAfterPendingTimeouts();
