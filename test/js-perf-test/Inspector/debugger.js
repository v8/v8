// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

new BenchmarkSuite('Debugger.paused', [10000], [
  new Benchmark('Debugger.paused', false, false, 0, DebuggerPaused, DebuggerEnable, DebuggerDisable),
]);

let lastId = 0;
function DebuggerEnable() {
  send(JSON.stringify({id: ++lastId, method: 'Debugger.enable'}));
  // force lazy compilation of inspector related scrtips
  send(JSON.stringify({id: ++lastId, method: 'Runtime.evaluate', params: {expression: ''}}));
}

function DebuggerDisable() {
  send(JSON.stringify({id: ++lastId, method: 'Debugger.disable'}));
}

function DebuggerPaused() {
  for (var i = 0; i < 10; ++i) {
    debugger;
  }
}
