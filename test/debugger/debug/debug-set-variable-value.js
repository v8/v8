// Copyright 2012 the V8 project authors. All rights reserved.
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



var Debug = debug.Debug;

// Accepts a function/closure 'fun' that must have a debugger statement inside.
// A variable 'variable_name' must be initialized before debugger statement
// and returned after the statement. The test will alter variable value when
// on debugger statement and check that returned value reflects the change.
function RunPauseTest(scope_number, expected_old_result, variable_name,
    new_value, expected_new_result, fun) {
  var actual_old_result = fun();
  assertEquals(expected_old_result, actual_old_result);

  var listener_delegate;
  var listener_called = false;
  var exception = null;

  function listener_delegate(exec_state) {
    var scope = exec_state.frame(0).scope(scope_number);
    scope.setVariableValue(variable_name, new_value);
  }

  function listener(event, exec_state, event_data, data) {
    try {
      if (event == Debug.DebugEvent.Break) {
        listener_called = true;
        listener_delegate(exec_state);
      }
    } catch (e) {
      exception = e;
    }
  }

  // Add the debug event listener.
  Debug.setListener(listener);

  var actual_new_result;
  try {
    actual_new_result = fun();
  } finally {
    Debug.setListener(null);
  }

  if (exception != null) {
   assertUnreachable("Exception in listener\n" + exception.stack);
  }
  assertTrue(listener_called);

  assertEquals(expected_new_result, actual_new_result);
}


function ClosureTestCase(scope_index, old_result, variable_name, new_value,
    new_result, success_expected, factory) {
  this.scope_index_ = scope_index;
  this.old_result_ = old_result;
  this.variable_name_ = variable_name;
  this.new_value_ = new_value;
  this.new_result_ = new_result;
  this.success_expected_ = success_expected;
  this.factory_ = factory;
}

ClosureTestCase.prototype.run_pause_test = function() {
  var th = this;
  var fun = this.factory_(true);
  this.run_and_catch_(function() {
    RunPauseTest(th.scope_index_ + 1, th.old_result_, th.variable_name_,
        th.new_value_, th.new_result_, fun);
  });
}

ClosureTestCase.prototype.run_and_catch_ = function(runnable) {
  if (this.success_expected_) {
    runnable();
  } else {
    assertThrows(runnable);
  }
}


// Test scopes visible from closures.

var closure_test_cases = [
  new ClosureTestCase(0, 'cat', 'v1', 5, 5, true,
      function Factory(debug_stop) {
    var v1 = 'cat';
    return function() {
      if (debug_stop) debugger;
      return v1;
    }
  }),

  new ClosureTestCase(0, 4, 't', 7, 9, true, function Factory(debug_stop) {
    var t = 2;
    var r = eval("t");
    return function() {
      if (debug_stop) debugger;
      return r + t;
    }
  }),

  new ClosureTestCase(0, 6, 't', 10, 13, true, function Factory(debug_stop) {
    var t = 2;
    var r = eval("t = 3");
    return function() {
      if (debug_stop) debugger;
      return r + t;
    }
  }),

  new ClosureTestCase(0, 17, 's', 'Bird', 'Bird', true,
      function Factory(debug_stop) {
    eval("var s = 17");
    return function() {
      if (debug_stop) debugger;
      return s;
    }
  }),

  new ClosureTestCase(2, 'capybara', 'foo', 77, 77, true,
      function Factory(debug_stop) {
    var foo = "capybara";
    return (function() {
      var bar = "fish";
      try {
        throw {name: "test exception"};
      } catch (e) {
        return function() {
          if (debug_stop) debugger;
          bar = "beast";
          return foo;
        }
      }
    })();
  }),

  new ClosureTestCase(0, 'AlphaBeta', 'eee', 5, '5Beta', true,
      function Factory(debug_stop) {
    var foo = "Beta";
    return (function() {
      var bar = "fish";
      try {
        throw "Alpha";
      } catch (eee) {
        return function() {
          if (debug_stop) debugger;
          return eee + foo;
        }
      }
    })();
  })
];

for (var i = 0; i < closure_test_cases.length; i++) {
  closure_test_cases[i].run_pause_test();
}


// Test local scope.

RunPauseTest(0, 'HelloYou', 'u', 'We', 'HelloWe', (function Factory() {
  return function() {
    var u = "You";
    var v = "Hello";
    debugger;
    return v + u;
  }
})());

RunPauseTest(0, 'Helloworld', 'p', 'GoodBye', 'HelloGoodBye',
    (function Factory() {
  function H(p) {
    var v = "Hello";
    debugger;
    return v + p;
  }
  return function() {
    return H("world");
  }
})());

RunPauseTest(0, 'mouse', 'v1', 'dog', 'dog', (function Factory() {
  return function() {
    var v1 = 'cat';
    eval("v1 = 'mouse'");
    debugger;
    return v1;
  }
})());

RunPauseTest(0, 'mouse', 'v1', 'dog', 'dog', (function Factory() {
  return function() {
    eval("var v1 = 'mouse'");
    debugger;
    return v1;
  }
})());


// Check that we correctly update local variable that
// is referenced from an inner closure.
RunPauseTest(0, 'Blue', 'v', 'Green', 'Green', (function Factory() {
  return function() {
    function A() {
      var v = "Blue";
      function Inner() {
        return void v;
      }
      debugger;
      return v;
    }
    return A();
  }
})());

// Check that we correctly update parameter, that is known to be stored
// both on stack and in heap.
RunPauseTest(0, 5, 'p', 2012, 2012, (function Factory() {
  return function() {
    function A(p) {
      function Inner() {
        return void p;
      }
      debugger;
      return p;
    }
    return A(5);
  }
})());


// Test script-scope variable.
let abc = 12;
{
  let exception;
  function listener(event, exec_state) {
    try {
      if (event == Debug.DebugEvent.Break) {
        let scope_count = exec_state.frame().scopeCount();
        let script_scope = exec_state.frame().scope(scope_count - 2);
        assertEquals(debug.ScopeType.Script, script_scope.scopeType());
        script_scope.setVariableValue('abc', 42);
      }
    } catch(e) { exception = e }
  }

  Debug.setListener(listener);
  assertEquals(12, abc);
  debugger;
  assertEquals(42, abc);

  if (exception != null) {
    assertUnreachable("Exception in listener\n" + exception.stack);
  }
}

Debug.setListener(null);
