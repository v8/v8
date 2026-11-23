// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-background-compile

// Make JSReceiver::GetConstructor() of a RangeError no longer report
// RangeError. This must not affect whether a main-thread stack overflow during
// stress background compilation is recognized as such.
Object.defineProperty(RangeError, 'name', {value: ''});
delete RangeError.prototype.constructor;

// Deeply nested source so that the parser needs a lot of stack.
const source = '['.repeat(1000) + '0' + ']'.repeat(1000);
let compiles_left = 200;

function recurse() {
  try {
    recurse();
  } catch (e) {
  }
  // Compile close to the stack limit, so that the main-thread compile
  // overflows the stack while the background compile does not.
  if (compiles_left > 0) {
    compiles_left--;
    try {
      Realm.eval(Realm.current(), source + ';' + compiles_left);
    } catch (e) {
    }
  }
}
recurse();
