// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


(function TestStringLiteralTypes() {
  // These are not really valid here.
  // They should only be valid in function/constructor signatures.
  CheckValidType("(cmd: 'add', x: number, y: number) => number");
  CheckValidType('(cmd: "sum", a: number[]) => number');
  CheckValidType("(x: number, cmd: 'one', ...rest) => any");
  CheckValidType("(x: string, y: number, cmd: 'two', ...rest) => any");
  CheckValidType("(x: number, cmd?: 'two', ...rest) => string");
  // String literal types where they shouldn't be.
  CheckInvalidType("'foo'");
  CheckInvalidType("('foo')");
  CheckInvalidType("'foo'[]");
  CheckInvalidType("('foo')[]");
  CheckInvalidType("('foo'[])");
  CheckInvalidType("(('foo')[])");
  CheckInvalidType("'foo' | 'bar'");
  CheckInvalidType("('foo') => any");
})();
