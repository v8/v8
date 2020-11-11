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

TEST(WrapperBudget) {
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

    // Check that the generic-wrapper budget has initially a value of
    // kGenericWrapperBudget.
    CHECK_EQ(
        main_export->shared().wasm_exported_function_data().wrapper_budget(),
        kGenericWrapperBudget);
    CHECK_GT(kGenericWrapperBudget, 0);

    // Call the exported Wasm function and get the result.
    Handle<Object> params[2] = {Handle<Object>(Smi::FromInt(6), isolate),
                                Handle<Object>(Smi::FromInt(7), isolate)};
    static const int32_t kExpectedValue = 42;
    Handle<Object> receiver = isolate->factory()->undefined_value();
    MaybeHandle<Object> maybe_result =
        Execution::Call(isolate, main_export, receiver, 2, params);
    Handle<Object> result = maybe_result.ToHandleChecked();

    // Check that the budget has now a value of (kGenericWrapperBudget - 1).
    CHECK_EQ(
        main_export->shared().wasm_exported_function_data().wrapper_budget(),
        kGenericWrapperBudget - 1);

    CHECK(result->IsSmi() && Smi::ToInt(*result) == kExpectedValue);
  }
  Cleanup();
}

TEST(WrapperReplacement) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&FLAG_wasm_generic_wrapper, true);

    TestSignatures sigs;
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);

    // Define the Wasm function.
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);
    WasmFunctionBuilder* f = builder->AddFunction(sigs.i_i());
    f->builder()->AddExport(CStrVector("main"), f);
    byte code[] = {WASM_RETURN1(WASM_GET_LOCAL(0)), WASM_END};
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

    // Get the exported function.
    MaybeHandle<WasmExportedFunction> maybe_export =
        testing::GetExportedFunction(isolate, instance.ToHandleChecked(),
                                     "main");
    Handle<WasmExportedFunction> main_export = maybe_export.ToHandleChecked();

    // Check that the generic-wrapper budget has initially a value of
    // kGenericWrapperBudget.
    CHECK_EQ(
        main_export->shared().wasm_exported_function_data().wrapper_budget(),
        kGenericWrapperBudget);
    CHECK_GT(kGenericWrapperBudget, 0);

    // Call the exported Wasm function as many times as required to almost
    // exhaust the budget for using the generic wrapper.
    const int budget = static_cast<int>(kGenericWrapperBudget);
    for (int i = budget; i > 1; --i) {
      // Verify that the wrapper to be used is still the generic one.
      Code wrapper =
          main_export->shared().wasm_exported_function_data().wrapper_code();
      CHECK(wrapper.is_builtin() &&
            wrapper.builtin_index() == Builtins::kGenericJSToWasmWrapper);
      // Call the function.
      int32_t expected_value = i;
      Handle<Object> params[1] = {
          Handle<Object>(Smi::FromInt(expected_value), isolate)};
      Handle<Object> receiver = isolate->factory()->undefined_value();
      MaybeHandle<Object> maybe_result =
          Execution::Call(isolate, main_export, receiver, 1, params);
      Handle<Object> result = maybe_result.ToHandleChecked();
      // Verify that the budget has now a value of (i - 1) and the return value
      // is correct.
      CHECK_EQ(
          main_export->shared().wasm_exported_function_data().wrapper_budget(),
          i - 1);
      CHECK(result->IsSmi() && Smi::ToInt(*result) == expected_value);
    }

    // Get the wrapper-code object before making the call that will kick off the
    // wrapper replacement.
    Code wrapper_before_call =
        main_export->shared().wasm_exported_function_data().wrapper_code();
    // Verify that the wrapper before the call is the generic wrapper.
    CHECK(wrapper_before_call.is_builtin() &&
          wrapper_before_call.builtin_index() ==
              Builtins::kGenericJSToWasmWrapper);

    // Call the exported Wasm function one more time to kick off the wrapper
    // replacement.
    int32_t expected_value = 42;
    Handle<Object> params[1] = {
        Handle<Object>(Smi::FromInt(expected_value), isolate)};
    Handle<Object> receiver = isolate->factory()->undefined_value();
    MaybeHandle<Object> maybe_result =
        Execution::Call(isolate, main_export, receiver, 1, params);
    Handle<Object> result = maybe_result.ToHandleChecked();
    // Check that the budget has been exhausted and the result is correct.
    CHECK_EQ(
        main_export->shared().wasm_exported_function_data().wrapper_budget(),
        0);
    CHECK(result->IsSmi() && Smi::ToInt(*result) == expected_value);

    // Verify that the wrapper-code object has changed.
    Code wrapper_after_call =
        main_export->shared().wasm_exported_function_data().wrapper_code();
    CHECK_NE(wrapper_after_call, wrapper_before_call);
    // Verify that the wrapper is now a specific one.
    CHECK(wrapper_after_call.kind() == CodeKind::JS_TO_WASM_FUNCTION);
  }
  Cleanup();
}

TEST(EagerWrapperReplacement) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&FLAG_wasm_generic_wrapper, true);

    TestSignatures sigs;
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);

    // Define three Wasm functions.
    // Two of these functions (add and mult) will share the same signature,
    // while the other one (id) won't.
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);
    WasmFunctionBuilder* add = builder->AddFunction(sigs.i_ii());
    add->builder()->AddExport(CStrVector("add"), add);
    byte add_code[] = {WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)),
                       WASM_END};
    add->EmitCode(add_code, sizeof(add_code));
    WasmFunctionBuilder* mult = builder->AddFunction(sigs.i_ii());
    mult->builder()->AddExport(CStrVector("mult"), mult);
    byte mult_code[] = {WASM_I32_MUL(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)),
                        WASM_END};
    mult->EmitCode(mult_code, sizeof(mult_code));
    WasmFunctionBuilder* id = builder->AddFunction(sigs.i_i());
    id->builder()->AddExport(CStrVector("id"), id);
    byte id_code[] = {WASM_GET_LOCAL(0), WASM_END};
    id->EmitCode(id_code, sizeof(id_code));

    // Compile module.
    ZoneBuffer buffer(&zone);
    builder->WriteTo(&buffer);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    testing::SetupIsolateForWasmModule(isolate);
    ErrorThrower thrower(isolate, "CompileAndRunWasmModule");
    MaybeHandle<WasmInstanceObject> maybe_instance =
        CompileAndInstantiateForTesting(
            isolate, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));
    Handle<WasmInstanceObject> instance = maybe_instance.ToHandleChecked();

    // Get the exported functions.
    Handle<WasmExportedFunction> add_export =
        testing::GetExportedFunction(isolate, instance, "add")
            .ToHandleChecked();
    Handle<WasmExportedFunction> mult_export =
        testing::GetExportedFunction(isolate, instance, "mult")
            .ToHandleChecked();
    Handle<WasmExportedFunction> id_export =
        testing::GetExportedFunction(isolate, instance, "id").ToHandleChecked();

    // Get the function data for all exported functions.
    WasmExportedFunctionData add_function_data =
        add_export->shared().wasm_exported_function_data();
    WasmExportedFunctionData mult_function_data =
        mult_export->shared().wasm_exported_function_data();
    WasmExportedFunctionData id_function_data =
        id_export->shared().wasm_exported_function_data();

    // Set the remaining generic-wrapper budget for add to 1,
    // so that the next call to it will cause the function to tier up.
    add_function_data.set_wrapper_budget(1);

    // Verify that the generic-wrapper budgets for all functions are correct.
    CHECK_EQ(add_function_data.wrapper_budget(), 1);
    CHECK_EQ(mult_function_data.wrapper_budget(), kGenericWrapperBudget);
    CHECK_EQ(id_function_data.wrapper_budget(), kGenericWrapperBudget);

    // Verify that all functions are set to use the generic wrapper.
    CHECK(add_function_data.wrapper_code().is_builtin() &&
          add_function_data.wrapper_code().builtin_index() ==
              Builtins::kGenericJSToWasmWrapper);
    CHECK(mult_function_data.wrapper_code().is_builtin() &&
          mult_function_data.wrapper_code().builtin_index() ==
              Builtins::kGenericJSToWasmWrapper);
    CHECK(id_function_data.wrapper_code().is_builtin() &&
          id_function_data.wrapper_code().builtin_index() ==
              Builtins::kGenericJSToWasmWrapper);

    // Call the add function to trigger the tier up.
    {
      int32_t expected_value = 21;
      Handle<Object> params[2] = {Handle<Object>(Smi::FromInt(10), isolate),
                                  Handle<Object>(Smi::FromInt(11), isolate)};
      Handle<Object> receiver = isolate->factory()->undefined_value();
      MaybeHandle<Object> maybe_result =
          Execution::Call(isolate, add_export, receiver, 2, params);
      Handle<Object> result = maybe_result.ToHandleChecked();
      CHECK(result->IsSmi() && Smi::ToInt(*result) == expected_value);
    }

    // Verify that the generic-wrapper budgets for all functions are correct.
    CHECK_EQ(add_function_data.wrapper_budget(), 0);
    CHECK_EQ(mult_function_data.wrapper_budget(), kGenericWrapperBudget);
    CHECK_EQ(id_function_data.wrapper_budget(), kGenericWrapperBudget);

    // Verify that the tier up of the add function replaced the wrapper
    // for both the add and the mult functions, but not the id function.
    CHECK(add_function_data.wrapper_code().kind() ==
          CodeKind::JS_TO_WASM_FUNCTION);
    CHECK(mult_function_data.wrapper_code().kind() ==
          CodeKind::JS_TO_WASM_FUNCTION);
    CHECK(id_function_data.wrapper_code().is_builtin() &&
          id_function_data.wrapper_code().builtin_index() ==
              Builtins::kGenericJSToWasmWrapper);

    // Call the mult function to verify that the compiled wrapper is used.
    {
      int32_t expected_value = 42;
      Handle<Object> params[2] = {Handle<Object>(Smi::FromInt(7), isolate),
                                  Handle<Object>(Smi::FromInt(6), isolate)};
      Handle<Object> receiver = isolate->factory()->undefined_value();
      MaybeHandle<Object> maybe_result =
          Execution::Call(isolate, mult_export, receiver, 2, params);
      Handle<Object> result = maybe_result.ToHandleChecked();
      CHECK(result->IsSmi() && Smi::ToInt(*result) == expected_value);
    }
    // Verify that mult's budget is still intact, which means that the call
    // didn't go through the generic wrapper.
    CHECK_EQ(mult_function_data.wrapper_budget(), kGenericWrapperBudget);

    // Call the id function to verify that the generic wrapper is used.
    {
      int32_t expected_value = 12;
      Handle<Object> params[1] = {
          Handle<Object>(Smi::FromInt(expected_value), isolate)};
      Handle<Object> receiver = isolate->factory()->undefined_value();
      MaybeHandle<Object> maybe_result =
          Execution::Call(isolate, id_export, receiver, 1, params);
      Handle<Object> result = maybe_result.ToHandleChecked();
      CHECK(result->IsSmi() && Smi::ToInt(*result) == expected_value);
    }
    // Verify that id's budget decreased by 1, which means that the call
    // used the generic wrapper.
    CHECK_EQ(id_function_data.wrapper_budget(), kGenericWrapperBudget - 1);
  }
  Cleanup();
}
#endif

}  // namespace test_run_wasm_wrappers
}  // namespace wasm
}  // namespace internal
}  // namespace v8
