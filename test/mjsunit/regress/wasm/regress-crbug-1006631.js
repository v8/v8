// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-turbo-graph --experimental-wasm-type-reflection

new WebAssembly.Function({ parameters: [], results: [] }, x => x);
