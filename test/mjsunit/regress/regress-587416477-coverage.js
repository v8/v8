// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-background-compile --stress-flush-code --expose-gc
// Flags: --allow-natives-syntax --verify-code-merge

function flush() {
  for (let i = 0; i < 5; i++) gc({type: "major"});
}

// Scripts compiled after a coverage mode change must not merge into previously
// cached scripts (CompilationCacheScript::Lookup skips partial hits once
// disable_bytecode_flushing is set).
{
  // 1. Direct eval on a lexical block scope whose ScopeInfo is reachable on
  // old_script only via the compiled function's BytecodeArray constant pool.
  let codeBlockEval = `
    (function outer(x) {
      {
        let y = 12;
        try {} catch { eval("x + y"); }
        return x + y;
      }
    })
  `;

  // 2. Nested closure inside an unemitted catch block capturing a lexical
  // block variable whose ScopeInfo is reachable on old_script only via the
  // compiled function's BytecodeArray constant pool.
  let codeBlockClosure = `
    (function outer() {
      {
        let y = 12;
        try {} catch { return () => y; }
        return y;
      }
    })
  `;

  // 3. Unused function declarations are emitted under precise coverage
  // (allocating an extra CreateClosure feedback slot), which would cause an
  // out-of-bounds ClosureFeedbackCellArray read if merged or recompiled onto
  // a flushed pre-coverage SFI with a live closure.
  let codeUnusedFn = `
    (function outer() {
      "use strict";
      function unused() { return 1; }
      return 12;
    })
  `;

  let outerBlockEval = Realm.eval(Realm.current(), codeBlockEval);
  let outerBlockClosure = Realm.eval(Realm.current(), codeBlockClosure);
  let outerUnusedFn = Realm.eval(Realm.current(), codeUnusedFn);

  assertEquals(12, outerUnusedFn());
  flush();

  // Recompile cases 1 and 2 without block coverage so their block ScopeInfos
  // live only in their BytecodeArray constant pools while their top-level SFIs
  // remain flushed.
  assertEquals(42, outerBlockEval(30));
  assertEquals(12, outerBlockClosure());

  %DebugTogglePreciseCoverage(true);
  try {
    Realm.eval(Realm.current(), codeUnusedFn);
  } finally {
    %DebugTogglePreciseCoverage(false);
  }
  assertEquals(12, outerUnusedFn());

  %DebugToggleBlockCoverage(true);
  try {
    Realm.eval(Realm.current(), codeBlockEval);
    Realm.eval(Realm.current(), codeBlockClosure);

    // 4. Forced flushing after a coverage mode toggle must not trigger a
    // BackgroundMergeTask on a partial script cache hit.
    let codeForcedFlush =
        'globalThis.__forcedFlushTop = %GetFunctionForCurrentFrame(); 1;';
    assertEquals(1, Realm.eval(Realm.current(), codeForcedFlush));
    %ForceFlush(globalThis.__forcedFlushTop);
    assertEquals(1, Realm.eval(Realm.current(), codeForcedFlush));
  } finally {
    %DebugToggleBlockCoverage(false);
  }

  assertEquals(42, outerBlockEval(30));
  assertEquals(12, outerBlockClosure());
}
