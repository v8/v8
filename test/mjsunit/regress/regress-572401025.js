// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector --expose-gc --stress-flush-code

let scriptId;
globalThis.receive = function(message) {
  const msg = JSON.parse(message);
  if (msg.method === 'Debugger.scriptParsed' && msg.params.url === 'target.js') {
    scriptId = msg.params.scriptId;
  }
};

const call_outer = Realm.eval(Realm.current(), `1 + 1;
(function outer() {
  "use strict";
  let f_ref;
  label: {
    f_ref = f;
    break label;
    let x = 1;
    function f() {
      return x;
    }
  }
  return f_ref;
})
//# sourceURL=target.js`);

for (let i = 0; i < 4; i++) gc();

send(JSON.stringify({id: 1, method: 'Debugger.enable'}));
send(JSON.stringify({
  id: 2,
  method: 'Debugger.getPossibleBreakpoints',
  params: {
    start: {scriptId: scriptId, lineNumber: 0, columnNumber: 0},
    end: {scriptId: scriptId, lineNumber: 0, columnNumber: 5},
  },
}));

assertThrows(() => call_outer()() + 1, ReferenceError);
