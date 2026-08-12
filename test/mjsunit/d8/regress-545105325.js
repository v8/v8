// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector --allow-natives-syntax

Object.setPrototypeOf(this, new Proxy({}, {}));

send(JSON.stringify({ id: 1, method: 'Debugger.enable' }));

class C {
  constructor() {
    try {
      new C();
    } catch (e) {}
    %WasmStruct();
  }
}
new C();
