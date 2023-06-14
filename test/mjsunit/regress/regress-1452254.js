// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --verify-heap

let o = { toJSON() {} };
function ID(x) { return x; }

class C0 {
  toJSON() {}
  [ID('x')](){}
};

class C1 {
  static toJSON() {}
  static [ID('x')](){}
};
