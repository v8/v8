// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-async-await --allow-natives-syntax --max-old-space-size 3

let out;

async function longLoop() {
  for (let i = 0; i < 8000; i++) await undefined;
  out = 1;
}

longLoop();

%RunMicrotasks();

assertEquals(1, out);
