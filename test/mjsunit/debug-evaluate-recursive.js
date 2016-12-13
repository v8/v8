// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --expose-debug-as debug
// Get the Debug object exposed from the debug context global object.
Debug = debug.Debug

listenerComplete = false;
exception = false;

// Event listener which evaluates with break disabled.
function listener(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Break)
    {
      // Call functions with break using the FrameMirror directly.
      assertEquals(1, exec_state.evaluateGlobal('f()', true).value());
      assertEquals(2, exec_state.evaluateGlobal('g()', true).value());
      assertEquals(1, exec_state.frame(0).evaluate('f()', true).value());
      assertEquals(2, exec_state.frame(0).evaluate('g()', true).value());
      // Indicate that all was processed.
      listenerComplete = true;
    }
  } catch (e) {
    exception = e
  };
};


// Event listener which evaluates with break enabled one time and the second
// time evaluates with break disabled.
var break_count = 0;
function listener_recurse(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Break)
    {
      break_count++;

      // Call functions with break using the FrameMirror directly.
      if (break_count == 1) {
        // First break event evaluates with break enabled.
        assertEquals(1, exec_state.frame(0).evaluate('f()', false).value());
        listenerComplete = true;
      } else {
        // Second break event evaluates with break disabled.
        assertEquals(2, break_count);
        assertFalse(listenerComplete);
        assertEquals(1, exec_state.frame(0).evaluate('f()', true).value());
      }
    }
  } catch (e) {
    exception = e
  };
};

// Add the debug event listener.
Debug.setListener(listener);

// Test functions - one with break point and one with debugger statement.
function f() {
  return 1;
};

function g() {
  debugger;
  return 2;
};

Debug.setBreakPoint(f, 2, 0);

// Cause a debug break event.
debugger;

assertFalse(exception, "exception in listener")
// Make sure that the debug event listener vas invoked.
assertTrue(listenerComplete);

// Remove the debug event listener.
Debug.setListener(null);

// Set debug event listener wich uses recursive breaks.
Debug.setListener(listener_recurse);
listenerComplete = false;

Debug.setBreakPoint(f, 2, 0);

debugger;

assertFalse(exception, "exception in listener")
// Make sure that the debug event listener vas invoked.
assertTrue(listenerComplete);
assertEquals(2, break_count);
