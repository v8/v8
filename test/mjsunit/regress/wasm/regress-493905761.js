// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let memories = [];
for (let i = 0; i < 200; i++) {
  try {
    memories.push(new WebAssembly.Memory({initial: 1, shared: true, maximum: 65536}));
  } catch(e) { break; }
}
let targetMemory = memories[0];
let workerCode = `
  onmessage = function(event) {
    let data = event.data || event;
    let targetMemory = data.targetMemory;
    while(true) {
      try {
        let b = targetMemory.buffer;
        targetMemory.grow(0);
        targetMemory.grow(1);
      } catch(e) {}
    }
  }
`;
for (let i = 0; i < 6; i++) {
  new Worker(workerCode, {type: 'string'}).postMessage({targetMemory});
}
