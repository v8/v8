// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-source-phase-imports --jitless

assertPromiseResult(
    import.source('../incrementer.wasm'), assertUnreachable, e => {
      assertEquals('CompileError', e.name);
      assertMatches(/jitless/, e.message);
    });
