// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-default-parameters --min-preparse-length=0

var i = 0;
function f(handler = function(b) { return b + "#" + (++i); }, b = "red") {
  return handler(b);
}

assertEquals([
  "blue#1",
  "red#2",
  "red",
  "yellow#3"
], [
  f(undefined, "blue"),
  f(),
  f(function(b) { return b; }),
  f(undefined, "yellow")
]);
