// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/fuzzer/wasm/fuzzer-common.h"

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-metrics.h"
#include "src/execution/isolate.h"
#include "src/utils/ostreams.h"
#include "src/wasm/baseline/liftoff-compiler.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "tools/wasm/mjsunit-module-disassembler-impl.h"

#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter.h"
#endif  // V8_ENABLE_DRUMBRAKE

namespace v8::internal::wasm::fuzzing {

namespace {

void CompileAllFunctionsForReferenceExecution(NativeModule* native_module,
                                              int32_t* max_steps) {
  const WasmModule* module = native_module->module();
  WasmCodeRefScope code_ref_scope;
  CompilationEnv env = CompilationEnv::ForModule(native_module);
  ModuleWireBytes wire_bytes_accessor{native_module->wire_bytes()};
  for (size_t i = module->num_imported_functions; i < module->functions.size();
       ++i) {
    auto& func = module->functions[i];
    base::Vector<const uint8_t> func_code =
        wire_bytes_accessor.GetFunctionBytes(&func);
    constexpr bool kIsShared = false;
    FunctionBody func_body(func.sig, func.code.offset(), func_code.begin(),
                           func_code.end(), kIsShared);
    auto result =
        ExecuteLiftoffCompilation(&env, func_body,
                                  LiftoffOptions{}
                                      .set_func_index(func.func_index)
                                      .set_for_debugging(kForDebugging)
                                      .set_max_steps(max_steps)
                                      // TODO(clemensb): Fully remove
                                      // nondeterminism detection.
                                      .set_detect_nondeterminism(false));
    if (!result.succeeded()) {
      FATAL(
          "Liftoff compilation failed on a valid module. Run with "
          "--trace-wasm-decoder (in a debug build) to see why.");
    }
    native_module->PublishCode(native_module->AddCompiledCode(result));
  }
}

}  // namespace

CompileTimeImports CompileTimeImportsForFuzzing() {
  CompileTimeImports result;
  result.Add(CompileTimeImport::kJsString);
  result.Add(CompileTimeImport::kTextDecoder);
  result.Add(CompileTimeImport::kTextEncoder);
  return result;
}

// Compile a baseline module. We pass a pointer to a max step counter and a
// nondeterminsm flag that are updated during execution by Liftoff.
DirectHandle<WasmModuleObject> CompileReferenceModule(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    int32_t* max_steps) {
  // Create the native module.
  std::shared_ptr<NativeModule> native_module;
  constexpr bool kNoVerifyFunctions = false;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  WasmDetectedFeatures detected_features;
  ModuleResult module_res =
      DecodeWasmModule(enabled_features, wire_bytes, kNoVerifyFunctions,
                       ModuleOrigin::kWasmOrigin, &detected_features);
  CHECK(module_res.ok());
  std::shared_ptr<WasmModule> module = std::move(module_res).value();
  CHECK_NOT_NULL(module);
  CompileTimeImports compile_imports = CompileTimeImportsForFuzzing();
  WasmError imports_error = ValidateAndSetBuiltinImports(
      module.get(), wire_bytes, compile_imports, &detected_features);
  CHECK(!imports_error.has_error());  // The module was compiled before.
  const size_t code_size_estimate =
      WasmCodeManager::EstimateNativeModuleCodeSize(module.get());
  native_module = GetWasmEngine()->NewNativeModule(
      isolate, enabled_features, detected_features,
      CompileTimeImportsForFuzzing(), module, code_size_estimate);
  native_module->SetWireBytes(base::OwnedCopyOf(wire_bytes));
  // The module is known to be valid as this point (it was compiled by the
  // caller before).
  module->set_all_functions_validated();

  // The value is -3 so that it is different than the compilation ID of actual
  // compilations, different than the sentinel value of the CompilationState
  // (-1) and the value used by native module deserialization (-2).
  const int dummy_fuzzing_compilation_id = -3;
  native_module->compilation_state()->set_compilation_id(
      dummy_fuzzing_compilation_id);
  InitializeCompilationForTesting(native_module.get());

  // Compile all functions with Liftoff.
  CompileAllFunctionsForReferenceExecution(native_module.get(), max_steps);

  // Create the module object.
  constexpr base::Vector<const char> kNoSourceUrl;
  DirectHandle<Script> script =
      GetWasmEngine()->GetOrCreateScript(isolate, native_module, kNoSourceUrl);
  TypeCanonicalizer::PrepareForCanonicalTypeId(isolate,
                                               module->MaxCanonicalTypeIndex());
  return WasmModuleObject::New(isolate, std::move(native_module), script);
}

#if V8_ENABLE_DRUMBRAKE
void ClearJsToWasmWrappersForTesting(Isolate* isolate) {
  isolate->heap()->SetJSToWasmWrappers(
      ReadOnlyRoots(isolate).empty_weak_fixed_array());
}
#endif  // V8_ENABLE_DRUMBRAKE

struct ReferenceExecutionResult {
  int result = -1;
  std::unique_ptr<const char[]> exception;
  bool should_execute_non_reference = false;
};

ReferenceExecutionResult ExecuteReferenceRun(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    int exported_main_function_index, int32_t max_executed_instructions) {
  // The reference module uses a special compilation mode of Liftoff for
  // termination and nondeterminism detected, and that would be undone by
  // flushing that code.
  FlagScope<bool> no_liftoff_code_flushing(&v8_flags.flush_liftoff_code, false);

  int32_t max_steps = max_executed_instructions;

  HandleScope handle_scope(isolate);  // Avoid leaking handles.
  Zone reference_module_zone(isolate->allocator(), "wasm reference module");
  DirectHandle<WasmModuleObject> module_ref =
      CompileReferenceModule(isolate, wire_bytes, &max_steps);
  DirectHandle<WasmInstanceObject> instance_ref;

  // Before execution, there should be no dangling nondeterminism registered on
  // the engine, no pending exception, and no termination request.
  DCHECK(!WasmEngine::had_nondeterminism());
  DCHECK(!isolate->has_exception());
  DCHECK(!isolate->stack_guard()->CheckTerminateExecution());

  // Try to instantiate the reference instance, return if it fails.
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference");
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_ref, {},
                               {})  // no imports & memory
             .ToHandle(&instance_ref)) {
      isolate->clear_exception();
      thrower.Reset();  // Ignore errors.
      return {};
    }
  }

  // Get the "main" exported function. We checked before that this exists.
  DirectHandle<WasmExportedFunction> main_function;
  CHECK(testing::GetExportedFunction(isolate, instance_ref, "main")
            .ToHandle(&main_function));

  struct OomCallbackData {
    Isolate* isolate;
    bool heap_limit_reached{false};
    size_t initial_limit{0};
  };
  OomCallbackData oom_callback_data{isolate};
  auto heap_limit_callback = [](void* raw_data, size_t current_limit,
                                size_t initial_limit) -> size_t {
    OomCallbackData* data = reinterpret_cast<OomCallbackData*>(raw_data);
    if (data->heap_limit_reached) return initial_limit;
    data->heap_limit_reached = true;
    // We can not throw an exception directly at this point, so request
    // termination on the next stack check.
    data->isolate->stack_guard()->RequestTerminateExecution();
    data->initial_limit = initial_limit;
    // Return a generously raised limit to maximize the chance to make it to the
    // next interrupt check point, where execution will terminate.
    return initial_limit * 4;
  };
  isolate->heap()->AddNearHeapLimitCallback(heap_limit_callback,
                                            &oom_callback_data);

  Tagged<WasmExportedFunctionData> func_data =
      main_function->shared()->wasm_exported_function_data();
  DCHECK_EQ(exported_main_function_index, func_data->function_index());
  const FunctionSig* sig = func_data->instance_data()
                               ->module()
                               ->functions[func_data->function_index()]
                               .sig;
  DirectHandleVector<Object> compiled_args =
      testing::MakeDefaultArguments(isolate, sig);
  std::unique_ptr<const char[]> exception;
  int32_t result_ref = testing::CallWasmFunctionForTesting(
      isolate, instance_ref, "main", base::VectorOf(compiled_args), &exception);
  bool execute = true;
  // Reached max steps, do not try to execute the test module as it might
  // never terminate.
  if (max_steps < 0) execute = false;
  // If there is nondeterminism, we cannot guarantee the behavior of the test
  // module, and in particular it may not terminate.
  if (WasmEngine::clear_nondeterminism()) execute = false;
  // Similar to max steps reached, also discard modules that need too much
  // memory.
  isolate->heap()->RemoveNearHeapLimitCallback(heap_limit_callback,
                                               oom_callback_data.initial_limit);
  if (oom_callback_data.heap_limit_reached) {
    execute = false;
    isolate->stack_guard()->ClearTerminateExecution();
  }

  if (exception) {
    if (strcmp(exception.get(),
               "RangeError: Maximum call stack size exceeded") == 0) {
      // There was a stack overflow, which may happen nondeterministically. We
      // cannot guarantee the behavior of the test module, and in particular it
      // may not terminate.
      execute = false;
    }
  }

  if (!execute) {
    // Before discarding the module, see if Turbofan runs into any DCHECKs.
    TierUpAllForTesting(isolate, instance_ref->trusted_data(isolate));
    return {};
  }

  return {result_ref, std::move(exception), true};
}

int FindExportedMainFunction(const WasmModule* module,
                             base::Vector<const uint8_t> wire_bytes) {
  constexpr base::Vector<const char> kMainName = base::StaticCharVector("main");
  for (const WasmExport& exp : module->export_table) {
    if (exp.kind != ImportExportKindCode::kExternalFunction) continue;
    base::Vector<const uint8_t> name =
        wire_bytes.SubVector(exp.name.offset(), exp.name.end_offset());
    if (base::Vector<const char>::cast(name) != kMainName) continue;
    return exp.index;
  }
  return -1;
}

int ExecuteAgainstReference(Isolate* isolate,
                            DirectHandle<WasmModuleObject> module_object,
                            int32_t max_executed_instructions
#if V8_ENABLE_DRUMBRAKE
                            ,
                            bool is_wasm_jitless
#endif  // V8_ENABLE_DRUMBRAKE
) {
  NativeModule* native_module = module_object->native_module();
  const WasmModule* module = native_module->module();
  const base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  int exported_main = FindExportedMainFunction(module, wire_bytes);
  if (exported_main < 0) return -1;

  // We do not instantiate the module if there is a start function, because a
  // start function can contain an infinite loop which we cannot handle.
  if (module->start_function_index >= 0) return -1;

  ReferenceExecutionResult ref_result = ExecuteReferenceRun(
      isolate, wire_bytes, exported_main, max_executed_instructions);
  if (!ref_result.should_execute_non_reference) return -1;

#if V8_ENABLE_DRUMBRAKE
  if (is_wasm_jitless) {
    v8::internal::v8_flags.jitless = true;
    v8::internal::v8_flags.wasm_jitless = true;

    FlagList::EnforceFlagImplications();
    v8::internal::wasm::WasmInterpreterThread::Initialize();
    ClearJsToWasmWrappersForTesting(isolate);

    // Compiled WasmCode objects should be cleared before running drumbrake.
    isolate->heap()->CollectAllGarbage(GCFlag::kNoFlags,
                                       i::GarbageCollectionReason::kTesting);

    // The module should be validated when compiled for jitless mode.
    // But, we already compiled the module without jitless for the reference
    // instance. So, we run the validation here before running drumbrake.
    auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
    WasmDetectedFeatures unused_detected_features;
    ModuleDecoderImpl decoder(
        enabled_features, module_object->native_module()->wire_bytes(),
        ModuleOrigin::kWasmOrigin, &unused_detected_features);
    if (decoder.DecodeModule(/*validate_functions=*/true).failed()) return -1;
  }
#endif  // V8_ENABLE_DRUMBRAKE

  // Instantiate a fresh instance for the actual (non-ref) execution.
  DirectHandle<WasmInstanceObject> instance;
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference (second)");
    // We instantiated before, so the second instantiation must also succeed.
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_object, {},
                               {})  // no imports & memory
             .ToHandle(&instance)) {
      DCHECK(thrower.error());
      // The only reason to fail the second instantiation should be OOM.
      if (strstr(thrower.error_msg(), "Out of memory")) {
        // The initial memory size might be too large for instantiation
        // (especially on 32 bit systems), therefore do not treat it as a fuzzer
        // failure.
        return -1;
      }
      FATAL("Second instantiation failed unexpectedly: %s",
            thrower.error_msg());
    }
    DCHECK(!thrower.error());
  }

  std::unique_ptr<const char[]> exception;
  const FunctionSig* sig = module->functions[exported_main].sig;
  DirectHandleVector<Object> compiled_args =
      testing::MakeDefaultArguments(isolate, sig);
  int32_t result = testing::CallWasmFunctionForTesting(
      isolate, instance, "main", base::VectorOf(compiled_args), &exception);

  // Also the second run can hit nondeterminism which was not hit before (when
  // growing memory). In that case, do not compare results.
  // TODO(384781857): Due to nondeterminism, the second run could even not
  // terminate. If this happens often enough we should do something about this.
  if (WasmEngine::clear_nondeterminism()) return -1;

  if ((ref_result.exception != nullptr) != (exception != nullptr)) {
    FATAL("Exception mismatch! Expected: <%s>; got: <%s>",
          ref_result.exception ? ref_result.exception.get() : "<no exception>",
          exception ? exception.get() : "<no exception>");
  }

  if (!exception) {
    CHECK_EQ(ref_result.result, result);
  }

  return 0;
}

void GenerateTestCase(Isolate* isolate, ModuleWireBytes wire_bytes,
                      bool compiles) {
  StdoutStream os;
  GenerateTestCase(os, isolate, wire_bytes, compiles, false, "");
  os.flush();
}

void GenerateTestCase(StdoutStream& os, Isolate* isolate,
                      ModuleWireBytes wire_bytes, bool compiles,
                      bool emit_call_main, std::string_view extra_flags) {
  // Libfuzzer sometimes runs a test twice (for detecting memory leaks), and in
  // this case we do not want multiple outputs by this function.
  // Similarly if we explicitly execute the same test multiple times (via
  // `-runs=N`).
  static std::atomic<bool> did_output_before{false};
  if (did_output_before.exchange(true)) return;

  constexpr bool kVerifyFunctions = false;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  WasmDetectedFeatures unused_detected_features;
  ModuleResult module_res = DecodeWasmModule(
      enabled_features, wire_bytes.module_bytes(), kVerifyFunctions,
      ModuleOrigin::kWasmOrigin, &unused_detected_features);
  CHECK_WITH_MSG(module_res.ok(), module_res.error().message().c_str());
  WasmModule* module = module_res.value().get();
  CHECK_NOT_NULL(module);

  AccountingAllocator allocator;
  Zone zone(&allocator, "constant expression zone");

  MultiLineStringBuilder out;
  NamesProvider names(module, wire_bytes.module_bytes());
  MjsunitModuleDis disassembler(out, module, &names, wire_bytes, &allocator,
                                !compiles);
  disassembler.PrintModule(extra_flags, emit_call_main);
  const bool offsets = false;  // Not supported by MjsunitModuleDis.
  out.WriteTo(os, offsets);
}

namespace {
static uint8_t kDummyModuleWireBytes[]{
    WASM_MODULE_HEADER,
    SECTION(Type, ENTRY_COUNT(2),
            // recgroup of 2 types
            WASM_REC_GROUP(ENTRY_COUNT(2),
                           // (array (field (mut f32)))
                           WASM_ARRAY_DEF(kF32Code, true),
                           // (struct (field i64) (field externref))
                           WASM_NONFINAL(WASM_STRUCT_DEF(
                               FIELD_COUNT(2), STRUCT_FIELD(kI64Code, false),
                               STRUCT_FIELD(kExternRefCode, false)))),
            // function type (void -> i32)
            SIG_ENTRY_x(kI32Code))};

}  // namespace

void AddDummyTypesToTypeCanonicalizer(Isolate* isolate) {
  const size_t type_count = GetTypeCanonicalizer()->GetCurrentNumberOfTypes();
  const bool is_valid = GetWasmEngine()->SyncValidate(
      isolate, WasmEnabledFeatures(), CompileTimeImportsForFuzzing(),
      base::VectorOf(kDummyModuleWireBytes));
  CHECK(is_valid);
  // As the types are reset on each run by the fuzzer, the validation should
  // have added new types to the TypeCanonicalizer.
  CHECK_GT(GetTypeCanonicalizer()->GetCurrentNumberOfTypes(), type_count);
}

void EnableExperimentalWasmFeatures(v8::Isolate* isolate) {
  struct EnableExperimentalWasmFeatures {
    explicit EnableExperimentalWasmFeatures(v8::Isolate* isolate)
        : isolate(isolate) {
      // Enable all staged features.
#define ENABLE_PRE_STAGED_AND_STAGED_FEATURES(feat, ...) \
  v8_flags.experimental_wasm_##feat = true;
      FOREACH_WASM_PRE_STAGING_FEATURE_FLAG(
          ENABLE_PRE_STAGED_AND_STAGED_FEATURES)
      FOREACH_WASM_STAGING_FEATURE_FLAG(ENABLE_PRE_STAGED_AND_STAGED_FEATURES)
#undef ENABLE_PRE_STAGED_AND_STAGED_FEATURES

      // Enable non-staged experimental features or other experimental flags
      // that we also want to fuzz, e.g., new optimizations.
      // Note: If you add a Wasm feature here, you will also have to add the
      // respective flag(s) to the mjsunit/wasm/generate-random-module.js test,
      // otherwise that fails on an unsupported feature.
      // You may also want to add the flag(s) to the JS file header in
      // `PrintModule()` of `mjsunit-module-disassembler-impl.h`, to make bugs
      // easier to reproduce with generated mjsunit test cases.

      // The "pure Wasm" part of this proposal is considered ready for
      // fuzzing, the JS-related part (prototypes etc) not yet.
      v8_flags.experimental_wasm_custom_descriptors = true;

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
      // Fuzz revectorization, which is otherwise still considered experimental.
      v8_flags.experimental_wasm_revectorize = true;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

      // Enforce implications from enabling features.
      FlagList::EnforceFlagImplications();

      // Last, install any conditional features. Implications are handled
      // implicitly.
      isolate->InstallConditionalFeatures(isolate->GetCurrentContext());
    }

    const v8::Isolate* const isolate;
  };

  // Call the constructor exactly once (per process!). The compiler will
  // properly synchronize this.
  static EnableExperimentalWasmFeatures one_time_enable_experimental_features(
      isolate);
  // Ensure that within the same process we always pass the same isolate. You
  // would get surprising results otherwise.
  CHECK_EQ(one_time_enable_experimental_features.isolate, isolate);
}

void ResetTypeCanonicalizer(v8::Isolate* isolate) {
  v8::internal::Isolate* i_isolate =
      reinterpret_cast<v8::internal::Isolate*>(isolate);

  // Make sure that there are no NativeModules left referencing the canonical
  // types. Collecting NativeModules can require two rounds of GC.
  for (int i = 0; i < 2 && GetWasmEngine()->NativeModuleCount() != 0; i++) {
    // We need to invoke GC without stack, otherwise the native module may
    // survive.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        i_isolate->heap());
    isolate->RequestGarbageCollectionForTesting(
        v8::Isolate::kFullGarbageCollection);
  }
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  TypeCanonicalizer::ClearWasmCanonicalTypesForTesting(i_isolate);
  AddDummyTypesToTypeCanonicalizer(i_isolate);
}

int WasmExecutionFuzzer::FuzzWasmModule(base::Vector<const uint8_t> data,
                                        bool require_valid) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  // Strictly enforce the input size limit. Note that setting "max_len" on the
  // fuzzer target is not enough, since different fuzzers are used and not all
  // respect that limit.
  if (data.size() > max_input_size()) return -1;

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // We explicitly enable staged WebAssembly features here to increase fuzzer
  // coverage. For libfuzzer fuzzers it is not possible that the fuzzer enables
  // the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  // Allow mixed old and new EH instructions in the same module for fuzzing, to
  // help us test the interaction between the two EH proposals without requiring
  // multiple modules.
  v8_flags.wasm_allow_mixed_eh_for_testing = true;

  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  // The first byte specifies some internal configuration, like which function
  // is compiled with which compiler, and other flags.
  uint8_t configuration_byte = data.empty() ? 0 : data[0];
  if (!data.empty()) data += 1;

  // Derive the compiler configuration for the first four functions from the
  // configuration byte, to choose for each function between:
  // 0: TurboFan
  // 1: Liftoff
  // 2: Liftoff for debugging
  uint8_t tier_mask = 0;
  uint8_t debug_mask = 0;
  for (int i = 0; i < 4; ++i, configuration_byte /= 3) {
    int compiler_config = configuration_byte % 3;
    tier_mask |= (compiler_config == 0) << i;
    debug_mask |= (compiler_config == 2) << i;
  }
  // The purpose of setting the tier mask (which affects the initial
  // compilation of each function) is to deterministically test a combination
  // of Liftoff and Turbofan.
  FlagScope<int> tier_mask_scope(&v8_flags.wasm_tier_mask_for_testing,
                                 tier_mask);
  FlagScope<int> debug_mask_scope(&v8_flags.wasm_debug_mask_for_testing,
                                  debug_mask);

  ZoneBuffer buffer(&zone);
  if (!GenerateModule(i_isolate, &zone, data, &buffer)) {
    return -1;
  }

  return SyncCompileAndExecuteAgainstReference(isolate, base::VectorOf(buffer),
                                               require_valid);
}

int SyncCompileAndExecuteAgainstReference(
    v8::Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    bool require_valid) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  ResetTypeCanonicalizer(isolate);

  // Clear any exceptions from a prior run.
  if (i_isolate->has_exception()) i_isolate->clear_exception();

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);

  bool valid = GetWasmEngine()->SyncValidate(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, ModuleWireBytes{wire_bytes}, valid);
  }

  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  // We want to keep dynamic tiering enabled because that changes the code
  // Liftoff generates as well as optimizing compilers' behavior (especially
  // around inlining). We switch it to synchronous mode to avoid the
  // nondeterminism of background jobs finishing at random times.
  FlagScope<bool> sync_tier_up(&v8_flags.wasm_sync_tier_up, true);
  // Reference runs use extra compile settings (like non-determinism detection),
  // which could be replaced by new liftoff code without this option.
  FlagScope<bool> no_liftoff_code_flushing(&v8_flags.flush_liftoff_code, false);

  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeDirectHandle<WasmModuleObject> compiled_module =
      GetWasmEngine()->SyncCompile(i_isolate, enabled_features,
                                   CompileTimeImportsForFuzzing(), &thrower,
                                   base::OwnedCopyOf(wire_bytes));
  CHECK_EQ(valid, !compiled_module.is_null());
  CHECK_EQ(!valid, thrower.error());
  if (require_valid && !valid) {
    FATAL("Generated module should validate, but got: %s", thrower.error_msg());
  }
  thrower.Reset();

  // Do not execute invalid modules, and return `-1` to avoid adding them to the
  // corpus. Even though invalid modules are also somewhat interesting to fuzz,
  // we will get them often enough via mutations, so we do not add them to the
  // corpus.
  if (!valid) return -1;

  return ExecuteAgainstReference(i_isolate, compiled_module.ToHandleChecked(),
                                 kDefaultMaxFuzzerExecutedInstructions);
}

}  // namespace v8::internal::wasm::fuzzing
