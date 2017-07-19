// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// This tests checks some possible wrong exception handling due to,
// for instance, the OSR loop peeling. If exception handlers are not updated
// correctly, when we run the second iteration of the outermost loop, which
// is the OSR optimised version, the try-catch will fail... which should not
// fail on a correct code.

function toto() {
  for (var a = 0; a < 2; a++) {
    try { throw 'The exception should have been caught.'; }
    catch(e) {}
    for (var b = 0; b < 1; b++) {
      %OptimizeOsr();
    }
  }
}

toto();
