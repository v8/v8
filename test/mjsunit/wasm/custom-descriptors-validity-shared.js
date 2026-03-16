// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-custom-descriptors --experimental-wasm-shared

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function Build(...recgroups) {
  let builder = new WasmModuleBuilder();
  for (let r of recgroups) {
    builder.startRecGroup();
    r(builder);
    builder.endRecGroup();
  }
  return builder;
}

function CheckValid(...recgroups) {
  let builder = Build(...recgroups);
  builder.instantiate();
}

function CheckInvalid(message, ...recgroups) {
  let builder = Build(...recgroups);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError, message);
}

CheckValid((builder) => {
  builder.addStruct({descriptor: 1, shared: true});
  builder.addStruct({describes: 0, shared: true});
});

CheckInvalid(/Type 0 and its descriptor 1 must have same sharedness/,
             (builder) => {
  builder.addStruct({descriptor: 1, shared: true});
  builder.addStruct({describes: 0});
});

// Shared descriptors of non-shared types could technically be allowed. TBD.
CheckInvalid(/Type 0 and its descriptor 1 must have same sharedness/,
             (builder) => {
  builder.addStruct({descriptor: 1});
  builder.addStruct({describes: 0, shared: true});
});
