// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --turboshaft --jit-fuzzing

{
  const zero = 0;
  const x = -zero;
  const result = Math.cbrt(x);
}
