// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-lazy-source-positions
try {
  (function () {
    ((d, e = d) => {
      throw new Error();
    })();
  })();
} catch (ex) {
  print(ex.stack);
}

try {
  (function () {
    ((d, e = f, f = d) => {
      // Won't get here as the initializers will cause a ReferenceError
    })();
  })();
} catch (ex) {
  print(ex.stack);
}
