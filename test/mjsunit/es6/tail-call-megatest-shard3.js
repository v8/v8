// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-tailcalls
// TODO(v8:4698), TODO(ishell): support these cases.
// Flags: --turbo --nostress-opt

try {
  load("mjsunit/es6/tail-call-megatest.js");
} catch(e) {
  load("test/mjsunit/es6/tail-call-megatest.js");
}

run_tests(3);
