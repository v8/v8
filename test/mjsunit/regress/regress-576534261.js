// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector --allow-natives-syntax

globalThis.receive = function(message) {};

let M = 60000;
let N = 20000;
globalThis.s = 'x'.repeat(N);
globalThis.arr = Array.from({length: M}, () => new RegExp(s));

send(JSON.stringify({
  id: 1,
  method: 'Runtime.evaluate',
  params: {
    expression: 'arr',
    generatePreview: false,
    serializationOptions: {
      serialization: 'deep'
    }
  }
}));
