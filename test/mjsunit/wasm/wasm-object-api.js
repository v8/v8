// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

assertFalse(undefined === WASM);
assertFalse(undefined == WASM);
assertEquals("function", typeof WASM.verifyModule);
assertEquals("function", typeof WASM.verifyFunction);
assertEquals("function", typeof WASM.compileRun);
