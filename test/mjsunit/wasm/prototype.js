// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function GlobalPrototype() {
  class _Global extends WebAssembly.Global {}
  let global = new _Global({value: 'i32', mutable: false}, 0);
  assertInstanceof(global, _Global);
  assertInstanceof(global, WebAssembly.Global);
})();
