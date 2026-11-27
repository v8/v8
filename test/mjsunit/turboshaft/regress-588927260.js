// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-analyze-environment-liveness
// Flags: --trace-turbo-graph

// Trigger a BuildGraphPhase bailout (kTooManyArguments) due to FrameState
// inputs exceeding uint16_t max while --trace-turbo-graph is enabled.
const vars = Array.from({length: 65530}, (_, i) => `v${i}=0`).join(',');
const f = new Function('a', `if (false) { var ${vars}; } return a + 1;`);

%PrepareFunctionForOptimization(f);
%OptimizeFunctionOnNextCall(f);
f();
