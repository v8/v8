// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var a = 'a';
for (var i = 0; i < 27; i++) a += a;

function replace() {
  a.replace('a', a);
}

assertThrows(replace, RangeError);
