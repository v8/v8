// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignition --turbo

Debug = debug.Debug

var exception = null;
var date = new Date();

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Break) return;
  try {
    function success(expectation, source) {
      var result = exec_state.frame(0).evaluate(source, true).value();
      if (expectation !== undefined) assertEquals(expectation, result);
    }
    function fail(source) {
      assertThrows(() => exec_state.frame(0).evaluate(source, true),
                   EvalError);
    }

    // Test Date.prototype functions.
    success(undefined, `Date()`);
    success(undefined, `new Date()`);
    success(undefined, `Date.now()`);
    success(undefined, `Date.parse(1)`);
    for (f of Object.getOwnPropertyNames(Date.prototype)) {
      if (typeof Date.prototype[f] === "function") {
        if (f.startsWith("set") || f.startsWith("toLocale")) {
          fail(`date.${f}(5);`, true);
        } else {
          success(undefined, `date.${f}();`, true);
        }
      }
    }

    // Test Boolean.
    success(true, `Boolean(1)`);
    success(new Boolean(true), `new Boolean(1)`);
    success("true", `true.toString()`);
    success(true, `true.valueOf()`);

    // Test global functions.
    success(1, `parseInt("1")`);
    success(1.3, `parseFloat("1.3")`);
    success("abc", `decodeURI("abc")`);
    success("abc", `encodeURI("abc")`);
    success("abc", `decodeURIComponent("abc")`);
    success("abc", `encodeURIComponent("abc")`);
    success("abc", `escape("abc")`);
    success("abc", `unescape("abc")`);
  } catch (e) {
    exception = e;
    print(e, e.stack);
  };
};

// Add the debug event listener.
Debug.setListener(listener);

function f() {
  debugger;
};

f();

assertNull(exception);
