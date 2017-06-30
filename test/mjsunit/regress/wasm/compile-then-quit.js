// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --allow-natives-syntax --wasm-async-compilation

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 42]).exportAs('f');
let buffer = builder.toBuffer();

// This test is meaningless if quit does not exist, e.g. with "d8 --omit-quit".
assertPromiseResult(
    WebAssembly.compile(buffer), typeof quit !== 'undefined' ? quit : _ => {
      print('No quit() available');
    }, assertUnreachable);
