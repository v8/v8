// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

function CheckValid(type) {
  // print("V:", type);
  assertDoesNotThrow("'use types'; var x: " + type + ";");
}

function CheckInvalid(type) {
  // print("I:", type);
  assertThrows("'use types'; var x: " + type + ";", SyntaxError);
}

(function TestStringLiteralTypes() {
  // These are not really valid here.
  // They should only be valid in function/constructor signatures.
  CheckValid("(cmd: 'add', x: number, y: number) => number");
  CheckValid('(cmd: "sum", a: number[]) => number');
  CheckValid("(x: number, cmd: 'one', ...rest) => any");
  CheckValid("(x: string, y: number, cmd: 'two', ...rest) => any");
  CheckValid("(x: number, cmd?: 'two', ...rest) => string");
  // String literal types where they shouldn't be.
  CheckInvalid("'foo'");
  CheckInvalid("('foo')");
  CheckInvalid("'foo'[]");
  CheckInvalid("('foo')[]");
  CheckInvalid("('foo'[])");
  CheckInvalid("(('foo')[])");
  CheckInvalid("'foo' | 'bar'");
  CheckInvalid("('foo') => any");
})();
