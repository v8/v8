// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

if (this.Worker) {
  function __f_3() {
    onmessage = function() {}
  }
  var __v_7 = new Worker(__f_3);
  __v_7.postMessage("");
}
