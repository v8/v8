// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-promises --expose-debug-as debug

// Test debug events when we listen to all exceptions and
// there is a catch handler for the exception thrown in a Promise.
// Expectation:
//  - the normal Exception debug event is triggered.
//  - the PendingExceptionInPromise debug event is triggered afterwards,
//    with the same exception object.

Debug = debug.Debug;

var log = [];
var step = 0;
var exception = undefined;

var p = new Promise(function(resolve, reject) {
  log.push("resolve");
  resolve();
});

var q = p.chain(
  function() {
    log.push("throw");
    throw new Error("uncaught");
  });

function listener(event, exec_state, event_data, data) {
  try {
    // Ignore exceptions during startup in stress runs.
    if (step > 1) return;
    assertEquals(["resolve", "end main", "throw"], log);
    if (event == Debug.DebugEvent.Exception) {
      assertEquals(0, step);
      exception = event_data.exception();
      assertEquals(undefined, event_data.promise());
    } else if (event == Debug.DebugEvent.PendingExceptionInPromise) {
      assertEquals(1, step);
      assertEquals(exception, event_data.exception());
      assertEquals("uncaught", exception.message);
      assertTrue(event_data.promise() instanceof Promise);
      assertTrue(event_data.uncaught());
    } else {
      return;
    }
    step++;
  } catch (e) {
    // Signal a failure with exit code 1.  This is necessary since the
    // debugger swallows exceptions and we expect the chained function
    // and this listener to be executed after the main script is finished.
    print("Unexpected exception: " + e + "\n" + e.stack);
    quit(1);
  }
}

Debug.setBreakOnException();
Debug.setListener(listener);

log.push("end main");
