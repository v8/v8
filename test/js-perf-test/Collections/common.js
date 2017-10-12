// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var N = 10;
var LargeN = 1e4;
var keys;


function SetupSmiKeys(count = 2 * N) {
  keys = Array.from({ length : count }, (v, i) => i);
}


function SetupStringKeys(count = 2 * N) {
  keys = Array.from({ length : count }, (v, i) => 's' + i);
}


function SetupObjectKeys(count = 2 * N) {
  keys = Array.from({ length : count }, (v, i) => ({}));
}
