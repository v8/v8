// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is loaded before each correctness test case and after v8_mock.js.
// You can temporarily change JS behavior here to silence known problems.
// Please refer to a bug in a comment and remove the suppression once the
// problem is fixed.

// Suppress https://crbug.com/1339320
(function () {
  var oldMathPow = Math.pow
  Math.pow = function(a, b){
    var s = "" + oldMathPow(a, b)
    // Low tech precision mock. Limit digits in string representation.
    // The phrases Infinity and NaN don't match the split("e").
    s = s.split("e");
    s[0] = s[0].substr(0, 15);
    return parseFloat(s.join("e"));
  }
})();
