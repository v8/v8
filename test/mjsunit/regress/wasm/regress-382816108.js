// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-max-module-size=1 --fuzzing
// Flags: --wasm-allow-mixed-eh-for-testing
// Flags: --experimental-fuzzing

%WasmGenerateRandomModule();
