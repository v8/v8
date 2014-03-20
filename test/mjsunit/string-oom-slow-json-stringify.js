// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var a = 'a';
for (var i = 0; i < 20; i++) a += a;
for (var i = 0; i < 8; i++) a = [a, a];

function stringify() {
  JSON.stringify(a);
}

assertThrows(stringify, RangeError);
