// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --validate-asm --no-lazy-feedback-allocation
// Flags: --invocation-count-for-turbofan=1

function Module() {
  "use asm";
  return {};
}
var m = Module();
m = Module();
