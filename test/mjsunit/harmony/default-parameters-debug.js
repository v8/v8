// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-debug-as debug --harmony-default-parameters

// Get the Debug object exposed from the debug context global object.
Debug = debug.Debug

listenerComplete = false;
breakPointCount = 0;

function listener(event, exec_state, event_data, data) {
  if (event == Debug.DebugEvent.Break) {
    breakPointCount++;
    if (breakPointCount == 1) {
      // Break point in initializer for parameter `a`, invoked by
      // initializer for parameter `b`
      assertEquals('default', exec_state.frame(1).evaluate('mode').value());

      // initializer for `b` can't refer to `b`
      assertThrows(function() {
        return exec_state.frame(1).evaluate('b').value();
      }, ReferenceError);

      assertThrows(function() {
        return exec_state.frame(1).evaluate('c');
      }, ReferenceError);
    } else if (breakPointCount == 2) {
      // Break point in IIFE initializer for parameter `c`
      assertEquals('modeFn', exec_state.frame(1).evaluate('a.name').value());
      assertEquals('default', exec_state.frame(1).evaluate('b').value());
      assertThrows(function() {
        return exec_state.frame(1).evaluate('c');
      }, ReferenceError);
    } else if (breakPointCount == 3) {
      // Break point in function body --- `c` parameter is shadowed
      assertEquals('modeFn', exec_state.frame(0).evaluate('a.name').value());
      assertEquals('default', exec_state.frame(0).evaluate('b').value());
      // TODO(caitp): fix scoping so that parameter `c` can be shadowed by vars
      //assertEquals(true, exec_state.frame(0).evaluate('c').value());
    }
  }
};

// Add the debug event listener.
Debug.setListener(listener);

function f(a = function modeFn(mode) {
                 debugger;
                 return mode;
               },
           b = a("default"),
           c = (function() {
             debugger;
           })()) {
  // TODO(caitp): fix scoping so that parameter `c` can be shadowed by vars
  //var c = true;
  debugger;
};

f();

// Make sure that the debug event listener vas invoked.
assertEquals(3, breakPointCount);
