// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/function-compiler.h"

#include "src/compiler/wasm-compiler.h"
#include "src/counters.h"
#include "src/macro-assembler-inl.h"
#include "src/wasm/baseline/liftoff-compiler.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

const char* GetExecutionTierAsString(ExecutionTier tier) {
  switch (tier) {
    case ExecutionTier::kBaseline:
      return "liftoff";
    case ExecutionTier::kOptimized:
      return "turbofan";
    case ExecutionTier::kInterpreter:
      return "interpreter";
  }
  UNREACHABLE();
}

}  // namespace

// static
ExecutionTier WasmCompilationUnit::GetDefaultExecutionTier(
    const WasmModule* module) {
  return FLAG_liftoff && module->origin == kWasmOrigin
             ? ExecutionTier::kBaseline
             : ExecutionTier::kOptimized;
}

WasmCompilationUnit::WasmCompilationUnit(WasmEngine* wasm_engine, int index,
                                         ExecutionTier tier)
    : wasm_engine_(wasm_engine), func_index_(index), tier_(tier) {
  if (V8_UNLIKELY(FLAG_wasm_tier_mask_for_testing) && index < 32 &&
      (FLAG_wasm_tier_mask_for_testing & (1 << index))) {
    tier = ExecutionTier::kOptimized;
  }
  SwitchTier(tier);
}

// Declared here such that {LiftoffCompilationUnit} and
// {TurbofanWasmCompilationUnit} can be opaque in the header file.
WasmCompilationUnit::~WasmCompilationUnit() = default;

void WasmCompilationUnit::ExecuteCompilation(
    CompilationEnv* env, NativeModule* native_module,
    const std::shared_ptr<WireBytesStorage>& wire_bytes_storage,
    Counters* counters, WasmFeatures* detected) {
  auto* func = &env->module->functions[func_index_];
  Vector<const uint8_t> code = wire_bytes_storage->GetCode(func->code);
  wasm::FunctionBody func_body{func->sig, func->code.offset(), code.start(),
                               code.end()};

  auto size_histogram = SELECT_WASM_COUNTER(counters, env->module->origin, wasm,
                                            function_size_bytes);
  size_histogram->AddSample(static_cast<int>(func_body.end - func_body.start));
  auto timed_histogram = SELECT_WASM_COUNTER(counters, env->module->origin,
                                             wasm_compile, function_time);
  TimedHistogramScope wasm_compile_function_time_scope(timed_histogram);

  if (FLAG_trace_wasm_compiler) {
    PrintF("Compiling wasm function %d with %s\n\n", func_index_,
           GetExecutionTierAsString(tier_));
  }

  switch (tier_) {
    case ExecutionTier::kBaseline:
      if (liftoff_unit_->ExecuteCompilation(env, native_module, func_body,
                                            counters, detected)) {
        break;
      }
      // Otherwise, fall back to turbofan.
      SwitchTier(ExecutionTier::kOptimized);
      // TODO(wasm): We could actually stop or remove the tiering unit for this
      // function to avoid compiling it twice with TurboFan.
      V8_FALLTHROUGH;
    case ExecutionTier::kOptimized:
      turbofan_unit_->ExecuteCompilation(env, native_module, func_body,
                                         counters, detected);
      break;
    case ExecutionTier::kInterpreter:
      UNREACHABLE();  // TODO(titzer): compile interpreter entry stub.
  }
}

void WasmCompilationUnit::SwitchTier(ExecutionTier new_tier) {
  // This method is being called in the constructor, where neither
  // {liftoff_unit_} nor {turbofan_unit_} are set, or to switch tier from
  // kLiftoff to kTurbofan, in which case {liftoff_unit_} is already set.
  tier_ = new_tier;
  switch (new_tier) {
    case ExecutionTier::kBaseline:
      DCHECK(!turbofan_unit_);
      DCHECK(!liftoff_unit_);
      liftoff_unit_.reset(new LiftoffCompilationUnit(this));
      return;
    case ExecutionTier::kOptimized:
      DCHECK(!turbofan_unit_);
      liftoff_unit_.reset();
      turbofan_unit_.reset(new compiler::TurbofanWasmCompilationUnit(this));
      return;
    case ExecutionTier::kInterpreter:
      UNREACHABLE();  // TODO(titzer): allow compiling interpreter entry stub.
  }
  UNREACHABLE();
}

// static
void WasmCompilationUnit::CompileWasmFunction(Isolate* isolate,
                                              NativeModule* native_module,
                                              WasmFeatures* detected,
                                              const WasmFunction* function,
                                              ExecutionTier tier) {
  ModuleWireBytes wire_bytes(native_module->wire_bytes());
  FunctionBody function_body{function->sig, function->code.offset(),
                             wire_bytes.start() + function->code.offset(),
                             wire_bytes.start() + function->code.end_offset()};

  WasmCompilationUnit unit(isolate->wasm_engine(), function->func_index, tier);
  CompilationEnv env = native_module->CreateCompilationEnv();
  unit.ExecuteCompilation(
      &env, native_module,
      native_module->compilation_state()->GetWireBytesStorage(),
      isolate->counters(), detected);
}

void WasmCompilationUnit::SetResult(WasmCode* code, Counters* counters) {
  DCHECK_NULL(result_);
  result_ = code;
  code->native_module()->PublishCode(code);

  counters->wasm_generated_code_size()->Increment(
      static_cast<int>(code->instructions().size()));
  counters->wasm_reloc_size()->Increment(
      static_cast<int>(code->reloc_info().size()));
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
