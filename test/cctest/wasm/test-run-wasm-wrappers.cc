// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_wrappers {

using testing::CompileAndInstantiateForTesting;

#ifdef V8_TARGET_ARCH_X64
namespace {
void Cleanup() {
  // By sending a low memory notifications, we will try hard to collect all
  // garbage and will therefore also invoke all weak callbacks of actually
  // unreachable persistent handles.
  Isolate* isolate = CcTest::InitIsolateOnce();
  reinterpret_cast<v8::Isolate*>(isolate)->LowMemoryNotification();
}

}  // namespace

TEST(CallCounter) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&FLAG_wasm_generic_wrapper, true);

    TestSignatures sigs;
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);

    // Define the Wasm function.
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);
    WasmFunctionBuilder* f = builder->AddFunction(sigs.i_ii());
    f->builder()->AddExport(CStrVector("main"), f);
    byte code[] = {WASM_I32_MUL(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)),
                   WASM_END};
    f->EmitCode(code, sizeof(code));

    // Compile module.
    ZoneBuffer buffer(&zone);
    builder->WriteTo(&buffer);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    testing::SetupIsolateForWasmModule(isolate);
    ErrorThrower thrower(isolate, "CompileAndRunWasmModule");
    MaybeHandle<WasmInstanceObject> instance = CompileAndInstantiateForTesting(
        isolate, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));

    MaybeHandle<WasmExportedFunction> maybe_export =
        testing::GetExportedFunction(isolate, instance.ToHandleChecked(),
                                     "main");
    Handle<WasmExportedFunction> main_export = maybe_export.ToHandleChecked();

    // Check that the counter has initially a value of 0.
    CHECK_EQ(main_export->shared().wasm_exported_function_data().call_count(),
             0);

    // Call the exported Wasm function and get the result.
    Handle<Object> params[2] = {Handle<Object>(Smi::FromInt(6), isolate),
                                Handle<Object>(Smi::FromInt(7), isolate)};
    static const int32_t kExpectedValue = 42;
    Handle<Object> receiver = isolate->factory()->undefined_value();
    MaybeHandle<Object> maybe_result =
        Execution::Call(isolate, main_export, receiver, 2, params);
    Handle<Object> result = maybe_result.ToHandleChecked();

    // Check that the counter has now a value of 1.
    CHECK_EQ(main_export->shared().wasm_exported_function_data().call_count(),
             1);

    CHECK(result->IsSmi() && Smi::ToInt(*result) == kExpectedValue);
  }
  Cleanup();
}
#endif

}  // namespace test_run_wasm_wrappers
}  // namespace wasm
}  // namespace internal
}  // namespace v8
