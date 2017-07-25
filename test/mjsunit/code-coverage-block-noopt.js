// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt --block-coverage
// Flags: --no-stress-fullcodegen --harmony-async-iteration --no-opt
// Files: test/mjsunit/code-coverage-utils.js

%DebugToggleBlockCoverage(true);

TestCoverage(
"optimized and inlined functions",
`
function g() { if (true) nop(); }         // 0000
function f() { g(); g(); }                // 0050
f(); f(); %OptimizeFunctionOnNextCall(f); // 0100
f(); f(); f(); f(); f(); f();             // 0150
`,
[{"start":0,"end":199,"count":1},
 {"start":0,"end":33,"count":16},
 {"start":50,"end":76,"count":8}]
);

%DebugToggleBlockCoverage(false);
