// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --jit-fuzzing

for (let i = 0; i < 50; i++) {
  try { typeof x } catch (e) {};
  let x;
}
