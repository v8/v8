// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function get_thin_string(a, b) {
  var str = a + b;  // Make a ConsString.
  var o = {};
  o[str];  // Turn it into a ThinString.
  return str;
}

var str = get_thin_string("foo", "bar");

var re = /.o+ba./;
assertEquals(["foobar"], re.exec(str));
assertEquals(["foobar"], re.exec(str));
assertEquals(["foobar"], re.exec(str));

function CheckCS() {
  assertEquals("o", str.substring(1, 2));
  assertEquals("f".charCodeAt(0), str.charCodeAt(0));
  assertEquals("f", str.split(/oo/)[0]);
}
CheckCS();
%OptimizeFunctionOnNextCall(CheckCS);
CheckCS();

function CheckTF() {
  try {} catch(e) {}  // Turbofan.
  assertEquals("o", str.substring(1, 2));
  assertEquals("f".charCodeAt(0), str.charCodeAt(0));
  assertEquals("f", str.split(/oo/)[0]);
}
CheckTF();
%OptimizeFunctionOnNextCall(CheckTF);
CheckTF();
