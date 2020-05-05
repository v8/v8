// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/utils/utils.h"
#include "src/utils/vector.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_gc {

WASM_EXEC_TEST(BasicStruct) {
  // TODO(7748): Implement support in other tiers.
  if (execution_tier == ExecutionTier::kLiftoff) return;
  if (execution_tier == ExecutionTier::kInterpreter) return;
  TestSignatures sigs;
  EXPERIMENTAL_FLAG_SCOPE(gc);
  EXPERIMENTAL_FLAG_SCOPE(anyref);
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  WasmModuleBuilder* builder = new (&zone) WasmModuleBuilder(&zone);
  StructType::Builder type_builder(&zone, 2);
  type_builder.AddField(kWasmI32);
  type_builder.AddField(kWasmI32);
  int32_t type_index = builder->AddStructType(type_builder.Build());
  ValueType kRefTypes[] = {ValueType(ValueType::kRef, type_index)};
  ValueType kOptRefType = ValueType(ValueType::kOptRef, type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  WasmFunctionBuilder* f = builder->AddFunction(sigs.i_v());
  f->builder()->AddExport(CStrVector("f"), f);
  byte f_code[] = {WASM_STRUCT_GET(type_index, 0,
                                   WASM_STRUCT_NEW(type_index, WASM_I32V(42),
                                                   WASM_I32V(64))),
                   kExprEnd};
  f->EmitCode(f_code, sizeof(f_code));

  WasmFunctionBuilder* g = builder->AddFunction(sigs.i_v());
  g->builder()->AddExport(CStrVector("g"), g);
  byte g_code[] = {WASM_STRUCT_GET(type_index, 1,
                                   WASM_STRUCT_NEW(type_index, WASM_I32V(42),
                                                   WASM_I32V(64))),
                   kExprEnd};
  g->EmitCode(g_code, sizeof(g_code));

  WasmFunctionBuilder* h = builder->AddFunction(&sig_q_v);
  h->builder()->AddExport(CStrVector("h"), h);
  byte h_code[] = {WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64)),
                   kExprEnd};
  h->EmitCode(h_code, sizeof(h_code));

  WasmFunctionBuilder* j = builder->AddFunction(sigs.i_v());
  uint32_t local_index = j->AddLocal(kOptRefType);
  uint32_t field_index = 0;
  j->builder()->AddExport(CStrVector("j"), j);
  byte i_code[] = {
      WASM_SET_LOCAL(local_index,
                     WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
      WASM_STRUCT_SET(type_index, field_index, WASM_GET_LOCAL(local_index),
                      WASM_I32V(-99)),
      WASM_STRUCT_GET(type_index, field_index, WASM_GET_LOCAL(local_index)),
      kExprEnd};
  j->EmitCode(i_code, sizeof(i_code));

  ZoneBuffer buffer(&zone);
  builder->WriteTo(&buffer);

  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  testing::SetupIsolateForWasmModule(isolate);
  ErrorThrower thrower(isolate, "Test");
  Handle<WasmInstanceObject> instance =
      testing::CompileAndInstantiateForTesting(
          isolate, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()))
          .ToHandleChecked();

  CHECK_EQ(42, testing::CallWasmFunctionForTesting(isolate, instance, &thrower,
                                                   "f", 0, nullptr));
  CHECK_EQ(64, testing::CallWasmFunctionForTesting(isolate, instance, &thrower,
                                                   "g", 0, nullptr));

  // TODO(7748): This uses the JavaScript interface to retrieve the plain
  // WasmStruct. Once the JS interaction story is settled, this may well
  // need to be changed.
  Handle<WasmExportedFunction> h_export =
      testing::GetExportedFunction(isolate, instance, "h").ToHandleChecked();
  Handle<Object> undefined = isolate->factory()->undefined_value();
  Handle<Object> ref_result =
      Execution::Call(isolate, h_export, undefined, 0, nullptr)
          .ToHandleChecked();
  CHECK(ref_result->IsWasmStruct());

  CHECK_EQ(-99, testing::CallWasmFunctionForTesting(isolate, instance, &thrower,
                                                    "j", 0, nullptr));
}

}  // namespace test_gc
}  // namespace wasm
}  // namespace internal
}  // namespace v8
