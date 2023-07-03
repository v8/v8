// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-to-js-generic-wrapper

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

let sig = makeSig([], []);
const imp_index = builder.addImport('imp', 'foo', sig);
builder.addFunction('main', sig)
    .addBody([
      kExprCallFunction,
      imp_index,
    ])
    .exportFunc();

let called = false;
function foo() {
  called = true;
}

assertTrue(!called);
const instance =
    builder.instantiate({imp: {foo: foo}});
instance.exports.main();
assertTrue(called);
