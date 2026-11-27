// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function getCharCode() {
  return 0x1234;
}
%NeverOptimizeFunction(getCharCode);

let sink = 0;

// Keep enough int32 values live across String.fromCharCode to exhaust all
// allocatable GPRs on x64. getCharCode() leaves char_code in rax (the return
// register), and returning the String.fromCharCode result gives it an rax hint,
// exercising EnsureFreeRegisterAtEnd when the hint matches a clobbered input.
function fromCharCodeWithHintOnClobberedInput(
    flag, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) {
  let char_code = getCharCode() | 0;
  let v0 = (a0 + 1) | 0;
  let v1 = (a1 + 2) | 0;
  let v2 = (a2 + 3) | 0;
  let v3 = (a3 + 4) | 0;
  let v4 = (a4 + 5) | 0;
  let v5 = (a5 + 6) | 0;
  let v6 = (a6 + 7) | 0;
  let v7 = (a7 + 8) | 0;
  let v8 = (a8 + 9) | 0;
  let v9 = (a9 + 10) | 0;
  let v10 = (a10 + 11) | 0;
  let v11 = (a11 + 12) | 0;
  let v12 = (a12 + 13) | 0;
  let v13 = (a13 + 14) | 0;
  let result = String.fromCharCode(char_code);
  if (flag) {
    sink = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 +
           v13;
  }
  return result;
}

%PrepareFunctionForOptimization(fromCharCodeWithHintOnClobberedInput);
assertEquals(
    '\u1234',
    fromCharCodeWithHintOnClobberedInput(
        true, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14));
assertEquals(
    '\u1234',
    fromCharCodeWithHintOnClobberedInput(
        false, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14));
%OptimizeFunctionOnNextCall(fromCharCodeWithHintOnClobberedInput);
assertEquals(
    '\u1234',
    fromCharCodeWithHintOnClobberedInput(
        false, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14));
