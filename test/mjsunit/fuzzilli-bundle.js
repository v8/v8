// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzilli-bundle

/// FUZZILLI_BUNDLE
/// FUZZILLI_MODULE:a.js
export {a}
let a = 42;
/// FUZZILLI_MODULE:b.js
export * from "a.js"
/// FUZZILLI_MODULE:main.js
import {a} from "b.js"
assertEquals(a, 42)
