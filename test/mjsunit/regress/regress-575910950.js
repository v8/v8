// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector --max-old-space-size=4

globalThis.receive = function(message) {
  print('receive: ' + message);
};

globalThis.o = {};
for (let i = 0; i < 10000; i++) {
  o['p' + i] = undefined;
}

send(JSON.stringify({
  id: 1,
  method: 'Runtime.evaluate',
  params: {expression: 'Array(5000).fill(o)', returnByValue: true}
}));
