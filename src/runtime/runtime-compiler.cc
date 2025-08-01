// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/asmjs/asm-js.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/runtime/runtime-utils.h"

namespace v8::internal {

namespace {
void LogExecution(Isolate* isolate, DirectHandle<JSFunction> function) {
  DCHECK(v8_flags.log_function_events);
  if (!function->has_feedback_vector()) return;
#ifdef V8_ENABLE_LEAPTIERING
  DCHECK(function->IsLoggingRequested(isolate));
  IsolateGroup::current()->js_dispatch_table()->ResetTieringRequest(
      function->dispatch_handle());
#else
  if (!function->feedback_vector()->log_next_execution()) return;
#endif
  DirectHandle<SharedFunctionInfo> sfi(function->shared(), isolate);
  DirectHandle<String> name = SharedFunctionInfo::DebugName(isolate, sfi);
  DisallowGarbageCollection no_gc;
  Tagged<SharedFunctionInfo> raw_sfi = *sfi;
  std::string event_name = "first-execution";
  CodeKind kind = function->abstract_code(isolate)->kind(isolate);
  // Not adding "-interpreter" for tooling backwards compatibility.
  if (kind != CodeKind::INTERPRETED_FUNCTION) {
    event_name += "-";
    event_name += CodeKindToString(kind);
  }
  LOG(isolate, FunctionEvent(
                   event_name.c_str(), Cast<Script>(raw_sfi->script())->id(), 0,
                   raw_sfi->StartPosition(), raw_sfi->EndPosition(), *name));
#ifndef V8_ENABLE_LEAPTIERING
  function->feedback_vector()->set_log_next_execution(false);
#endif  // !V8_ENABLE_LEAPTIERING
}
}  // namespace

RUNTIME_FUNCTION(Runtime_CompileLazy) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  StackLimitCheck check(isolate);
  if (V8_UNLIKELY(
          check.JsHasOverflowed(kStackSpaceRequiredForCompilation * KB))) {
    return isolate->StackOverflow();
  }

  DirectHandle<SharedFunctionInfo> sfi(function->shared(), isolate);

  DCHECK(!function->is_compiled(isolate));
#ifdef DEBUG
  if (v8_flags.trace_lazy && sfi->is_compiled()) {
    PrintF("[unoptimized: %s]\n", function->DebugNameCStr().get());
  }
#endif
  IsCompiledScope is_compiled_scope;
  if (!Compiler::Compile(isolate, function, Compiler::KEEP_EXCEPTION,
                         &is_compiled_scope)) {
    return ReadOnlyRoots(isolate).exception();
  }
#ifndef V8_ENABLE_LEAPTIERING
  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    LogExecution(isolate, function);
  }
#endif  // !V8_ENABLE_LEAPTIERING
  DCHECK(function->is_compiled(isolate));
  return function->code(isolate);
}

RUNTIME_FUNCTION(Runtime_InstallBaselineCode) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  DirectHandle<SharedFunctionInfo> sfi(function->shared(), isolate);
  DCHECK(sfi->HasBaselineCode());
  {
    if (!V8_ENABLE_LEAPTIERING_BOOL || !function->has_feedback_vector()) {
      IsCompiledScope is_compiled_scope(*sfi, isolate);
      IsBaselineCompiledScope is_baseline_compiled_scope(*sfi, isolate);
      DCHECK(is_baseline_compiled_scope.is_compiled());
      DCHECK(!function->HasAvailableOptimizedCode(isolate));
      DCHECK(!function->has_feedback_vector());
      JSFunction::CreateAndAttachFeedbackVector(isolate, function,
                                                &is_compiled_scope);
    }
    DisallowGarbageCollection no_gc;
    Tagged<Code> baseline_code = sfi->baseline_code(kAcquireLoad);
    function->UpdateCodeKeepTieringRequests(isolate, baseline_code);
#ifdef V8_ENABLE_LEAPTIERING
    return baseline_code;
  }
#else
    if V8_LIKELY (!v8_flags.log_function_events) return baseline_code;
  }
  DCHECK(v8_flags.log_function_events);
  LogExecution(isolate, function);
  // LogExecution might allocate, reload the baseline code
  return sfi->baseline_code(kAcquireLoad);
#endif  // V8_ENABLE_LEAPTIERING
}

RUNTIME_FUNCTION(Runtime_InstallSFICode) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  {
    DisallowGarbageCollection no_gc;
    Tagged<SharedFunctionInfo> sfi = function->shared();
    DCHECK(sfi->is_compiled());
    Tagged<Code> sfi_code = sfi->GetCode(isolate);
    if (V8_LIKELY(sfi_code->kind() != CodeKind::BASELINE ||
                  function->has_feedback_vector())) {
      function->UpdateCode(isolate, sfi_code);
      return sfi_code;
    }
  }
  // This could be the first time we are installing baseline code so we need to
  // ensure that a feedback vectors is allocated.
  IsCompiledScope is_compiled_scope(function->shared(), isolate);
  DCHECK(!function->HasAvailableOptimizedCode(isolate));
  DCHECK(!function->has_feedback_vector());
  JSFunction::CreateAndAttachFeedbackVector(isolate, function,
                                            &is_compiled_scope);
  Tagged<Code> sfi_code = function->shared()->GetCode(isolate);
  function->UpdateCode(isolate, sfi_code);
  return sfi_code;
}

#ifdef V8_ENABLE_LEAPTIERING

namespace {

void CompileOptimized(DirectHandle<JSFunction> function, ConcurrencyMode mode,
                      CodeKind target_kind, Isolate* isolate) {
  // Ensure that the tiering request is reset even if compilation fails.
  function->ResetTieringRequests();

  // As a pre- and post-condition of CompileOptimized, the function *must* be
  // compiled, i.e. the installed InstructionStream object must not be
  // CompileLazy.
  IsCompiledScope is_compiled_scope(function->shared(), isolate);
  if (V8_UNLIKELY(!is_compiled_scope.is_compiled())) {
    // This happens if the code is flushed while we still have an optimization
    // request pending (or if manually an optimization is requested on an
    // uncompiled function).
    // Instead of calling into Compiler::Compile and having to do exception
    // handling here, we reset and return and thus tail-call into CompileLazy.
    function->ResetIfCodeFlushed(isolate);
    return;
  }

  if (mode == ConcurrencyMode::kConcurrent) {
    // No need to start another compile job.
    if (function->tiering_in_progress() ||
        function->GetActiveTier(isolate) >= target_kind) {
      static_assert(kTieringStateInProgressBlocksTierup);
      function->SetInterruptBudget(isolate, BudgetModification::kRaise);
      return;
    }
  }

  // Concurrent optimization runs on another thread, thus no additional gap.
  const int gap =
      IsConcurrent(mode) ? 0 : kStackSpaceRequiredForCompilation * KB;
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed(gap)) return;

  Compiler::CompileOptimized(isolate, function, mode, target_kind);

  DCHECK(function->is_compiled(isolate));
}

}  // namespace

RUNTIME_FUNCTION(Runtime_StartMaglevOptimizeJob) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  DCHECK(function->IsOptimizationRequested(isolate));
  CompileOptimized(function, ConcurrencyMode::kConcurrent, CodeKind::MAGLEV,
                   isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_StartTurbofanOptimizeJob) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  DCHECK(function->IsOptimizationRequested(isolate));
  CompileOptimized(function, ConcurrencyMode::kConcurrent,
                   CodeKind::TURBOFAN_JS, isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_OptimizeMaglevEager) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  DCHECK(function->IsOptimizationRequested(isolate));
  CompileOptimized(function, ConcurrencyMode::kSynchronous, CodeKind::MAGLEV,
                   isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_OptimizeTurbofanEager) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  DCHECK(function->IsOptimizationRequested(isolate));
  CompileOptimized(function, ConcurrencyMode::kSynchronous,
                   CodeKind::TURBOFAN_JS, isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_MarkLazyDeoptimized) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  bool reoptimize = (*args.at<Smi>(1)).value();

  IsCompiledScope is_compiled_scope(function->shared(), isolate);
  if (!is_compiled_scope.is_compiled()) {
    StackLimitCheck check(isolate);
    if (V8_UNLIKELY(
            check.JsHasOverflowed(kStackSpaceRequiredForCompilation * KB))) {
      return isolate->StackOverflow();
    }
    if (!Compiler::Compile(isolate, function, Compiler::KEEP_EXCEPTION,
                           &is_compiled_scope)) {
      return ReadOnlyRoots(isolate).exception();
    }
    // In case this code was flushed we should not re-optimize it too quickly.
    reoptimize = false;
  }

  function->ResetTieringRequests();
  if (reoptimize) {
    // Set the budget such that we have one invocation which allows us to detect
    // if any ICs need updating before re-optimization.
    function->raw_feedback_cell()->set_interrupt_budget(1);
  } else {
    function->SetInterruptBudget(isolate, BudgetModification::kRaise,
                                 CodeKind::INTERPRETED_FUNCTION);
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

#else

RUNTIME_FUNCTION(Runtime_CompileOptimized) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSFunction> function = args.at<JSFunction>(0);

  CodeKind target_kind;
  ConcurrencyMode mode;
  DCHECK(function->has_feedback_vector());
  switch (function->tiering_state()) {
    case TieringState::kRequestMaglev_Synchronous:
      target_kind = CodeKind::MAGLEV;
      mode = ConcurrencyMode::kSynchronous;
      break;
    case TieringState::kRequestMaglev_Concurrent:
      target_kind = CodeKind::MAGLEV;
      mode = ConcurrencyMode::kConcurrent;
      break;
    case TieringState::kRequestTurbofan_Synchronous:
      target_kind = CodeKind::TURBOFAN_JS;
      mode = ConcurrencyMode::kSynchronous;
      break;
    case TieringState::kRequestTurbofan_Concurrent:
      target_kind = CodeKind::TURBOFAN_JS;
      mode = ConcurrencyMode::kConcurrent;
      break;
    case TieringState::kNone:
    case TieringState::kInProgress:
      UNREACHABLE();
  }

  // As a pre- and post-condition of CompileOptimized, the function *must* be
  // compiled, i.e. the installed InstructionStream object must not be
  // CompileLazy.
  IsCompiledScope is_compiled_scope(function->shared(), isolate);
  DCHECK(is_compiled_scope.is_compiled());

  StackLimitCheck check(isolate);
  // Concurrent optimization runs on another thread, thus no additional gap.
  const int gap =
      IsConcurrent(mode) ? 0 : kStackSpaceRequiredForCompilation * KB;
  if (check.JsHasOverflowed(gap)) return isolate->StackOverflow();

  Compiler::CompileOptimized(isolate, function, mode, target_kind);

  DCHECK(function->is_compiled(isolate));
  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    LogExecution(isolate, function);
  }
  return function->code(isolate);
}

RUNTIME_FUNCTION(Runtime_HealOptimizedCodeSlot) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);

  DCHECK(function->shared()->is_compiled());

  function->feedback_vector()->EvictOptimizedCodeMarkedForDeoptimization(
      isolate, function->shared(), "Runtime_HealOptimizedCodeSlot");
  return function->code(isolate);
}

#endif  // !V8_ENABLE_LEAPTIERING

RUNTIME_FUNCTION(Runtime_FunctionLogNextExecution) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> js_function = args.at<JSFunction>(0);
  DCHECK(v8_flags.log_function_events);
  LogExecution(isolate, js_function);
  return js_function->code(isolate);
}

// The enum values need to match "AsmJsInstantiateResult" in
// tools/metrics/histograms/enums.xml.
enum AsmJsInstantiateResult {
  kAsmJsInstantiateSuccess = 0,
  kAsmJsInstantiateFail = 1,
};

RUNTIME_FUNCTION(Runtime_InstantiateAsmJs) {
  HandleScope scope(isolate);
  DCHECK_EQ(args.length(), 4);
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);

  DirectHandle<JSReceiver> stdlib;
  if (IsJSReceiver(args[1])) {
    stdlib = args.at<JSReceiver>(1);
  }
  DirectHandle<JSReceiver> foreign;
  if (IsJSReceiver(args[2])) {
    foreign = args.at<JSReceiver>(2);
  }
  DirectHandle<JSArrayBuffer> memory;
  if (IsJSArrayBuffer(args[3])) {
    memory = args.at<JSArrayBuffer>(3);
  }
  DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate);
#if V8_ENABLE_WEBASSEMBLY
  if (shared->HasAsmWasmData()) {
    DirectHandle<AsmWasmData> data(shared->asm_wasm_data(), isolate);
    MaybeDirectHandle<Object> result = AsmJs::InstantiateAsmWasm(
        isolate, shared, data, stdlib, foreign, memory);
    if (!result.is_null()) {
      isolate->counters()->asmjs_instantiate_result()->AddSample(
          kAsmJsInstantiateSuccess);
      return *result.ToHandleChecked();
    }
    if (isolate->has_exception()) {
      // If instantiation fails, we do not propagate the exception but instead
      // fall back to JS execution. The only exception (to that rule) is the
      // termination exception.
      DCHECK(isolate->is_execution_terminating());
      return ReadOnlyRoots{isolate}.exception();
    }
    isolate->counters()->asmjs_instantiate_result()->AddSample(
        kAsmJsInstantiateFail);

    // Remove wasm data, mark as broken for asm->wasm, replace AsmWasmData on
    // the SFI with UncompiledData and set entrypoint to CompileLazy builtin,
    // and return a smi 0 to indicate failure.
    SharedFunctionInfo::DiscardCompiled(isolate, shared);
  }
  shared->set_is_asm_wasm_broken(true);
#endif
  DCHECK_EQ(function->code(isolate), *BUILTIN_CODE(isolate, InstantiateAsmJs));
  function->UpdateCode(isolate, *BUILTIN_CODE(isolate, CompileLazy));
  DCHECK(!isolate->has_exception());
  return Smi::zero();
}

namespace {

bool TryGetOptimizedOsrCode(Isolate* isolate, Tagged<FeedbackVector> vector,
                            const interpreter::BytecodeArrayIterator& it,
                            Tagged<Code>* code_out) {
  std::optional<Tagged<Code>> maybe_code =
      vector->GetOptimizedOsrCode(isolate, {}, it.GetSlotOperand(2));
  if (maybe_code.has_value()) {
    *code_out = maybe_code.value();
    return true;
  }
  return false;
}

// Deoptimize all osr'd loops which contain a particular deopt exit.
// We only deoptimize OSR code if the exit is inside the actual loop. The OSR
// code might still be affected (if the exit is after the loop, or in the case
// of turbofan if the exit is before the loop, but reachable through an outer
// loop). Regardless in both of these two cases, the OSR code is still usable to
// quickly get out of the loop itself.
bool DeoptAllOsrLoopsContainingDeoptExit(Isolate* isolate,
                                         Tagged<JSFunction> function,
                                         BytecodeOffset deopt_exit_offset) {
  DisallowGarbageCollection no_gc;
  DCHECK(!deopt_exit_offset.IsNone());

  if (!v8_flags.use_ic ||
      !function->feedback_vector()->maybe_has_optimized_osr_code()) {
    return false;
  }
  Handle<BytecodeArray> bytecode_array(
      function->shared()->GetBytecodeArray(isolate), isolate);
  DCHECK(interpreter::BytecodeArrayIterator::IsValidOffset(
      bytecode_array, deopt_exit_offset.ToInt()));

  Tagged<FeedbackVector> vector = function->feedback_vector();
  Tagged<Code> code;

  bool any_marked = false;
  bool has_maglev_code = false;
  bool has_turbofan_code = false;

  interpreter::BytecodeArrayIterator it(bytecode_array);
  for (; !it.done(); it.Advance()) {
    // We're only interested in loop ranges.
    if (it.current_bytecode() != interpreter::Bytecode::kJumpLoop) continue;
    bool has_osr_code = TryGetOptimizedOsrCode(isolate, vector, it, &code);
    if (has_osr_code) {
      if (base::IsInRange(deopt_exit_offset.ToInt(), it.GetJumpTargetOffset(),
                          it.current_offset())) {
        code->SetMarkedForDeoptimization(isolate,
                                         LazyDeoptimizeReason::kEagerDeopt);
        any_marked = true;
      } else {
        has_turbofan_code |= code->is_turbofanned();
        has_maglev_code |= code->is_maglevved();
      }
    }
  }
  Tagged<FeedbackVector> fbv = function->feedback_vector();
  if (!has_maglev_code && fbv->maybe_has_maglev_osr_code()) {
    fbv->set_maybe_has_optimized_osr_code(false, CodeKind::MAGLEV);
  }
  if (!has_turbofan_code && fbv->maybe_has_turbofan_osr_code()) {
    fbv->set_maybe_has_optimized_osr_code(false, CodeKind::TURBOFAN_JS);
  }
  return any_marked;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_NotifyDeoptimized) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  Deoptimizer* deoptimizer = Deoptimizer::Grab(isolate);
  DCHECK(CodeKindCanDeoptimize(deoptimizer->compiled_code()->kind()));
  DCHECK(AllowGarbageCollection::IsAllowed());
  DCHECK(isolate->context().is_null());

  TimerEventScope<TimerEventDeoptimizeCode> timer(isolate);
  TRACE_EVENT0("v8", "V8.DeoptimizeCode");
  DirectHandle<JSFunction> function = deoptimizer->function();
  // For OSR the optimized code isn't installed on the function, so get the
  // code object from deoptimizer.
  DirectHandle<Code> optimized_code = deoptimizer->compiled_code();
  const DeoptimizeKind deopt_kind = deoptimizer->deopt_kind();
  const DeoptimizeReason deopt_reason =
      deoptimizer->GetDeoptInfo().deopt_reason;

  // TODO(turbofan): We currently need the native context to materialize
  // the arguments object, but only to get to its map.
  isolate->set_context(deoptimizer->function()->native_context());

  // Make sure to materialize objects before causing any allocation.
  deoptimizer->MaterializeHeapObjects();
  deoptimizer->ProcessDeoptReason(deopt_reason);
  const BytecodeOffset deopt_exit_offset =
      deoptimizer->bytecode_offset_in_outermost_frame();
  delete deoptimizer;

  // Ensure the context register is updated for materialized objects.
  JavaScriptStackFrameIterator top_it(isolate);
  JavaScriptFrame* top_frame = top_it.frame();
  isolate->set_context(Cast<Context>(top_frame->context()));

  // Lazy deopts don't invalidate the underlying optimized code since the code
  // object itself is still valid (as far as we know); the called function
  // caused the deopt, not the function we're currently looking at.
  if (deopt_kind == DeoptimizeKind::kLazy) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  // Some eager deopts also don't invalidate InstructionStream (e.g. when
  // preparing for OSR from Maglev to Turbofan).
  if (IsDeoptimizationWithoutCodeInvalidation(deopt_reason)) {
    if (deopt_reason == DeoptimizeReason::kPrepareForOnStackReplacement &&
        function->ActiveTierIsMaglev(isolate)) {
      isolate->tiering_manager()->MarkForTurboFanOptimization(*function);
    }
    return ReadOnlyRoots(isolate).undefined_value();
  }

  DCHECK_NE(deopt_reason, DeoptimizeReason::kOSREarlyExit);
  DCHECK_NE(deopt_reason, DeoptimizeReason::kPrepareForOnStackReplacement);

  // Non-OSR'd code is deoptimized unconditionally. If the deoptimization occurs
  // inside the outermost loop containing a loop that can trigger OSR
  // compilation, we remove the OSR code, it will avoid hit the out of date OSR
  // code and soon later deoptimization.
  //
  // For OSR'd code, we keep the optimized code around if deoptimization occurs
  // outside the outermost loop containing the loop that triggered OSR
  // compilation. The reasoning is that OSR is intended to speed up the
  // long-running loop; so if the deoptimization occurs outside this loop it is
  // still worth jumping to the OSR'd code on the next run. The reduced cost of
  // the loop should pay for the deoptimization costs.

  const BytecodeOffset osr_offset = optimized_code->osr_offset();
  bool any_marked = false;

  if (osr_offset.IsNone() || Deoptimizer::DeoptExitIsInsideOsrLoop(
                                 isolate, *function, deopt_exit_offset,
                                 osr_offset, optimized_code->kind())) {
    function->ResetTieringRequests();
    if (!optimized_code->marked_for_deoptimization()) {
      optimized_code->SetMarkedForDeoptimization(
          isolate, LazyDeoptimizeReason::kEagerDeopt);
      any_marked = true;
    }
  }

  if (osr_offset.IsNone()) {
    if (DeoptAllOsrLoopsContainingDeoptExit(isolate, *function,
                                            deopt_exit_offset)) {
      any_marked = true;
    }
  }

  if (any_marked) {
    Deoptimizer::DeoptimizeMarkedCode(isolate);
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_ObserveNode) {
  // The %ObserveNode intrinsic only tracks the changes to an observed node in
  // code compiled by TurboFan.
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<Object> obj = args.at(0);
  return *obj;
}

RUNTIME_FUNCTION(Runtime_VerifyType) {
  // %VerifyType has no effect in the interpreter.
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<Object> obj = args.at(0);
  return *obj;
}

RUNTIME_FUNCTION(Runtime_CheckTurboshaftTypeOf) {
  // %CheckTurboshaftTypeOf has no effect in the interpreter.
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> obj = args.at(0);
  return *obj;
}

namespace {

void GetOsrOffsetAndFunctionForOSR(Isolate* isolate, BytecodeOffset* osr_offset,
                                   Handle<JSFunction>* function) {
  DCHECK(osr_offset->IsNone());
  DCHECK(function->is_null());

  // Determine the frame that triggered the OSR request.
  JavaScriptStackFrameIterator it(isolate);
  UnoptimizedJSFrame* frame = UnoptimizedJSFrame::cast(it.frame());
  DCHECK_IMPLIES(frame->is_interpreted(),
                 frame->LookupCode()->is_interpreter_trampoline_builtin());
  DCHECK_IMPLIES(frame->is_baseline(),
                 frame->LookupCode()->kind() == CodeKind::BASELINE);

  *osr_offset = BytecodeOffset(frame->GetBytecodeOffset());
  *function = handle(frame->function(), isolate);

  DCHECK(!osr_offset->IsNone());
  DCHECK((*function)->shared()->HasBytecodeArray());
}

Tagged<Object> CompileOptimizedOSR(Isolate* isolate,
                                   DirectHandle<JSFunction> function,
                                   CodeKind min_opt_level,
                                   BytecodeOffset osr_offset) {
  ConcurrencyMode mode =
      V8_LIKELY(isolate->concurrent_recompilation_enabled() &&
                v8_flags.concurrent_osr)
          ? ConcurrencyMode::kConcurrent
          : ConcurrencyMode::kSynchronous;

  if (V8_UNLIKELY(isolate->EfficiencyModeEnabled() &&
                  min_opt_level == CodeKind::MAGLEV)) {
    mode = ConcurrencyMode::kSynchronous;
  }

  DirectHandle<Code> result;
  if (!Compiler::CompileOptimizedOSR(
           isolate, function, osr_offset, mode,
           (maglev::IsMaglevOsrEnabled() && min_opt_level == CodeKind::MAGLEV)
               ? CodeKind::MAGLEV
               : CodeKind::TURBOFAN_JS)
           .ToHandle(&result) ||
      result->marked_for_deoptimization()) {
    // An empty result can mean one of two things:
    // 1) we've started a concurrent compilation job - everything is fine.
    // 2) synchronous compilation failed for some reason.

#ifndef V8_ENABLE_LEAPTIERING
    if (!function->HasAttachedOptimizedCode(isolate)) {
      function->UpdateCode(isolate, function->shared()->GetCode(isolate));
    }
#endif  // V8_ENABLE_LEAPTIERING

    return Smi::zero();
  }

  DCHECK(!result.is_null());
  DCHECK(result->is_turbofanned() || result->is_maglevved());
  DCHECK(CodeKindIsOptimizedJSFunction(result->kind()));

#ifdef DEBUG
  Tagged<DeoptimizationData> data =
      Cast<DeoptimizationData>(result->deoptimization_data());
  DCHECK_EQ(BytecodeOffset(data->OsrBytecodeOffset().value()), osr_offset);
  DCHECK_GE(data->OsrPcOffset().value(), 0);
#endif  // DEBUG

  // First execution logging happens in LogOrTraceOptimizedOSREntry
  return *result;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_CompileOptimizedOSR) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(0, args.length());
  DCHECK(v8_flags.use_osr);

  BytecodeOffset osr_offset = BytecodeOffset::None();
  Handle<JSFunction> function;
  GetOsrOffsetAndFunctionForOSR(isolate, &osr_offset, &function);

  return CompileOptimizedOSR(isolate, function, CodeKind::MAGLEV, osr_offset);
}

namespace {

Tagged<Object> CompileOptimizedOSRFromMaglev(Isolate* isolate,
                                             DirectHandle<JSFunction> function,
                                             BytecodeOffset osr_offset) {
  // This path is only relevant for tests (all production configurations enable
  // concurrent OSR). It's quite subtle, if interested read on:
  if (V8_UNLIKELY(!isolate->concurrent_recompilation_enabled() ||
                  !v8_flags.concurrent_osr)) {
    // - Synchronous Turbofan compilation may trigger lazy deoptimization (e.g.
    //   through compilation dependency finalization actions).
    // - Maglev (currently) disallows marking an opcode as both can_lazy_deopt
    //   and can_eager_deopt.
    // - Maglev's JumpLoop opcode (the logical caller of this runtime function)
    //   is marked as can_eager_deopt since OSR'ing to Turbofan involves
    //   deoptimizing to Ignition under the hood.
    // - Thus this runtime function *must not* trigger a lazy deopt, and
    //   therefore cannot trigger synchronous Turbofan compilation (see above).
    //
    // We solve this synchronous OSR case by bailing out early to Ignition, and
    // letting it handle OSR. How do we trigger the early bailout? Returning
    // any non-null InstructionStream from this function triggers the deopt in
    // JumpLoop.
    if (v8_flags.trace_osr) {
      CodeTracer::Scope scope(isolate->GetCodeTracer());
      PrintF(scope.file(),
             "[OSR - Tiering from Maglev to Turbofan failed because "
             "concurrent_osr is disabled. function: %s, osr offset: %d]\n",
             function->DebugNameCStr().get(), osr_offset.ToInt());
    }
    return Smi::zero();
  }

  if (V8_UNLIKELY(isolate->EfficiencyModeEnabled() ||
                  isolate->BatterySaverModeEnabled())) {
    function->feedback_vector()->reset_osr_urgency();
    function->SetInterruptBudget(isolate, BudgetModification::kRaise);
    return Smi::zero();
  }

  return CompileOptimizedOSR(isolate, function, CodeKind::TURBOFAN_JS,
                             osr_offset);
}

}  // namespace

RUNTIME_FUNCTION(Runtime_CompileOptimizedOSRFromMaglev) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(1, args.length());
  DCHECK(v8_flags.use_osr);

  const BytecodeOffset osr_offset(args.positive_smi_value_at(0));

  JavaScriptStackFrameIterator it(isolate);
  MaglevFrame* frame = MaglevFrame::cast(it.frame());
  DCHECK_EQ(frame->LookupCode()->kind(), CodeKind::MAGLEV);
  DirectHandle<JSFunction> function = direct_handle(frame->function(), isolate);

  return CompileOptimizedOSRFromMaglev(isolate, function, osr_offset);
}

RUNTIME_FUNCTION(Runtime_CompileOptimizedOSRFromMaglevInlined) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  DCHECK(v8_flags.use_osr);

  const BytecodeOffset osr_offset(args.positive_smi_value_at(0));
  DirectHandle<JSFunction> function = args.at<JSFunction>(1);

  JavaScriptStackFrameIterator it(isolate);
  MaglevFrame* frame = MaglevFrame::cast(it.frame());
  DCHECK_EQ(frame->LookupCode()->kind(), CodeKind::MAGLEV);

  if (*function != frame->function()) {
    // We are OSRing an inlined function. Mark the top frame one for
    // optimization.
    if (!frame->function()->ActiveTierIsTurbofan(isolate)) {
      isolate->tiering_manager()->MarkForTurboFanOptimization(
          frame->function());
    }
  }

  return CompileOptimizedOSRFromMaglev(isolate, function, osr_offset);
}

RUNTIME_FUNCTION(Runtime_LogOrTraceOptimizedOSREntry) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(0, args.length());
  CHECK(v8_flags.trace_osr || v8_flags.log_function_events);

  BytecodeOffset osr_offset = BytecodeOffset::None();
  Handle<JSFunction> function;
  GetOsrOffsetAndFunctionForOSR(isolate, &osr_offset, &function);

  if (v8_flags.trace_osr) {
    PrintF(CodeTracer::Scope{isolate->GetCodeTracer()}.file(),
           "[OSR - entry. function: %s, osr offset: %d]\n",
           function->DebugNameCStr().get(), osr_offset.ToInt());
  }
#ifndef V8_ENABLE_LEAPTIERING
  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    LogExecution(isolate, function);
  }
#endif  // !V8_ENABLE_LEAPTIERING
  return ReadOnlyRoots(isolate).undefined_value();
}

static Tagged<Object> CompileGlobalEval(
    Isolate* isolate, Handle<i::Object> source_object,
    DirectHandle<SharedFunctionInfo> outer_info, LanguageMode language_mode,
    int eval_scope_info_index, int eval_position) {
  DirectHandle<NativeContext> native_context = isolate->native_context();

  // Check if native context allows code generation from
  // strings. Throw an exception if it doesn't.
  MaybeDirectHandle<String> source;
  bool unknown_object;
  std::tie(source, unknown_object) = Compiler::ValidateDynamicCompilationSource(
      isolate, native_context, source_object);
  // If the argument is an unhandled string time, bounce to GlobalEval.
  if (unknown_object) {
    return native_context->global_eval_fun();
  }
  if (source.is_null()) {
    Handle<Object> error_message =
        native_context->ErrorMessageForCodeGenerationFromStrings();
    DirectHandle<Object> error;
    MaybeDirectHandle<Object> maybe_error = isolate->factory()->NewEvalError(
        MessageTemplate::kCodeGenFromStrings, error_message);
    if (maybe_error.ToHandle(&error)) isolate->Throw(*error);
    return ReadOnlyRoots(isolate).exception();
  }

  // Deal with a normal eval call with a string argument. Compile it
  // and return the compiled function bound in the local context.
  static const ParseRestriction restriction = NO_PARSE_RESTRICTION;
  DirectHandle<JSFunction> compiled;
  DirectHandle<Context> context(isolate->context(), isolate);
  if (!Is<NativeContext>(*context) && v8_flags.reuse_scope_infos) {
    Tagged<WeakFixedArray> array = Cast<Script>(outer_info->script())->infos();
    Tagged<ScopeInfo> stored_info;
    CHECK(array->get(eval_scope_info_index)
              .GetHeapObjectIfWeak(isolate, &stored_info));
    CHECK_EQ(stored_info, context->scope_info());
  }
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, compiled,
      Compiler::GetFunctionFromEval(
          isolate, source.ToHandleChecked(), outer_info, context, language_mode,
          restriction, kNoSourcePosition, eval_position),
      ReadOnlyRoots(isolate).exception());
  return *compiled;
}

RUNTIME_FUNCTION(Runtime_ResolvePossiblyDirectEval) {
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());

  DirectHandle<Object> callee = args.at(0);

  // If "eval" didn't refer to the original GlobalEval, it's not a
  // direct call to eval.
  if (*callee != isolate->native_context()->global_eval_fun()) {
    return *callee;
  }

  DCHECK(is_valid_language_mode(args.smi_value_at(3)));
  LanguageMode language_mode = static_cast<LanguageMode>(args.smi_value_at(3));
  DirectHandle<SharedFunctionInfo> outer_info(args.at<JSFunction>(2)->shared(),
                                              isolate);
  return CompileGlobalEval(isolate, args.at<Object>(1), outer_info,
                           language_mode, args.smi_value_at(4),
                           args.smi_value_at(5));
}

}  // namespace v8::internal
