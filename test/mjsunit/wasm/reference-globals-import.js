// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --experimental-wasm-stringref
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test type checks when creating a global with a value imported from a global
// from another module.
(function TestReferenceGlobalsImportGlobal() {
  print(arguments.callee.name);

  let exporting_instance = (function() {
    let builder = new WasmModuleBuilder();
    builder.setSingletonRecGroups();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
        builder.addStruct([makeField(kWasmI32, false)], type_super);
    let type_other = builder.addStruct([makeField(kWasmI64, false)]);

    builder.addGlobal(wasmRefType(type_super), false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_super])
           .exportAs("super");
    builder.addGlobal(wasmRefType(type_sub), false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_sub])
           .exportAs("sub");
    builder.addGlobal(wasmRefType(type_other), false,
            [kExprI64Const, 42,
             kGCPrefix, kExprStructNew, type_other])
           .exportAs("other");
    // null variants
    builder.addGlobal(wasmRefNullType(type_super), false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_super])
           .exportAs("super_nullable");
    builder.addGlobal(wasmRefNullType(type_sub), false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_sub])
           .exportAs("sub_nullable");
    builder.addGlobal(wasmRefNullType(type_other), false,
            [kExprI64Const, 42,
             kGCPrefix, kExprStructNew, type_other])
           .exportAs("other_nullable");
    return builder.instantiate({});
  })();

  let tests = [
    //valid |type             |imported_global
    [true,  "super",          "super"],
    [true,  "sub",            "sub"],
    [true,  "super",          "sub"], // would be invalid for immutable global!
    [false, "sub",            "super"],
    [false, "sub",            "other"],
    [false, "super",          "super_nullable"],
    [true,  "super_nullable", "super"],
    [true,  "super_nullable", "sub"],
    [true,  "super_nullable", "sub_nullable"],
    [false, "super_nullable", "other_nullable"],
    [false, "sub_nullable",   "super_nullable"],
  ];
  for (let[expected_valid, type, global] of tests) {
    print(`test ${type} imports ${global}`);
    let builder = new WasmModuleBuilder();
    builder.setSingletonRecGroups();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
      builder.addStruct([makeField(kWasmI32, false)], type_super);

    let types = {
      super: wasmRefType(type_super),
      sub: wasmRefType(type_sub),
      super_nullable: wasmRefNullType(type_super),
      sub_nullable: wasmRefNullType(type_sub),
    };
    builder.addImportedGlobal("imports", "global", types[type], false);
    assertNotEquals(exporting_instance.exports[global], undefined);
    let imports = { global: exporting_instance.exports[global] };
    if (expected_valid) {
      builder.addFunction("read_global", makeSig([], [kWasmI32]))
      .addBody([
        kExprGlobalGet, 0,
        kGCPrefix, kExprStructGet, types[type].heap_type, 0,
      ])
      .exportFunc();

      let instance = builder.instantiate({imports});
      assertEquals(42, instance.exports.read_global());
    } else {
      assertThrows(
        () => builder.instantiate({imports}),
        WebAssembly.LinkError,
        /imported global does not match the expected type/
      );
    }
  }
})();

// Test type checks when creating a global initialized with wasm objects
// provided as externref.
(function TestReferenceGlobalsImportValue() {
  print(arguments.callee.name);

  let exporting_instance = (function() {
    let builder = new WasmModuleBuilder();
    builder.setSingletonRecGroups();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
        builder.addStruct([makeField(kWasmI32, false)], type_super);
    let type_other = builder.addStruct([makeField(kWasmI64, false)]);

    builder.addFunction("create_super", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, type_super,
      kGCPrefix, kExprExternExternalize])
    .exportFunc();
    builder.addFunction("create_sub", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, type_sub,
      kGCPrefix, kExprExternExternalize])
    .exportFunc();
    builder.addFunction("create_other", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI64Const, 42,
      kGCPrefix, kExprStructNew, type_other,
      kGCPrefix, kExprExternExternalize])
    .exportFunc();
    builder.addFunction("create_null", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprRefNull, kNullRefCode,
      kGCPrefix, kExprExternExternalize])
    .exportFunc();

    return builder.instantiate({});
  })();

  let tests = [
    //valid |type             |imported_value
    [true,  "super",          "super"],
    [true,  "sub",            "sub"],
    [true,  "super",          "sub"],
    [false, "sub",            "super"],
    [false, "sub",            "other"],
    [false, "super",          "null"],
    [true,  "super_nullable", "super"],
    [true,  "super_nullable", "sub"],
    [true,  "super_nullable", "sub"],
    [false, "super_nullable", "other"],
    [false, "sub_nullable",   "super"],
    [true,  "super_nullable", "null"],
  ];
  for (let[expected_valid, type, imported_value] of tests) {
    print(`test ${type} imports ${imported_value}`);
    let builder = new WasmModuleBuilder();
    builder.setSingletonRecGroups();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
      builder.addStruct([makeField(kWasmI32, false)], type_super);
    let types = {
      super: wasmRefType(type_super),
      sub: wasmRefType(type_sub),
      super_nullable: wasmRefNullType(type_super),
      sub_nullable: wasmRefNullType(type_sub),
    };
    builder.addImportedGlobal("imports", "global", types[type], false);
    let init_value = exporting_instance.exports[`create_${imported_value}`]();
    let imports = {global: init_value};
    if (expected_valid) {
      builder.addFunction("read_global", makeSig([], [kWasmI32]))
      .addBody([
        kExprBlock, kWasmVoid,
          kExprGlobalGet, 0,
          kExprBrOnNull, 0,
          kGCPrefix, kExprStructGet, types[type].heap_type, 0,
          kExprReturn,
        kExprEnd,
        ...wasmI32Const(-1),
      ])
      .exportFunc();

      let instance = builder.instantiate({imports});
      assertEquals(imported_value == "null" ? -1 : 42,
                   instance.exports.read_global());
    } else {
      assertThrows(
        () => builder.instantiate({imports}),
        WebAssembly.LinkError
      );
    }
  }
})();

(function TestReferenceGlobalsImportInvalidJsValues() {
  print(arguments.callee.name);
  let invalid_values =
      [undefined, {}, [], 0, NaN, null, /regex/, true, false, ""];
  for (let value of invalid_values) {
    print(`test invalid value ${JSON.stringify(value)}`);
    let builder = new WasmModuleBuilder();
    let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
    let ref_type = wasmRefType(struct_type);
    builder.addImportedGlobal("imports", "value", ref_type, false);
    assertThrows(
      () => builder.instantiate({imports: {value}}),
      WebAssembly.LinkError);
  }
})();

(function TestReferenceGlobalsAbstractTypes() {
  print(arguments.callee.name);
  let exporting_instance = (function() {
    let builder = new WasmModuleBuilder();
    builder.setSingletonRecGroups();
    let type_struct = builder.addStruct([makeField(kWasmI32, false)]);
    let type_array = builder.addArray(kWasmI32);

    builder.addFunction("create_struct", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, type_struct,
      kGCPrefix, kExprExternExternalize])
    .exportFunc();
    builder.addFunction("create_array", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprArrayNewFixed, type_array, 1,
      kGCPrefix, kExprExternExternalize])
    .exportFunc();
    return builder.instantiate({});
  })();

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "any1", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any2", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any3", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any4", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any4", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any5", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "eq1", kWasmEqRef, false);
  builder.addImportedGlobal("imports", "eq2", kWasmEqRef, false);
  builder.addImportedGlobal("imports", "eq3", kWasmEqRef, false);
  builder.addImportedGlobal("imports", "array", kWasmArrayRef, false);
  builder.instantiate({imports : {
    any1: exporting_instance.exports.create_struct(),
    any2: exporting_instance.exports.create_array(),
    any3: 12345, // i31
    any4: null,
    any5: "test string",
    eq1: 12345,
    eq2: exporting_instance.exports.create_array(),
    eq3: exporting_instance.exports.create_struct(),
    array: exporting_instance.exports.create_array(),
  }});
})();

(function TestReferenceGlobalsStrings() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "string1", kWasmStringRef, false);
  builder.addImportedGlobal("imports", "string2", kWasmStringRef, false);
  builder.addImportedGlobal("imports", "any", kWasmAnyRef, false);

  builder.addFunction("get_string1", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 0, kGCPrefix, kExprExternExternalize])
  .exportFunc();
  builder.addFunction("get_string2", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 1, kGCPrefix, kExprExternExternalize])
  .exportFunc();
  builder.addFunction("get_any", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 2, kGCPrefix, kExprExternExternalize])
  .exportFunc();

  let instance = builder.instantiate({imports : {
    string1: "Content of string1",
    string2: null,
    any: "Content of any",
  }});

  assertEquals("Content of string1", instance.exports.get_string1());
  assertEquals(null, instance.exports.get_string2());
  assertEquals("Content of any", instance.exports.get_any());
})();
