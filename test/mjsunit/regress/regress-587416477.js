// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-background-compile --stress-flush-code --expose-gc
// Flags: --verify-code-merge

function flush() {
  for (let i = 0; i < 5; i++) gc({type: "major"});
}

// Partial eager compilation aborted by stack overflow leaves an uncompiled
// SFI with its ScopeInfo attached in old_script->infos(), while its standalone
// eval ScopeInfo slot remains empty until BackgroundMergeTask runs.
{
  let chain = "y" + " + 1 - 1".repeat(250);
  let code = `
    (function outer() {
      return (function inner() {
        let y = 1;
        return eval("y") + ${chain};
      });
    })
  `;

  let outer = Realm.eval(Realm.current(), code);
  flush();

  let inner;
  function recurse(depth) {
    try {
      recurse(depth + 1);
    } catch (e) {
      if (!inner) {
        if ((depth & 31) !== 0) throw e;
        try {
          inner = outer();
        } catch (e2) {
          throw e;
        }
      }
    }
  }
  try { recurse(0); } catch (e) {}

  Realm.eval(Realm.current(), code);
  assertEquals(2, inner());
}
