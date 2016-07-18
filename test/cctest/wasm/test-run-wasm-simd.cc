// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include "src/wasm/encoder.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-macro-gen.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"

#include "test/cctest/cctest.h"
#include "test/cctest/wasm/test-signatures.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

namespace {
void TestModule(Zone* zone, WasmModuleBuilder* builder,
                int32_t expected_result) {
  FLAG_wasm_simd_prototype = true;
  FLAG_wasm_num_compilation_tasks = 0;
  ZoneBuffer buffer(zone);
  builder->WriteTo(buffer);

  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  WasmJs::InstallWasmFunctionMap(isolate, isolate->native_context());
  int32_t result =
      testing::CompileAndRunWasmModule(isolate, buffer.begin(), buffer.end());
  CHECK_EQ(expected_result, result);
}

void ExportAsMain(WasmFunctionBuilder* f) {
  static const char kMainName[] = "main";
  f->SetExported();
  f->SetName(kMainName, arraysize(kMainName) - 1);
}
}  // namespace

TEST(Run_WasmMoule_simd) {
  v8::base::AccountingAllocator allocator;
  Zone zone(&allocator);
  TestSignatures sigs;

  WasmModuleBuilder* builder = new (&zone) WasmModuleBuilder(&zone);
  uint16_t f_index = builder->AddFunction();
  WasmFunctionBuilder* f = builder->FunctionAt(f_index);
  f->SetSignature(sigs.i_i());
  ExportAsMain(f);

  byte code[] = {WASM_SIMD_I32x4_EXTRACT_LANE(
      WASM_SIMD_I32x4_SPLAT(WASM_I8(123)), WASM_I8(2))};
  f->EmitCode(code, sizeof(code));
  TestModule(&zone, builder, 123);
}
