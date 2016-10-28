// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function AddFunctions(builder) {
  let sig_index = builder.addType(kSig_i_ii);
  let mul = builder.addFunction("mul", sig_index)
    .addBody([
      kExprGetLocal, 0,  // --
      kExprGetLocal, 1,  // --
      kExprI32Mul        // --
    ]);
  let add = builder.addFunction("add", sig_index)
    .addBody([
      kExprGetLocal, 0,  // --
      kExprGetLocal, 1,  // --
      kExprI32Add        // --
    ]);
  let sub = builder.addFunction("sub", sig_index)
    .addBody([
      kExprGetLocal, 0,  // --
      kExprGetLocal, 1,  // --
      kExprI32Sub        // --
    ]);
  return {mul: mul, add: add, sub: sub};
}

(function ExportedTableTest() {
  print("ExportedTableTest...");

  let builder = new WasmModuleBuilder();

  let d = builder.addImport("js_div", kSig_i_ii);
  let f = AddFunctions(builder);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 33,  // --
      kExprGetLocal, 0,   // --
      kExprGetLocal, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  f.add.exportAs("blarg");

  builder.setFunctionTableLength(10);
  let g = builder.addImportedGlobal("base", undefined, kAstI32);
  builder.addFunctionTableInit(g, true, [f.mul.index, f.add.index,
                                         f.sub.index,
                                         d]);
  builder.addExportOfKind("table", kExternalTable, 0);

  let module = new WebAssembly.Module(builder.toBuffer());

  function js_div(a, b) { return (a / b) | 0; }

  for (let i = 0; i < 5; i++) {
    print(" base = " + i);
    let instance = new WebAssembly.Instance(module, {base: i, js_div: js_div});
    main = instance.exports.main;
    let table = instance.exports.table;
    assertTrue(table instanceof WebAssembly.Table);
    assertEquals(10, table.length);
    for (let j = 0; j < i; j++) {
      assertSame(null, table.get(j));
    }
    let mul = table.get(i+0);
    let add = table.get(i+1);
    let sub = table.get(i+2);

    print("  mul=" + mul);
    print("  add=" + add);
    print("  sub=" + sub);
    assertEquals("function", typeof mul);
    assertEquals("function", typeof add);
    assertEquals("function", typeof sub);
    assertEquals(2, mul.length);
    assertEquals(2, add.length);
    assertEquals(2, sub.length);
    assertEquals("blarg", add.name);

    let exp_div = table.get(i+3);
    assertEquals("function", typeof exp_div);
    print("  js_div=" + exp_div);
    // Should have a new, wrapped version of the import.
    assertFalse(js_div == exp_div);


    for (let j = i + 4; j < 10; j++) {
      assertSame(null, table.get(j));
    }

    assertEquals(-33, mul(-11, 3));
    assertEquals(4444444, add(3333333, 1111111));
    assertEquals(-9999, sub(1, 10000));
    assertEquals(-44, exp_div(-88.1, 2));
  }
})();
