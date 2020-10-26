// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {

  const size = 100000;
  const arr = Array(size).fill(0.5);
  for (let i = 0; i < 10; i++) {
    arr.join(",");
  }

})();
