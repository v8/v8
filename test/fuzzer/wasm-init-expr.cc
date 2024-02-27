// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm-fuzzer-common.h"

// This fuzzer fuzzes initializer expressions used e.g. in globals.
// The fuzzer creates a set of globals with initializer expressions and a set of
// functions containing the same body as these initializer expressions.
// The global value should be equal to the result of running the corresponding
// function.

namespace v8::internal::wasm::fuzzing {

#define CHECK_FLOAT_EQ(expected, actual)                    \
  if (std::isnan(expected)) {                               \
    CHECK(std::isnan(actual));                              \
  } else {                                                  \
    CHECK_EQ(expected, actual);                             \
    CHECK_EQ(std::signbit(expected), std::signbit(actual)); \
  }

namespace {
bool IsNullOrWasmNull(Tagged<Object> obj) {
  return IsNull(obj) || IsWasmNull(obj);
}

Handle<Object> GetExport(Isolate* isolate, Handle<WasmInstanceObject> instance,
                         const char* name) {
  Handle<JSObject> exports_object;
  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  exports_object = Handle<JSObject>::cast(
      JSObject::GetProperty(isolate, instance, exports).ToHandleChecked());

  Handle<Name> main_name = isolate->factory()->NewStringFromAsciiChecked(name);
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, exports_object, main_name, &desc);
  CHECK(property_found.FromMaybe(false));
  return desc.value();
}

void FuzzIt(base::Vector<const uint8_t> data) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  v8::Isolate::Scope isolate_scope(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  i_isolate->heap()->ClearWasmCanonicalRttsForTesting();

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // We explicitly enable staged WebAssembly features here to increase fuzzer
  // coverage. For libfuzzer fuzzers it is not possible that the fuzzer enables
  // the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  size_t expression_count = 0;
  base::Vector<const uint8_t> buffer =
      GenerateWasmModuleForInitExpressions(&zone, data, &expression_count);

  testing::SetupIsolateForWasmModule(i_isolate);
  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());
  auto enabled_features = WasmFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports;
  bool valid = GetWasmEngine()->SyncValidate(i_isolate, enabled_features,
                                             compile_imports, wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, valid);
  }

  CHECK(valid);
  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled_module = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, compile_imports, &thrower, wire_bytes);
  CHECK(!compiled_module.is_null());
  CHECK(!thrower.error());
  thrower.Reset();
  CHECK(!i_isolate->has_exception());

  Handle<WasmModuleObject> module_object = compiled_module.ToHandleChecked();
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, module_object, {}, {})
          .ToHandleChecked();
  CHECK_EQ(expression_count,
           module_object->native_module()->module()->num_declared_functions);

  for (size_t i = 0; i < expression_count; ++i) {
    char buffer[8];
    snprintf(buffer, sizeof buffer, "f%zu", i);
    // Execute corresponding function.
    auto function = Handle<WasmExportedFunction>::cast(
        GetExport(i_isolate, instance, buffer));
    Handle<Object> undefined = i_isolate->factory()->undefined_value();
    Handle<Object> function_result =
        Execution::Call(i_isolate, function, undefined, 0, {})
            .ToHandleChecked();
    // Get global value.
    snprintf(buffer, sizeof buffer, "g%zu", i);
    auto global =
        Handle<WasmGlobalObject>::cast(GetExport(i_isolate, instance, buffer));
    // Compare the function result with the global value.
    switch (global->type().kind()) {
      case ValueKind::kF32: {
        float global_val = global->GetF32();
        float func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsHeapNumber(*function_result));
          func_val = HeapNumber::cast(*function_result)->value();
        }
        CHECK_FLOAT_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kF64: {
        double global_val = global->GetF64();
        double func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsHeapNumber(*function_result));
          func_val = HeapNumber::cast(*function_result)->value();
        }
        CHECK_FLOAT_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kI32: {
        int32_t global_val = global->GetI32();
        int32_t func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsHeapNumber(*function_result));
          func_val = HeapNumber::cast(*function_result)->value();
        }
        CHECK_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kI64: {
        int64_t global_val = global->GetI64();
        int64_t func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsBigInt(*function_result));
          bool lossless;
          func_val = BigInt::cast(*function_result)->AsInt64(&lossless);
          CHECK(lossless);
        }
        CHECK_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kRef:
      case ValueKind::kRefNull: {
        // For reference types we can't do much for now.
        Handle<Object> global_val = global->GetRef();
        CHECK_EQ(IsUndefined(*global_val), IsUndefined(*function_result));
        CHECK_EQ(IsNullOrWasmNull(*global_val),
                 IsNullOrWasmNull(*function_result));
        break;
      }
      default:
        UNIMPLEMENTED();
    }
  }
}

}  // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzIt({data, size});
  return 0;
}

}  // namespace v8::internal::wasm::fuzzing
