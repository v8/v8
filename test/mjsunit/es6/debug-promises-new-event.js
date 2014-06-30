// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-debug-as debug

Debug = debug.Debug;

var exception = null;
var new_promise;

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.PromiseEvent) return;
  try {
    assertTrue(event_data.resolver().isFunction());
    assertEquals(resolver, event_data.resolver().value());
    assertTrue(event_data.resolver().resolved());
    assertEquals("resolver", event_data.resolver().name());
    assertTrue(event_data.resolver().source().indexOf("Token") > 0);

    assertTrue(event_data.promise().isPromise());
    new_promise = event_data.promise().value();
    assertEquals("pending", event_data.promise().status());
  } catch (e) {
    print(e + e.stack)
    exception = e;
  }
}

Debug.setListener(listener);

function resolver(resolve, reject) {
  resolve();  // Token
}

var p = new Promise(resolver);
assertEquals(new_promise, p);

assertNull(exception);
Debug.setListener(null);
