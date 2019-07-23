// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Resources: test/mjsunit/d8/d8-worker-script.txt

if (this.Worker) {
  // Verify that the Worker constructor by default treats its first argument
  // as the filename of a script load and run.
  var w = new Worker('test/mjsunit/d8/d8-worker-script.txt');
  assertEquals("Starting worker", w.getMessage());
  w.postMessage("");
  assertEquals("DONE", w.getMessage());
  w.terminate();

  try {
    var w = new Worker('test/mjsunit/d8/not-found.txt');
    assertFalse(true);
  } catch (e) {
    // should not be able to find this script.
  }
}
