// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

__v_1 = "var outer = 0; function test() {'use strict'; outer = 1; } test();";
assertThrows(function() {
  Wasm.instantiateModuleFromAsm(__v_1);
});
