// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

// Set unittest to false to run this test and just print results, without failing.
let unittest = true;

function run(expected, name, code) {
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_v_v).
    addBody(code);
  let buffer = builder.toBuffer();

  while (name.length < 35) name += " ";

  var valid = WebAssembly.validate(buffer);
  var success = expected == undefined ? "" : (valid == expected ? "(pass)" : "(fail)");
  if (valid) {
    print(name + "|   valid " + success);
  } else {
    print(name + "| invalid " + success);
  }

  if (unittest && expected != undefined) {
    assertTrue(valid === expected);
  }
}

let V = true;
let I = false;
let X = undefined;

let nop = kExprNop;
let iadd = kExprI32Add;
let unr = kExprUnreachable;
let ret = kExprReturn;
let br0 = [kExprBr, 0];
let brt = [kExprBrTable, 0, 0];
let f32 = [kExprF32Const, 0, 0, 0, 0];
let zero = [kExprI32Const, 0];
let if_else_empty = [kExprIf, kWasmStmt, kExprElse, kExprEnd];
let if_unr = [kExprIf, kWasmStmt, kExprUnreachable, kExprEnd];
let if_else_unr = [kExprIf, kWasmStmt, kExprUnreachable, kExprElse, kExprUnreachable, kExprEnd];
let block_unr = [kExprBlock, kWasmStmt, kExprUnreachable, kExprEnd];
let loop_unr = [kExprLoop, kWasmStmt, kExprUnreachable, kExprEnd];
let block_block_unr = [kExprBlock, kWasmStmt, kExprBlock, kWasmStmt, kExprUnreachable, kExprEnd, kExprEnd];
let drop = kExprDrop;

run(V, "U", [unr]);
run(V, 'U U', [unr, unr]);
run(V, "(if 0 () else ())", [...zero, ...if_else_empty]);
run(V, "(if 0 U)", [...zero, ...if_unr]);
run(V, "(if 0 U U)", [...zero, ...if_else_unr]);
run(I, "(if 0 U) iadd", [...zero, ...if_unr, iadd]);
run(I, "(if 0 U) iadd drop", [...zero, ...if_unr, iadd, drop]);
run(V, "0 0 (if 0 U) iadd drop", [...zero, ...zero, ...zero, ...if_unr, iadd, drop]);
run(V, "(if 0 U) 0 0 iadd drop", [...zero, ...if_unr, ...zero, ...zero, iadd, drop]);

run(V, "(block U)", [...block_unr]);
run(V, "(loop U)", [...loop_unr]);
run(V, "(if 0 U U)", [...zero, ...if_else_unr]);

run(V, 'U nop', [unr, nop]);
run(V, 'U iadd drop', [unr, iadd, drop]);
run(V, 'br0 iadd drop', [...br0, iadd, drop]);
run(V, '0 brt iadd drop', [...zero, ...brt, iadd, drop]);
run(V, 'ret iadd drop', [ret, iadd, drop]);

run(V, 'U 0 0 iadd drop', [unr, ...zero, ...zero, iadd, drop]);
run(V, 'br0 0 0 iadd drop', [...br0, ...zero, ...zero, iadd, drop]);
run(V, '0 brt 0 0 iadd drop', [...zero, ...brt, ...zero, ...zero, iadd, drop]);
run(V, 'ret 0 0 iadd drop', [ret, ...zero, ...zero, iadd, drop]);

run(I, 'br0 iadd', [...br0, iadd]);
run(I, '0 brt iadd', [...zero, ...brt, iadd]);
run(I, 'ret iadd', [ret, iadd]);
run(I, '0 0 br0 iadd', [...zero, ...zero, ...br0, iadd]);
run(I, '0 0 ret iadd', [...zero, ...zero, ret, iadd]);

run(I, '(block U) iadd drop', [...block_unr, iadd, drop]);
run(I, '(block (block U)) iadd drop', [...block_block_unr, iadd, drop]);
run(I, '(loop U) iadd drop', [...loop_unr, iadd]);
run(I, '(if 0 U U) iadd drop', [...zero, ...if_else_unr, iadd, drop]);

run(V, 'U 0 0 iadd drop', [unr, ...zero, ...zero, iadd, drop]);
run(V, "(block U) 0 0 iadd drop", [...block_unr, ...zero, ...zero, iadd, drop]);
run(V, "(loop U) 0 0 iadd drop", [...loop_unr, ...zero, ...zero, iadd, drop]);
run(V, "(block (block U)) 0 0 iadd drop", [...block_block_unr, ...zero, ...zero, iadd, drop]);
run(V, '0 0 U iadd drop', [...zero, ...zero, unr, iadd, drop]);
run(V, "0 0 (block U) iadd drop", [...zero, ...zero, ...block_unr, iadd, drop]);
run(V, "0 0 (loop U) iadd drop", [...zero, ...zero, ...loop_unr, iadd, drop]);
run(V, "0 0 (block (block U)) iadd drop", [...zero, ...zero, ...block_block_unr, iadd, drop]);

run(I, "U 0f iadd drop", [unr, ...f32, iadd, drop]);
run(I, "U 0f 0 iadd drop", [unr, ...f32, ...zero, iadd, drop]);
run(I, "U 0 0f iadd drop", [unr, ...zero, ...f32, iadd, drop]);
run(I, "(if 0 U U) 0f 0 iadd drop", [...zero, ...if_else_unr, ...f32, ...zero, iadd, drop]);
run(I, "(block U) 0f 0 iadd drop", [...block_unr, ...f32, ...zero, iadd, drop]);
run(I, "(loop U) 0f 0 iadd drop", [...loop_unr, ...f32, ...zero, iadd, drop]);
run(I, "(block (block U)) 0f 0 iadd drop", [...block_block_unr, ...f32, ...zero, iadd, drop]);

run(V, '0f U iadd drop', [...f32, unr, iadd, drop]);
run(V, '0f 0 U iadd drop', [...f32, ...zero, unr, iadd, drop]);
run(I, "0f 0 (block U) iadd drop", [...f32, ...zero, ...block_unr, iadd, drop]);
run(V, '0f U 0 iadd drop', [...f32, unr, ...zero, iadd, drop]);
run(I, "0 U 0f iadd drop", [...zero, unr, ...zero, ...f32, iadd, drop]);
run(I, "0f (block U) 0 iadd drop", [...f32, ...block_unr, ...zero, iadd, drop]);
run(I, "0 (block U) 0f iadd drop", [...zero, ...block_unr, ...f32, iadd, drop]);
