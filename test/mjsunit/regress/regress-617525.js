// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function __f_14() {
  "use asm";
  function __f_15() { return 0; }
  function __f_15() { return 137; }  // redeclared function
  return {};
}
assertThrows(function() { Wasm.instantiateModuleFromAsm(__f_14.toString()) });
