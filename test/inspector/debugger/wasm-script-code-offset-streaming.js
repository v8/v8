// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming

// Because of https://crbug.com/v8/10748 we skip pumping the message loop
// with --stress-incremental-marking, which makes this test fail. Thus disable
// that stress mode here.
// Flags: --no-stress-incremental-marking

utils.load('test/inspector/debugger/wasm-script-code-offset.js');
