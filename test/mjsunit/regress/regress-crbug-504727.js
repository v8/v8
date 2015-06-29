// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-test

if (this.Worker) {
  function __f_4() {}
  var __v_2 = new Worker(__f_4);
}
