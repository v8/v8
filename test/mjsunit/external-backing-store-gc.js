// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --mock-arraybuffer-allocator-limit=800000000

for (var i = 0; i < 1024; i++) {
  let garbage = new ArrayBuffer(1024*1024);
}
