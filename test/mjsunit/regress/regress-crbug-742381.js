// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f(v) {
  v.x = 0;
  v[1] = 0.1;
  v.x = {};
}
f({});
f(new Array(1));
f(new Array(1));
