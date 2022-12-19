// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-shadow-realm

// Test that CallSite#getFunction and CallSite#getThis throw inside
// ShadowRealms, as otherwise we could violate the callable boundary invariant.

const shadowRealm = new ShadowRealm();

// The ShadowRealm won't have assertThrows, so use try-catch and accumulate a
// message string.
const wrapped = shadowRealm.evaluate(`
Error.prepareStackTrace = function(err, frames) {
  let a = [];
  for (let i = 0; i < frames.length; i++) {
    try {
      a.push(frames[i].getFunction());
    } catch (e) {
      a.push("getFunction threw");
    }
    try {
      a.push(frames[i].getThis());
    } catch (e) {
      a.push("getThis threw");
    }
  }
  return a.join(' ');
};

function inner() {
  try {
    throw new Error();
  } catch (e) {
    return e.stack;
  }
}

inner;
`);

(function outer() {
  // There are 3 frames: top-level, outer, inner, so getFunction/getThis should
  // throw 3 times.
  assertEquals("getFunction threw getThis threw " +
               "getFunction threw getThis threw " +
               "getFunction threw getThis threw", wrapped());
})();
