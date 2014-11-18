// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-debug-as debug

Debug = debug.Debug
var exception = null;
var break_count = 0;
var expected_breaks = 7;

function listener(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Break) {
      assertTrue(exec_state.frameCount() != 0, "FAIL: Empty stack trace");
      var source = exec_state.frame(0).sourceLineText();
      print("paused at: " + source);
      assertTrue(source.indexOf("// Break " + break_count + ".") > 0,
                 "Unexpected pause at: " + source);
      if (source.indexOf("StepOver.") !== -1) {
        exec_state.prepareStep(Debug.StepAction.StepNext, 1);
      } else {
        exec_state.prepareStep(Debug.StepAction.StepIn, 1);
      }
      ++break_count;
    } else if (event == Debug.DebugEvent.AsyncTaskEvent &&
               event_data.type() === "willHandle" &&
               event_data.name() !== "Object.observe" &&
               break_count > 0) {
      exec_state.prepareStep(Debug.StepAction.StepIn, 1);
    }
  } catch (e) {
    exception = e;
    print(e, e.stack);
  }
};

Debug.setListener(listener);

Promise.resolve(42)
  .then(promise1)
  .then(Object) // Should skip stepping into native.
  .then(Boolean) // Should skip stepping into native.
  .then(promise2)
  .then(undefined, promise3)
  .catch(function(e) {
    %AbortJS("FAIL: uncaught exception " + e);
  });

function promise1()
{
  debugger; // Break 0.
  return exception || 1; // Break 1.
} // Break 2.

function promise2()
{
  throw new Error; // Break 3.
}

function promise3()
{
  finalize(); // Break 4. StepOver.
  return break_count; // Break 5.
} // Break 6.

function finalize()
{
  var dummy = {};
  Object.observe(dummy, function() {
    if (expected_breaks !== break_count) {
      %AbortJS("FAIL: expected <" + expected_breaks + "> breaks instead of <" +
               break_count + ">");
    }
    if (exception !== null) {
      %AbortJS("FAIL: exception: " + exception);
    }
  });
  dummy.foo = 1;
}
