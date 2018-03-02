// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-numeric-separator

{
  const basic = 1_0_0_0;
  assertEquals(basic, 1000);
}
{
  const exponent = 1_0e+1;
  assertEquals(exponent, 10e+1);
}
{
  const exponent2 = 1_0e+1_0;
  assertEquals(exponent2, 10e+10);
}
{
  const hex = 0xF_F_FF;
  assertEquals(hex, 0xFFFF);
}
{
  const octal = 0o7_7_7;
  assertEquals(octal, 0o777);
}
{
  const implicitOctal = 07_7_7;
  assertEquals(implicitOctal, 0o777);
}

{
  let exception = false;
  try {
    const code = `"use strict" const implicitOctal = 07_7_7`;
    eval(code);
  } catch(e) {
    exception = true;
    assertInstanceof(e, SyntaxError);
  }
  assertTrue(exception);
}

{
  const binary = 0b0_1_0_1_0;
  assertEquals(binary, 0b01010);
}
{
  const leadingZeros = 09_1_3;
  assertEquals(leadingZeros, 0913);
}

assertThrows('1_0_0_0_', SyntaxError);
assertThrows('1e_1', SyntaxError);
assertThrows('1e+_1', SyntaxError);
assertThrows('1_e+1', SyntaxError);
assertThrows('1__0', SyntaxError);
assertThrows('0x_1', SyntaxError);
assertThrows('0x1__1', SyntaxError);
assertThrows('0x1_', SyntaxError);
assertThrows('0b_0101', SyntaxError);
assertThrows('0b11_', SyntaxError);
assertThrows('0b1__1', SyntaxError);
assertThrows('0o777_', SyntaxError);
assertThrows('0o_777', SyntaxError);
assertThrows('0o7__77', SyntaxError);
assertThrows('0777_', SyntaxError);
assertThrows('07__77', SyntaxError);
