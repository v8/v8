// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stack-size=100

var result = null;
var type = true;
var re = /\w/;
re.test("a");  // Trigger regexp compile.

function rec() {
  try {
    return rec();
  } catch (e) {
    if (!(e instanceof RangeError)) type = false;
    return re.test("b");
  }
}

assertTrue(rec());
assertTrue(type);
