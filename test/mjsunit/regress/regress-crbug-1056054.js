// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function (global) {
  var e = [];
  for (var i = 0; i < 1e5; ++i) {
    e.push('a' + i);
  }
  for (var j = 0; j < 900; ++j) {
    for(var i = 0; i < 1e4; ++i) {
      global[e[i]] = j;
      delete global[e[i]];
    }
  }
})(this);
