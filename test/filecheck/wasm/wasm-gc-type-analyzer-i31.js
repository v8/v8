// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-wasm-typer --trace-turbo-graph --no-liftoff
// Flags: --no-wasm-lazy-compilation --no-wasm-assert-types

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const Box = 0;
const Arr = 1;
const Other = 2;
const sigBoxOrI31 =
    makeSig([kWasmI32, wasmRefType(Box), wasmRefType(kWasmI31Ref)], [kWasmI32]);

function compile(name, sig, body, locals = []) {
  let builder = new WasmModuleBuilder();
  builder.addStruct([makeField(kWasmI32, false)]);
  builder.addArray(kWasmI32);
  builder.addStruct([makeField(kWasmI64, false)]);
  let fn = builder.addFunction(name, sig);
  for (let l of locals) fn.addLocals(l, 1);
  fn.addBody(body);
  builder.instantiate();
}

// CHECK-LABEL: Begin compiling method checkI31FirstBrOnCast using Turboshaft
// CHECK: Refine type for object #[[PHI1:[0-9]+]](Phi) -> (ref 0)|i31
// CHECK: Branch): Refine type for object #[[PHI1]](Phi) -> (ref i31)
// CHECK: Branch): Refine type for object #[[PHI1]](Phi) -> (ref 0)
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK-NOT: WasmTypeCast(
// CHECK: Return(
// CHECK-LABEL: V8.TFTurboshaftWasmOptimize
// CHECK: [[P0:[0-9]+]]: Parameter()[1]
// CHECK: Branch(#[[P0]])[
// CHECK-NOT: Branch(
// CHECK: Return(
compile('checkI31FirstBrOnCast', sigBoxOrI31, [
  kExprBlock, kI31RefCode,
    kExprLocalGet, 0,
    kExprIf, kEqRefCode,
      kExprLocalGet, 1,
    kExprElse,
      kExprLocalGet, 2,
    kExprEnd,
    kGCPrefix, kExprBrOnCast, 0b01, 0, kEqRefCode, kI31RefCode,
    kGCPrefix, kExprRefCast, Box,
    kGCPrefix, kExprStructGet, Box, 0,
    kExprReturn,
  kExprEnd,
  kGCPrefix, kExprI31GetS,
]);

// CHECK-LABEL: Begin compiling method checkStructFirstBrOnCastFail using Turboshaft
// CHECK: Refine type for object #[[PHI2:[0-9]+]](Phi) -> (ref 0)|i31
// CHECK: Branch): Refine type for object #[[PHI2]](Phi) -> (ref i31)
// CHECK: Branch): Refine type for object #[[PHI2]](Phi) -> (ref 0)
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCast(
// CHECK: Return(
compile('checkStructFirstBrOnCastFail', sigBoxOrI31, [
  kExprBlock, kEqRefCode,
    kExprLocalGet, 0,
    kExprIf, kEqRefCode,
      kExprLocalGet, 1,
    kExprElse,
      kExprLocalGet, 2,
    kExprEnd,
    kGCPrefix, kExprBrOnCastFail, 0b01, 0, kEqRefCode, Box,
    kGCPrefix, kExprStructGet, Box, 0,
    kExprReturn,
  kExprEnd,
  kGCPrefix, kExprRefCast, kI31RefCode,
  kGCPrefix, kExprI31GetS,
]);

// CHECK-LABEL: Begin compiling method castStructOrArray using Turboshaft
// CHECK: Refine type for object #[[PHI3:[0-9]+]](Phi) -> (ref eq)∖i31
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK: WasmTypeCast(#[[PHI3]], {{.*}})[(ref eq)∖i31 -> (ref 0)
compile('castStructOrArray',
    makeSig([kWasmI32, wasmRefType(Box), wasmRefType(Arr)], [wasmRefType(Box)]),
    [
      kExprBlock, kAnyRefCode,
        kExprLocalGet, 0,
        kExprIf, kEqRefCode,
          kExprLocalGet, 1,
        kExprElse,
          kExprLocalGet, 2,
        kExprEnd,
        kGCPrefix, kExprBrOnCast, 0b01, 0, kEqRefCode, kI31RefCode,
        kGCPrefix, kExprRefCast, Box,
        kExprReturn,
      kExprEnd,
      kExprUnreachable,
    ]);

// CHECK-LABEL: Begin compiling method narrowI31RefToNull using Turboshaft
// CHECK: Branch): Refine type for object #[[P0:[0-9]+]](Parameter) -> (ref i31)
// CHECK: Branch): Refine type for object #[[P0]](Parameter) -> nullref
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK-NOT: WasmTypeCast(
// CHECK: IsNull(#[[P0]])[i31ref
// CHECK-NOT: IsNull(
// CHECK: Return(
compile('narrowI31RefToNull', makeSig([kWasmI31Ref], [kWasmI32]), [
  // Widen static type to eqref via local 1 so the decoder emits WasmTypeCheck
  // instead of folding at decode time.
  kExprLocalGet, 0, kExprLocalTee, 1,
  kGCPrefix, kExprRefTest, kI31RefCode,
  kExprIf, kWasmI32,
    kExprLocalGet, 1, kGCPrefix, kExprRefCast, kI31RefCode,
    kGCPrefix, kExprI31GetS,
  kExprElse,
    // local 1 is narrowed to nullref: ref.is_null folds to 1 and non-nullable
    // ref.test (ref $Box) folds to 0 without emitting IsNull or WasmTypeCheck.
    kExprLocalGet, 1, kExprRefIsNull,
    kExprLocalGet, 1, kGCPrefix, kExprRefTest, Box,
    kExprI32Sub,
  kExprEnd,
], [kWasmEqRef]);

// CHECK-LABEL: Begin compiling method refTestNullOnNullableStruct using Turboshaft
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK: IsNull(#{{[0-9]+}})[(ref null 0)
// CHECK: Return(
compile('refTestNullOnNullableStruct',
    makeSig([wasmRefNullType(Box)], [kWasmI32]), [
      // ref.test nullref on (ref null $Box) folds to IsNull via Intersection +
      // true_type.is_none_type().
      kExprLocalGet, 0, kGCPrefix, kExprRefTestNull, kNullRefCode,
    ]);

// CHECK-LABEL: Begin compiling method directRefI31GetS using Turboshaft
// CHECK-NOT: WasmTypeAnnotation(
// CHECK: Refine type for object #{{[0-9]+}}(WasmRefI31) -> (ref i31)
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmI31Get(
// CHECK: Shift({{.*}})[ShiftLeft, Word32]
// CHECK: Shift({{.*}})[ShiftRightArithmeticShiftOutZeros, Word32]
// CHECK: Return(
compile('directRefI31GetS', makeSig([kWasmI32], [kWasmI32]), [
  kExprLocalGet, 0, kGCPrefix, kExprRefI31, kGCPrefix, kExprI31GetS,
]);

// CHECK-LABEL: Begin compiling method directRefI31GetU using Turboshaft
// CHECK-NOT: WasmTypeAnnotation(
// CHECK: Refine type for object #{{[0-9]+}}(WasmRefI31) -> (ref i31)
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmI31Get(
// CHECK: WordBinop({{.*}})[BitwiseAnd, Word32]
// CHECK: Return(
compile('directRefI31GetU', makeSig([kWasmI32], [kWasmI32]), [
  kExprLocalGet, 0, kGCPrefix, kExprRefI31, kGCPrefix, kExprI31GetU,
]);

// Folding `ref.test` on a struct-vs-i31 diamond drops the check's use of the
// merge Phi, lets BranchEliminationReducer collapse the two consecutive
// branches into one, and lets LateEscapeAnalysis drop the unused struct
// allocation.
//
// CHECK-LABEL: Begin compiling method diamondPhiRefTestBranchElim using Turboshaft
// CHECK: Refine type for object #{{[0-9]+}}(Phi) -> (ref exact 0)|i31
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK: Return(
// CHECK-LABEL: V8.TFTurboshaftWasmOptimize
// CHECK: [[P0:[0-9]+]]: Parameter()[1]
// CHECK-NOT: Allocate
// CHECK: Branch(#[[P0]])[
// CHECK-NOT: Branch(
// CHECK-NOT: Allocate
// CHECK: Return(
compile('diamondPhiRefTestBranchElim',
    makeSig([kWasmI32, kWasmI32], [kWasmI32]), [
      kExprLocalGet, 0,
      kExprIf, kEqRefCode,
        kExprLocalGet, 1, kGCPrefix, kExprStructNew, Box,
      kExprElse,
        kExprI32Const, 0, kGCPrefix, kExprRefI31,
      kExprEnd,
      kGCPrefix, kExprRefTest, Box,
      kExprIf, kWasmI32,
        kExprLocalGet, 1, kExprI32Const, 1, kExprI32Add,
      kExprElse,
        kExprI32Const, 0,
      kExprEnd,
    ]);

// Diamond with a negated comparison (`(p0 < 5) == 0`): the
// MachineOptimizationReducer in LoopPeeling branches on `p0 < 5` instead and
// swaps the Branch targets (exercising the swapped-arm predecessor order),
// WasmGCOptimize folds `ref.test i31` to `IsNonZero(p0 < 5)`, and the
// MachineOptimizationReducer in WasmLowering folds the double negation back to
// `p0 < 5`.
//
// CHECK-LABEL: Begin compiling method diamondPhiRefTestCmp using Turboshaft
// CHECK: Refine type for object #{{[0-9]+}}(Phi) -> (ref 0)|i31
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK: Return(
// CHECK-LABEL: V8.TFTurboshaftWasmOptimize
// CHECK: [[CMP:[0-9]+]]: Comparison({{.*}})[SignedLessThan, Word32]
// CHECK-NOT: [Equal, Word32]
// CHECK: Return(#{{[0-9]+}}, #[[CMP]])
compile('diamondPhiRefTestCmp', sigBoxOrI31, [
  kExprLocalGet, 0, kExprI32Const, 5, kExprI32LtS, kExprI32Eqz,
  kExprIf, kEqRefCode,
    kExprLocalGet, 1,
  kExprElse,
    kExprLocalGet, 2,
  kExprEnd,
  kGCPrefix, kExprRefTest, kI31RefCode,
]);

// Both diamond arms ($Box and $Arr) are disjoint from $Other even though their
// merged union `(ref eq)\i31` is not; folds directly to constant 0.
//
// CHECK-LABEL: Begin compiling method diamondPhiRefTestConstantFalse using Turboshaft
// CHECK: Refine type for object #{{[0-9]+}}(Phi) -> (ref eq)∖i31
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK: Return(
// CHECK-LABEL: V8.TFTurboshaftWasmOptimize
// CHECK: [[ZERO:[0-9]+]]: Constant()[word32: 0]
// CHECK: Return(#{{[0-9]+}}, #[[ZERO]])
compile('diamondPhiRefTestConstantFalse',
    makeSig([kWasmI32, wasmRefType(Box), wasmRefType(Arr)], [kWasmI32]), [
      kExprLocalGet, 0,
      kExprIf, kEqRefCode,
        kExprLocalGet, 1,
      kExprElse,
        kExprLocalGet, 2,
      kExprEnd,
      kGCPrefix, kExprRefTest, Other,
    ]);

// Diamond whose two arms are disjoint structs ($Box vs $Other): `ref.test $Box`
// folds to `cond`, and `ProcessBranchOnTarget` narrows the `else` branch to
// `(ref exact $Other)` so `ref.cast $Other` is also eliminated.
//
// CHECK-LABEL: Begin compiling method diamondPhiBranchRefineElseCast using Turboshaft
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK-NOT: WasmTypeCheck(
// CHECK-NOT: WasmTypeCast(
// CHECK: Return(
compile('diamondPhiBranchRefineElseCast',
    makeSig([kWasmI32, kWasmI32], [kWasmI32]), [
      kExprLocalGet, 0,
      kExprIf, kStructRefCode,
        kExprLocalGet, 1, kGCPrefix, kExprStructNew, Box,
      kExprElse,
        kExprI64Const, 7, kGCPrefix, kExprStructNew, Other,
      kExprEnd,
      kExprLocalTee, 2,
      kGCPrefix, kExprRefTest, Box,
      kExprIf, kWasmI32,
        kExprLocalGet, 2, kGCPrefix, kExprRefCast, Box,
        kGCPrefix, kExprStructGet, Box, 0,
      kExprElse,
        kExprLocalGet, 2, kGCPrefix, kExprRefCast, Other,
        kGCPrefix, kExprStructGet, Other, 0,
        kExprI32ConvertI64,
      kExprEnd,
    ], [kWasmStructRef]);

// Diamond whose guarding condition is itself an RTT-based `WasmTypeCheckOp`
// (`ref.test $Box`): reusing that condition eliminates the second
// `WasmTypeCheck` and lets BranchElimination + LateEscapeAnalysis drop the
// unused struct allocation.
//
// CHECK-LABEL: Begin compiling method diamondPhiCondIsTypeCheck using Turboshaft
// CHECK-LABEL: V8.TFTurboshaftWasmGCOptimize
// CHECK: WasmTypeCheck(
// CHECK-NOT: WasmTypeCheck(
// CHECK: Return(
// CHECK-LABEL: V8.TFTurboshaftWasmOptimize
// CHECK-NOT: Allocate
// CHECK: Return(
compile('diamondPhiCondIsTypeCheck',
    makeSig([kWasmAnyRef, kWasmI32], [kWasmI32]), [
      kExprLocalGet, 0, kGCPrefix, kExprRefTest, Box,
      kExprIf, kEqRefCode,
        kExprLocalGet, 1, kGCPrefix, kExprStructNew, Box,
      kExprElse,
        kExprI32Const, 0, kGCPrefix, kExprRefI31,
      kExprEnd,
      kGCPrefix, kExprRefTest, Box,
      kExprIf, kWasmI32,
        kExprLocalGet, 1, kExprI32Const, 1, kExprI32Add,
      kExprElse,
        kExprI32Const, 0,
      kExprEnd,
    ]);
