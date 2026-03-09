// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-shared --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function SharedStringConstants() {
  let builder = new WasmModuleBuilder();

  let kSharedRefExtern = wasmRefType(kWasmExternRef).shared();

  let foo = builder.addImportedGlobal(
      "'", "foo", wasmRefType(kWasmExternRef), false);
  let shared_foo = builder.addImportedGlobal(
      "'", "shared_foo", kSharedRefExtern, false);
  let one_char_string = builder.addImportedGlobal(
      "'", "o", kSharedRefExtern, false);

  builder.addFunction("get_foo", kSig_r_v)
    .addBody([kExprGlobalGet, foo])
    .exportFunc();
  builder.addFunction("get_shared_foo", makeSig([], [kSharedRefExtern]))
    .addBody([kExprGlobalGet, shared_foo])
    .exportFunc();
  builder.addFunction("get_one_char_string", makeSig([], [kSharedRefExtern]))
    .addBody([kExprGlobalGet, one_char_string])
    .exportFunc();

  let instance = builder.instantiate({}, {importedStringConstants: "'"});

  assertEquals("foo", instance.exports.get_foo());
  assertEquals("shared_foo", instance.exports.get_shared_foo());
  assertEquals("o", instance.exports.get_one_char_string());

  assertFalse(%IsSharedString(instance.exports.get_foo()));
  assertFalse(%IsInWritableSharedSpace(instance.exports.get_foo()));
  assertTrue(%IsSharedString(instance.exports.get_shared_foo()));
  assertTrue(%IsInWritableSharedSpace(instance.exports.get_shared_foo()));
  assertTrue(%IsSharedString(instance.exports.get_one_char_string()));
  assertTrue(%IsInWritableSharedSpace(instance.exports.get_one_char_string()));
})();
