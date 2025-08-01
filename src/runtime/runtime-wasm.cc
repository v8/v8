// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/builtins/builtins-inl.h"
#include "src/builtins/data-view-ops.h"
#include "src/common/assert-scope.h"
#include "src/common/message-template.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/frames.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/runtime/runtime-utils.h"
#include "src/strings/unicode-inl.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-export-wrapper-cache.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/wasm-value.h"

#if V8_ENABLE_WEBASSEMBLY && V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter.h"
#endif  // V8_ENABLE_WEBASSEMBLY && V8_ENABLE_DRUMBRAKE

namespace v8::internal {

// TODO(13036): See if we can find a way to have the stack walker visit
// tagged values being passed from Wasm to runtime functions. In the meantime,
// disallow access to safe-looking-but-actually-unsafe stack-backed handles
// and thereby force manual creation of safe handles (backed by HandleScope).
class RuntimeArgumentsWithoutHandles : public RuntimeArguments {
 public:
  RuntimeArgumentsWithoutHandles(int length, Address* arguments)
      : RuntimeArguments(length, arguments) {}

 private:
  // Disallowing the superclass method.
  template <class S = Object>
  V8_INLINE DirectHandle<S> at(int index) const;
};

#define RuntimeArguments RuntimeArgumentsWithoutHandles

// (End of TODO(13036)-related hackery.)

namespace {

template <typename FrameType>
class FrameFinder {
 public:
  explicit FrameFinder(Isolate* isolate,
                       std::initializer_list<StackFrame::Type>
                           skipped_frame_types = {StackFrame::EXIT})
      : frame_iterator_(isolate, isolate->thread_local_top(),
                        StackFrameIterator::FirstStackOnly{}) {
    // We skip at least one frame.
    DCHECK_LT(0, skipped_frame_types.size());

    for (auto type : skipped_frame_types) {
      DCHECK_EQ(type, frame_iterator_.frame()->type());
      USE(type);
      frame_iterator_.Advance();
    }
    // Type check the frame where the iterator stopped now.
    DCHECK_NOT_NULL(frame());
  }

  FrameType* frame() { return FrameType::cast(frame_iterator_.frame()); }

 private:
  StackFrameIterator frame_iterator_;
};

Tagged<WasmTrustedInstanceData> GetWasmInstanceDataOnStackTop(
    Isolate* isolate) {
  Address fp = Isolate::c_entry_fp(isolate->thread_local_top());
  fp = Memory<Address>(fp + ExitFrameConstants::kCallerFPOffset);
#ifdef DEBUG
  intptr_t marker =
      Memory<intptr_t>(fp + CommonFrameConstants::kContextOrFrameTypeOffset);
  DCHECK(StackFrame::MarkerToType(marker) == StackFrame::WASM ||
         StackFrame::MarkerToType(marker) == StackFrame::WASM_SEGMENT_START);
#endif
  Tagged<Object> trusted_instance_data(
      Memory<Address>(fp + WasmFrameConstants::kWasmInstanceDataOffset));
  return Cast<WasmTrustedInstanceData>(trusted_instance_data);
}

Tagged<Context> GetNativeContextFromWasmInstanceOnStackTop(Isolate* isolate) {
  return GetWasmInstanceDataOnStackTop(isolate)->native_context();
}

Tagged<Object> ThrowWasmError(
    Isolate* isolate, MessageTemplate message,
    std::initializer_list<DirectHandle<Object>> args = {}) {
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    // Store the trap reason to be retrieved later when the interpreter will
    // trap while detecting the thrown exception.
    wasm::WasmInterpreterThread::SetRuntimeLastWasmError(isolate, message);
  }
#endif  // V8_ENABLE_DRUMBRAKE

  DirectHandle<JSObject> error_obj =
      isolate->factory()->NewWasmRuntimeError(message, base::VectorOf(args));
  JSObject::AddProperty(isolate, error_obj,
                        isolate->factory()->wasm_uncatchable_symbol(),
                        isolate->factory()->true_value(), NONE);
  return isolate->Throw(*error_obj);
}

Tagged<Object> ThrowWasmSuspendError(Isolate* isolate,
                                     MessageTemplate message) {
  DirectHandle<JSObject> error_obj =
      isolate->factory()->NewWasmSuspendError(message);
  return isolate->Throw(*error_obj);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmGenericWasmToJSObject) {
  SealHandleScope seal_handle_scope(isolate);
  DCHECK_EQ(1, args.length());
  Tagged<Object> value = args[0];
  if (IsWasmFuncRef(value)) {
    Tagged<WasmInternalFunction> internal =
        Cast<WasmFuncRef>(value)->internal(isolate);
    Tagged<JSFunction> external;
    if (internal->try_get_external(&external)) return external;
    // Slow path:
    HandleScope handle_scope(isolate);
    return *WasmInternalFunction::GetOrCreateExternal(
        direct_handle(internal, isolate));
  }
  if (IsWasmNull(value)) return ReadOnlyRoots(isolate).null_value();
  return value;
}

// Takes a JS object and a wasm type as Smi. Type checks the object against the
// type; if the check succeeds, returns the object in its wasm representation;
// otherwise throws a type error.
RUNTIME_FUNCTION(Runtime_WasmGenericJSToWasmObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> value(args[0], isolate);
  // Make sure CanonicalValueType fits properly in a Smi.
  static_assert(wasm::CanonicalValueType::kLastUsedBit + 1 <= kSmiValueSize);
  int raw_type = args.smi_value_at(1);

  wasm::CanonicalValueType type =
      wasm::CanonicalValueType::FromRawBitField(raw_type);
  const char* error_message;
  DirectHandle<Object> result;
  if (!JSToWasmObject(isolate, value, type, &error_message).ToHandle(&result)) {
    return isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kWasmTrapJSTypeError));
  }
  return *result;
}

// Parameters:
// args[0]: the object, any JS value.
// args[1]: the expected canonicalized ValueType, Smi-tagged.
// Type checks the object against the type; if the check succeeds, returns the
// object in its wasm representation; otherwise throws a type error.
RUNTIME_FUNCTION(Runtime_WasmJSToWasmObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> value(args[0], isolate);
  // Make sure ValueType fits properly in a Smi.
  static_assert(wasm::CanonicalValueType::kLastUsedBit + 1 <= kSmiValueSize);
  int raw_type = args.smi_value_at(1);

  wasm::CanonicalValueType expected =
      wasm::CanonicalValueType::FromRawBitField(raw_type);
  const char* error_message;
  DirectHandle<Object> result;
  bool success = JSToWasmObject(isolate, value, expected, &error_message)
                     .ToHandle(&result);
  Tagged<Object> ret = success
                           ? *result
                           : isolate->Throw(*isolate->factory()->NewTypeError(
                                 MessageTemplate::kWasmTrapJSTypeError));
  return ret;
}

RUNTIME_FUNCTION(Runtime_WasmMemoryGrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  // {memory_index} and {delta_pages} are checked to be positive Smis in the
  // WasmMemoryGrow builtin which calls this runtime function.
  uint32_t memory_index = args.positive_smi_value_at(1);
  uint32_t delta_pages = args.positive_smi_value_at(2);

  DirectHandle<WasmMemoryObject> memory_object{
      trusted_instance_data->memory_object(memory_index), isolate};
  int ret = WasmMemoryObject::Grow(isolate, memory_object, delta_pages);
  // The WasmMemoryGrow builtin which calls this runtime function expects us to
  // always return a Smi.
  DCHECK(!isolate->has_exception());
  return Smi::FromInt(ret);
}

RUNTIME_FUNCTION(Runtime_TrapHandlerThrowWasmError) {
  CHECK(isolate->IsOnCentralStack());
  HandleScope scope(isolate);
  FrameFinder<WasmFrame> frame_finder(isolate, {StackFrame::EXIT});
  WasmFrame* frame = frame_finder.frame();
  // TODO(ahaas): We cannot use frame->position() here because for inlined
  // function it does not return the correct source position. We should remove
  // frame->position() to avoid problems in the future.
  FrameSummaries summaries = frame->Summarize();
  DCHECK(summaries.frames.back().IsWasm());
  int pos = summaries.frames.back().AsWasm().SourcePosition();

  wasm::WasmCodeRefScope code_ref_scope;
  auto wire_bytes = frame->wasm_code()->native_module()->wire_bytes();
  wasm::WasmOpcode op = static_cast<wasm::WasmOpcode>(wire_bytes.at(pos));
  MessageTemplate message = MessageTemplate::kWasmTrapMemOutOfBounds;
  if (op == wasm::kGCPrefix || op == wasm::kExprRefAsNonNull ||
      op == wasm::kExprCallRef || op == wasm::kExprReturnCallRef ||
      // Calling imported string function with null can trigger a signal.
      op == wasm::kExprCallFunction || op == wasm::kExprReturnCall ||
      // shared-everything atomic instructions.
      (op == wasm::kAtomicPrefix && wire_bytes.at(pos + 1) >= 0x4F)) {
    message = MessageTemplate::kWasmTrapNullDereference;
#if DEBUG
  } else {
    if (wasm::WasmOpcodes::IsPrefixOpcode(op)) {
      op = wasm::Decoder{wire_bytes}
               .read_prefixed_opcode<wasm::Decoder::NoValidationTag>(
                   &wire_bytes.begin()[pos])
               .first;
    }
#endif  // DEBUG
  }
  return ThrowWasmError(isolate, message);
}

RUNTIME_FUNCTION(Runtime_ThrowWasmError) {
  DCHECK(isolate->IsOnCentralStack());
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int message_id = args.smi_value_at(0);
  return ThrowWasmError(isolate, MessageTemplateFromInt(message_id));
}

RUNTIME_FUNCTION(Runtime_ThrowWasmStackOverflow) {
  SealHandleScope shs(isolate);
  DCHECK_LE(0, args.length());
  return isolate->StackOverflow();
}

RUNTIME_FUNCTION(Runtime_WasmThrowJSTypeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kWasmTrapJSTypeError));
}

RUNTIME_FUNCTION(Runtime_ThrowWasmSuspendError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));

  return ThrowWasmSuspendError(isolate, message_id);
}

RUNTIME_FUNCTION(Runtime_WasmThrowRangeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewRangeError(message_id));
}

RUNTIME_FUNCTION(Runtime_WasmThrowDataViewTypeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  DataViewOp op = static_cast<DataViewOp>(isolate->error_message_param());
  DirectHandle<String> op_name =
      isolate->factory()->NewStringFromAsciiChecked(ToString(op));
  DirectHandle<Object> value(args[1], isolate);

  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewTypeError(message_id, op_name, value));
}

RUNTIME_FUNCTION(Runtime_WasmThrowDataViewDetachedError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  DataViewOp op = static_cast<DataViewOp>(isolate->error_message_param());
  DirectHandle<String> op_name =
      isolate->factory()->NewStringFromAsciiChecked(ToString(op));

  THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message_id, op_name));
}

RUNTIME_FUNCTION(Runtime_WasmThrowTypeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  DirectHandle<Object> arg(args[1], isolate);
  if (IsSmi(*arg)) {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message_id));
  } else {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message_id, arg));
  }
}

RUNTIME_FUNCTION(Runtime_WasmThrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<Context> context = GetNativeContextFromWasmInstanceOnStackTop(isolate);
  isolate->set_context(context);
  DirectHandle<WasmExceptionTag> tag(Cast<WasmExceptionTag>(args[0]), isolate);
  DirectHandle<FixedArray> values(Cast<FixedArray>(args[1]), isolate);
  auto js_tag = Cast<WasmTagObject>(context->wasm_js_tag());
  if (*tag == js_tag->tag()) {
    return isolate->Throw(values->get(0));
  } else {
    DirectHandle<WasmExceptionPackage> exception =
        WasmExceptionPackage::New(isolate, tag, values);
    return isolate->Throw(*exception);
  }
}

RUNTIME_FUNCTION(Runtime_WasmReThrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->ReThrow(args[0]);
}

RUNTIME_FUNCTION(Runtime_WasmStackGuard) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());

  uint32_t gap = args.positive_smi_value_at(0);

  // Check if this is a real stack overflow.
  StackLimitCheck check(isolate);
  if (check.WasmHasOverflowed(gap)) return isolate->StackOverflow();

  return isolate->stack_guard()->HandleInterrupts(
      StackGuard::InterruptLevel::kAnyEffect);
}

RUNTIME_FUNCTION(Runtime_WasmCompileLazy) {
  DCHECK_EQ(2, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  int func_index = args.smi_value_at(1);

  TRACE_EVENT1("v8.wasm", "wasm.CompileLazy", "func_index", func_index);
  DisallowHeapAllocation no_gc;
  SealHandleScope scope(isolate);

  DCHECK(isolate->context().is_null());
  if (trusted_instance_data->has_native_context()) {
    isolate->set_context(trusted_instance_data->native_context());
  }
  bool success = wasm::CompileLazy(isolate, trusted_instance_data, func_index);
  if (!success) {
    DCHECK(v8_flags.wasm_lazy_validation);
    AllowHeapAllocation throwing_unwinds_the_stack;
    wasm::ThrowLazyCompilationError(
        isolate, trusted_instance_data->native_module(), func_index);
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots{isolate}.exception();
  }

  return Smi::FromInt(
      wasm::JumpTableOffset(trusted_instance_data->module(), func_index));
}

namespace {
Tagged<FixedArray> AllocateFeedbackVector(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    int declared_func_index) {
  DCHECK(isolate->context().is_null());
  isolate->set_context(trusted_instance_data->native_context());
  const wasm::WasmModule* module =
      trusted_instance_data->native_module()->module();

  int func_index = declared_func_index + module->num_imported_functions;
  int num_slots = NumFeedbackSlots(module, func_index);
  DirectHandle<FixedArray> vector =
      isolate->factory()->NewFixedArrayWithZeroes(num_slots);
  DCHECK_EQ(trusted_instance_data->feedback_vectors()->get(declared_func_index),
            Smi::zero());
  trusted_instance_data->feedback_vectors()->set(declared_func_index, *vector);
  isolate->set_context(Tagged<Context>());
  return *vector;
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmAllocateFeedbackVector) {
  DCHECK(isolate->IsOnCentralStack());
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  DCHECK(v8_flags.wasm_inlining);
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  int declared_func_index = args.smi_value_at(1);
  wasm::NativeModule** native_module_stack_slot =
      reinterpret_cast<wasm::NativeModule**>(args.address_of_arg_at(2));
  wasm::NativeModule* native_module = trusted_instance_data->native_module();
  // We have to save the native_module on the stack, in case the allocation
  // triggers a GC and we need the module to scan LiftoffSetupFrame stack frame.
  *native_module_stack_slot = native_module;
  return AllocateFeedbackVector(isolate, trusted_instance_data,
                                declared_func_index);
}

RUNTIME_FUNCTION(Runtime_WasmLiftoffDeoptFinish) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  // Destroy the Deoptimizer object stored on the isolate.
  size_t deopt_frame_count = Deoptimizer::DeleteForWasm(isolate);
  size_t i = 0;

  // For each liftoff frame, replace the smi in the feedback vector slot holding
  // the declared function index with the actual feedback vector from the
  // WasmTrustedInstanceData. If it doesn't exist yet, it needs to be allocated
  // (which is also the reason why this wasn't done by the deoptimizer already.)
  for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    StackFrame* frame = it.frame();
    if (frame->is_wasm() && WasmFrame::cast(frame)->wasm_code()->is_liftoff()) {
      Address vector_address =
          frame->fp() - WasmLiftoffFrameConstants::kFeedbackVectorOffset;
      Tagged<Object> feedback_slot_value(Memory<intptr_t>(vector_address));
      CHECK(feedback_slot_value.IsSmi());
      int declared_func_index = Cast<Smi>(feedback_slot_value).value();
      Tagged<Object> vector =
          trusted_instance_data->feedback_vectors()->get(declared_func_index);
      // If the vector doesn't exist, allocate a new feedback vector. This
      // happens if the corresponding function wasn't executed in Liftoff yet
      // for this particular module instantiation.
      if (vector == Smi::zero()) {
        if (v8_flags.trace_deopt_verbose) {
          wasm::WasmCodeRefScope code_ref_scope;
          const wasm::WasmCode* code =
              trusted_instance_data->native_module()->GetCode(
                  declared_func_index);
          PrintF(
              "Wasm deoptimization: allocating feedback vector for function %s "
              "[%d]\n",
              code ? code->DebugName().c_str() : "<no code object>",
              declared_func_index);
        }
        vector = AllocateFeedbackVector(isolate, trusted_instance_data,
                                        declared_func_index);
      }
      memcpy(reinterpret_cast<void*>(vector_address), &vector,
             sizeof(intptr_t));
      if (++i == deopt_frame_count) {
        break;  // All deopt frames have been visited.
      }
    }
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {
void ReplaceJSToWasmWrapper(
    Isolate* isolate, Tagged<WasmTrustedInstanceData> trusted_instance_data,
    int function_index, Tagged<Code> wrapper_code,
    bool receiver_is_first_param) {
  Tagged<WasmFuncRef> func_ref;
  // Always expect a func_ref. If this fails, we are maybe compiling a wrapper
  // for the start function. This function is only called once, so this should
  // not happen.
  CHECK(trusted_instance_data->try_get_func_ref(function_index, &func_ref));
  Tagged<JSFunction> external_function;
  CHECK(func_ref->internal(isolate)->try_get_external(&external_function));
  if (external_function->shared()->HasWasmJSFunctionData(isolate)) return;
  CHECK(external_function->shared()->HasWasmExportedFunctionData(isolate));
  Tagged<WasmExportedFunctionData> function_data =
      external_function->shared()->wasm_exported_function_data();
  if ((function_data->receiver_is_first_param() != 0) !=
      receiver_is_first_param) {
    // Different "receiver is first parameter" settings, do nothing.
    return;
  }
  external_function->UpdateCode(isolate, wrapper_code);
  function_data->set_wrapper_code(wrapper_code);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_TierUpJSToWasmWrapper) {
  DCHECK_EQ(1, args.length());

  // Avoid allocating a HandleScope and handles on the fast path.
  Tagged<WasmExportedFunctionData> function_data =
      Cast<WasmExportedFunctionData>(args[0]);
  Tagged<WasmTrustedInstanceData> trusted_data = function_data->instance_data();

  const wasm::WasmModule* module = trusted_data->module();
  const int function_index = function_data->function_index();
  bool receiver_is_first_param = function_data->receiver_is_first_param() != 0;
  const wasm::WasmFunction& function = module->functions[function_index];
  const wasm::CanonicalTypeIndex sig_id =
      module->canonical_sig_id(function.sig_index);
  const wasm::CanonicalSig* sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_id);

  Tagged<CodeWrapper> maybe_cached_wrapper = wasm::WasmExportWrapperCache::Get(
      isolate, sig_id, receiver_is_first_param);
  Tagged<Code> wrapper_code;
  if (!maybe_cached_wrapper.is_null()) {
    wrapper_code = maybe_cached_wrapper->code(isolate);
  } else {
    // Set the context on the isolate and open a handle scope for allocation of
    // new objects. Wrap {trusted_data} in a handle so it survives GCs.
    DCHECK(isolate->context().is_null());
    isolate->set_context(trusted_data->native_context());
    HandleScope scope(isolate);
    DirectHandle<WasmTrustedInstanceData> trusted_data_handle{trusted_data,
                                                              isolate};
    DirectHandle<Code> new_wrapper_code =
        wasm::JSToWasmWrapperCompilationUnit::CompileJSToWasmWrapper(
            isolate, sig, sig_id, receiver_is_first_param);

    // Compilation must have installed the wrapper into the cache.
    DCHECK_EQ(new_wrapper_code->wrapper(),
              wasm::WasmExportWrapperCache::Get(isolate, sig_id,
                                                receiver_is_first_param));

    // Reset raw pointers still needed outside the slow path.
    wrapper_code = *new_wrapper_code;
    trusted_data = *trusted_data_handle;
    function_data = {};
  }

  // Replace the wrapper for the function that triggered the tier-up.
  // This is to ensure that the wrapper is replaced, even if the function
  // is implicitly exported and is not part of the export_table.
  ReplaceJSToWasmWrapper(isolate, trusted_data, function_index, wrapper_code,
                         receiver_is_first_param);

  // Iterate over all exports to replace eagerly the wrapper for all functions
  // that share the signature of the function that tiered up.
  for (wasm::WasmExport exp : module->export_table) {
    if (exp.kind != wasm::kExternalFunction) continue;
    int index = static_cast<int>(exp.index);
    if (index == function_index) continue;  // Already replaced.
    const wasm::WasmFunction& exp_function = module->functions[index];
    if (module->canonical_sig_id(exp_function.sig_index) != sig_id) {
      continue;  // Different signature.
    }
    ReplaceJSToWasmWrapper(isolate, trusted_data, index, wrapper_code,
                           receiver_is_first_param);
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_IsWasmExternalFunction) {
  DCHECK_EQ(1, args.length());
  return isolate->heap()->ToBoolean(
      WasmExternalFunction::IsWasmExternalFunction(args[0]));
}

RUNTIME_FUNCTION(Runtime_TierUpWasmToJSWrapper) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<WasmImportData> import_data(Cast<WasmImportData>(args[0]),
                                           isolate);

  DCHECK(isolate->context().is_null());
  isolate->set_context(import_data->native_context());

  const wasm::CanonicalSig* sig = import_data->sig();
  DirectHandle<Object> origin(import_data->call_origin(), isolate);
  wasm::WasmCodeRefScope code_ref_scope;

  if (IsWasmInternalFunction(*origin)) {
    // The tierup for `WasmInternalFunction` is special, as there may not be an
    // instance.
    int expected_arity = static_cast<int>(sig->parameter_count());
    wasm::ImportCallKind kind;
    if (IsJSFunction(import_data->callable())) {
      Tagged<SharedFunctionInfo> shared =
          Cast<JSFunction>(import_data->callable())->shared();
      expected_arity =
          shared->internal_formal_parameter_count_without_receiver();
      kind = wasm::ImportCallKind::kJSFunction;
    } else {
      kind = wasm::ImportCallKind::kUseCallBuiltin;
    }
    wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
    wasm::CanonicalTypeIndex canonical_sig_index =
        wasm::GetTypeCanonicalizer()->FindIndex_Slow(sig);
    wasm::Suspend suspend = import_data->suspend();
    std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle =
        cache->GetCompiled(isolate, kind, canonical_sig_index, expected_arity,
                           suspend, sig);
    DCHECK_EQ(Cast<WasmInternalFunction>(*origin)->call_target(),
              wrapper_handle->code_pointer());
    return ReadOnlyRoots(isolate).undefined_value();
  }

  CHECK(IsWasmDispatchTable(*origin));
  DirectHandle<WasmDispatchTable> dispatch_table =
      Cast<WasmDispatchTable>(origin);
  int table_slot = import_data->table_slot();
  wasm::CanonicalTypeIndex sig_index = dispatch_table->sig(table_slot);
  DCHECK_EQ(sig,
            wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_index));

  // Compile a wrapper for the target callable.
  DirectHandle<JSReceiver> callable(Cast<JSReceiver>(import_data->callable()),
                                    isolate);
  wasm::Suspend suspend = import_data->suspend();

  wasm::ResolvedWasmImport resolved({}, -1, callable, sig, sig_index,
                                    wasm::WellKnownImport::kUninstantiated);
  wasm::ImportCallKind kind = resolved.kind();
  callable = resolved.callable();  // Update to ultimate target.
  DCHECK_NE(wasm::ImportCallKind::kLinkError, kind);
  int expected_arity = static_cast<int>(sig->parameter_count());
  if (kind == wasm::ImportCallKind ::kJSFunction) {
    expected_arity = Cast<JSFunction>(callable)
                         ->shared()
                         ->internal_formal_parameter_count_without_receiver();
  }

  wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
  std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle =
      cache->GetCompiled(isolate, kind, sig_index, expected_arity, suspend,
                         sig);

  DCHECK_EQ(dispatch_table->target(table_slot), wrapper_handle->code_pointer());

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTriggerTierUp) {
  SealHandleScope shs(isolate);

  {
    DisallowGarbageCollection no_gc;
    DCHECK_EQ(1, args.length());
    Tagged<WasmTrustedInstanceData> trusted_data =
        Cast<WasmTrustedInstanceData>(args[0]);

    FrameFinder<WasmFrame> frame_finder(isolate);
    int func_index = frame_finder.frame()->function_index();
    DCHECK_EQ(trusted_data, frame_finder.frame()->trusted_instance_data());

    if (V8_UNLIKELY(v8_flags.wasm_sync_tier_up)) {
      if (!trusted_data->native_module()->HasCodeWithTier(
              func_index, wasm::ExecutionTier::kTurbofan)) {
        wasm::TierUpNowForTesting(isolate, trusted_data, func_index);
      }
      // We call this function when the tiering budget runs out, so reset that
      // budget to appropriately delay the next call.
      int array_index =
          wasm::declared_function_index(trusted_data->module(), func_index);
      trusted_data->tiering_budget_array()[array_index].store(
          v8_flags.wasm_tiering_budget, std::memory_order_relaxed);
    } else {
      wasm::TriggerTierUp(isolate, trusted_data, func_index);
    }
  }

  // We're reusing this interrupt mechanism to interrupt long-running loops.
  StackLimitCheck check(isolate);
  // We don't need to handle stack overflows here, because the function that
  // performed this runtime call did its own stack check at its beginning.
  // However, we can't DCHECK(!check.JsHasOverflowed()) here, because the
  // additional stack space used by the CEntryStub and this runtime function
  // itself might have pushed us above the limit where a stack check would
  // fail.
  if (check.InterruptRequested()) {
    // Note: This might trigger a GC, which invalidates the {args} object (see
    // https://crbug.com/v8/13036#2).
    Tagged<Object> result = isolate->stack_guard()->HandleInterrupts();
    if (IsException(result)) return result;
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmI32AtomicWait) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  int memory_index = args.smi_value_at(1);
  double offset_double = args.number_value_at(2);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  int32_t expected_value = NumberToInt32(args[3]);
  Tagged<BigInt> timeout_ns = Cast<BigInt>(args[4]);

  DirectHandle<JSArrayBuffer> array_buffer{
      trusted_instance_data->memory_object(memory_index)->array_buffer(),
      isolate};
  // Should have trapped if address was OOB.
  DCHECK_LT(offset, array_buffer->byte_length());

  // Trap if memory is not shared, or wait is not allowed on the isolate
  if (!array_buffer->is_shared() || !isolate->allow_atomics_wait()) {
    return ThrowWasmError(
        isolate, MessageTemplate::kAtomicsOperationNotAllowed,
        {isolate->factory()->NewStringFromAsciiChecked("Atomics.wait")});
  }
  return FutexEmulation::WaitWasm32(isolate, array_buffer, offset,
                                    expected_value, timeout_ns->AsInt64());
}

RUNTIME_FUNCTION(Runtime_WasmI64AtomicWait) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  int memory_index = args.smi_value_at(1);
  double offset_double = args.number_value_at(2);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  Tagged<BigInt> expected_value = Cast<BigInt>(args[3]);
  Tagged<BigInt> timeout_ns = Cast<BigInt>(args[4]);

  DirectHandle<JSArrayBuffer> array_buffer{
      trusted_instance_data->memory_object(memory_index)->array_buffer(),
      isolate};
  // Should have trapped if address was OOB.
  DCHECK_LT(offset, array_buffer->byte_length());

  // Trap if memory is not shared, or if wait is not allowed on the isolate
  if (!array_buffer->is_shared() || !isolate->allow_atomics_wait()) {
    return ThrowWasmError(
        isolate, MessageTemplate::kAtomicsOperationNotAllowed,
        {isolate->factory()->NewStringFromAsciiChecked("Atomics.wait")});
  }
  return FutexEmulation::WaitWasm64(isolate, array_buffer, offset,
                                    expected_value->AsInt64(),
                                    timeout_ns->AsInt64());
}

namespace {
Tagged<Object> ThrowTableOutOfBounds(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data) {
  // Handle out-of-bounds access here in the runtime call, rather
  // than having the lower-level layers deal with JS exceptions.
  if (isolate->context().is_null()) {
    isolate->set_context(trusted_instance_data->native_context());
  }
  return ThrowWasmError(isolate, MessageTemplate::kWasmTrapTableOutOfBounds);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmRefFunc) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t function_index = args.positive_smi_value_at(1);

  return *WasmTrustedInstanceData::GetOrCreateFuncRef(
      isolate, trusted_instance_data, function_index);
}

RUNTIME_FUNCTION(Runtime_WasmInternalFunctionCreateExternal) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  // TODO(14564): Pass WasmFuncRef here instead of WasmInternalFunction.
  DirectHandle<WasmInternalFunction> internal(
      Cast<WasmInternalFunction>(args[0]), isolate);
  return *WasmInternalFunction::GetOrCreateExternal(internal);
}

RUNTIME_FUNCTION(Runtime_WasmFunctionTableGet) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t entry_index = args.positive_smi_value_at(2);
  DCHECK_LT(table_index, trusted_instance_data->tables()->length());
  auto table = direct_handle(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);
  // We only use the runtime call for lazily initialized function references.
  DCHECK(table->unsafe_type().ref_type_kind() == wasm::RefTypeKind::kFunction);

  if (!table->is_in_bounds(entry_index)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapTableOutOfBounds);
  }

  return *WasmTableObject::Get(isolate, table, entry_index);
}

RUNTIME_FUNCTION(Runtime_WasmFunctionTableSet) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t entry_index = args.positive_smi_value_at(2);
  DirectHandle<Object> element(args[3], isolate);
  DCHECK_LT(table_index, trusted_instance_data->tables()->length());
  auto table = direct_handle(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);
  // We only use the runtime call for lazily initialized function references.
  DCHECK(table->unsafe_type().ref_type_kind() == wasm::RefTypeKind::kFunction);

  if (!table->is_in_bounds(entry_index)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapTableOutOfBounds);
  }
  WasmTableObject::Set(isolate, table, entry_index, element);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTableInit) {
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t elem_segment_index = args.positive_smi_value_at(2);
  static_assert(
      wasm::kV8MaxWasmTableSize < kSmiMaxValue,
      "Make sure clamping to Smi range doesn't make an invalid call valid");
  uint32_t dst = args.positive_smi_value_at(3);
  uint32_t src = args.positive_smi_value_at(4);
  uint32_t count = args.positive_smi_value_at(5);

  DCHECK(!isolate->context().is_null());

  // TODO(14616): Pass the correct instance data.
  std::optional<MessageTemplate> opt_error =
      WasmTrustedInstanceData::InitTableEntries(
          isolate, trusted_instance_data, trusted_instance_data, table_index,
          elem_segment_index, dst, src, count);
  if (opt_error.has_value()) {
    return ThrowWasmError(isolate, opt_error.value());
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTableCopy) {
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t table_dst_index = args.positive_smi_value_at(1);
  uint32_t table_src_index = args.positive_smi_value_at(2);
  static_assert(
      wasm::kV8MaxWasmTableSize < kSmiMaxValue,
      "Make sure clamping to Smi range doesn't make an invalid call valid");
  uint32_t dst = args.positive_smi_value_at(3);
  uint32_t src = args.positive_smi_value_at(4);
  uint32_t count = args.positive_smi_value_at(5);

  DCHECK(!isolate->context().is_null());

  bool oob = !WasmTrustedInstanceData::CopyTableEntries(
      isolate, trusted_instance_data, table_dst_index, table_src_index, dst,
      src, count);
  if (oob) return ThrowTableOutOfBounds(isolate, trusted_instance_data);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTableGrow) {
  DCHECK(isolate->IsOnCentralStack());
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t table_index = args.positive_smi_value_at(1);
  DirectHandle<Object> value(args[2], isolate);
  uint32_t delta = args.positive_smi_value_at(3);

  DirectHandle<WasmTableObject> table(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);
  int result = WasmTableObject::Grow(isolate, table, delta, value);

  return Smi::FromInt(result);
}

RUNTIME_FUNCTION(Runtime_WasmTableFill) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t start = args.positive_smi_value_at(2);
  DirectHandle<Object> value(args[3], isolate);
  uint32_t count = args.positive_smi_value_at(4);

  DirectHandle<WasmTableObject> table(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);

  uint32_t table_size = table->current_length();

  if (start > table_size) {
    return ThrowTableOutOfBounds(isolate, trusted_instance_data);
  }

  // Even when table.fill goes out-of-bounds, as many entries as possible are
  // put into the table. Only afterwards we trap.
  uint32_t fill_count = std::min(count, table_size - start);
  if (fill_count < count) {
    return ThrowTableOutOfBounds(isolate, trusted_instance_data);
  }
  WasmTableObject::Fill(isolate, table, start, value, fill_count);

  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {
// Returns true if any breakpoint was hit, false otherwise.
bool ExecuteWasmDebugBreaks(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    WasmFrame* frame) {
  DirectHandle<Script> script{trusted_instance_data->module_object()->script(),
                              isolate};
  auto* debug_info = trusted_instance_data->native_module()->GetDebugInfo();

  // Enter the debugger.
  DebugScope debug_scope(isolate->debug());

  // Check for instrumentation breakpoints first, but still execute regular
  // breakpoints afterwards.
  bool paused_on_instrumentation = false;
  DCHECK_EQ(script->break_on_entry(),
            !!trusted_instance_data->break_on_entry());
  if (script->break_on_entry()) {
    MaybeDirectHandle<FixedArray> maybe_on_entry_breakpoints =
        WasmScript::CheckBreakPoints(isolate, script,
                                     WasmScript::kOnEntryBreakpointPosition,
                                     frame->id());
    script->set_break_on_entry(false);
    // Update the "break_on_entry" flag on all live instances.
    i::Tagged<i::WeakArrayList> weak_instance_list =
        script->wasm_weak_instance_list();
    for (int i = 0; i < weak_instance_list->length(); ++i) {
      if (weak_instance_list->Get(i).IsCleared()) continue;
      i::Cast<i::WasmInstanceObject>(weak_instance_list->Get(i).GetHeapObject())
          ->trusted_data(isolate)
          ->set_break_on_entry(false);
    }
    DCHECK(!trusted_instance_data->break_on_entry());
    if (!maybe_on_entry_breakpoints.is_null()) {
      isolate->debug()->OnInstrumentationBreak();
      paused_on_instrumentation = true;
    }
  }

  if (debug_info->IsStepping(frame) && !debug_info->IsFrameBlackboxed(frame)) {
    debug_info->ClearStepping(isolate);
    StepAction step_action = isolate->debug()->last_step_action();
    isolate->debug()->ClearStepping();
    isolate->debug()->OnDebugBreak(isolate->factory()->empty_fixed_array(),
                                   step_action);
    return true;
  }

  // Check whether we hit a breakpoint.
  DirectHandle<FixedArray> breakpoints;
  if (WasmScript::CheckBreakPoints(isolate, script, frame->position(),
                                   frame->id())
          .ToHandle(&breakpoints)) {
    debug_info->ClearStepping(isolate);
    StepAction step_action = isolate->debug()->last_step_action();
    isolate->debug()->ClearStepping();
    if (isolate->debug()->break_points_active()) {
      // We hit one or several breakpoints. Notify the debug listeners.
      isolate->debug()->OnDebugBreak(breakpoints, step_action);
    }
    return true;
  }

  return paused_on_instrumentation;
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmDebugBreak) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  FrameFinder<WasmFrame> frame_finder(
      isolate, {StackFrame::EXIT, StackFrame::WASM_DEBUG_BREAK});
  WasmFrame* frame = frame_finder.frame();
  DirectHandle<WasmTrustedInstanceData> trusted_data{
      frame->trusted_instance_data(), isolate};
  isolate->set_context(trusted_data->native_context());

  if (!ExecuteWasmDebugBreaks(isolate, trusted_data, frame)) {
    // We did not hit a breakpoint. If we are in stepping code, but the user did
    // not request stepping, clear this (to save further calls into this runtime
    // function).
    auto* debug_info = trusted_data->native_module()->GetDebugInfo();
    debug_info->ClearStepping(frame);
  }

  // Execute a stack check before leaving this function. This is to handle any
  // interrupts set by the debugger (e.g. termination), but also to execute Wasm
  // code GC to get rid of temporarily created Wasm code.
  StackLimitCheck check(isolate);
  if (check.InterruptRequested()) {
    Tagged<Object> interrupt_object =
        isolate->stack_guard()->HandleInterrupts();
    // Interrupt handling can create an exception, including the
    // termination exception.
    if (IsException(interrupt_object, isolate)) return interrupt_object;
    DCHECK(IsUndefined(interrupt_object, isolate));
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

// Assumes copy ranges are in-bounds and copy length > 0.
// TODO(manoskouk): Unify part of this with the implementation in
// wasm-extern-refs.cc
RUNTIME_FUNCTION(Runtime_WasmArrayCopy) {
  HandleScope scope(isolate);
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(5, args.length());
  Tagged<WasmArray> dst_array = Cast<WasmArray>(args[0]);
  uint32_t dst_index = args.positive_smi_value_at(1);
  Tagged<WasmArray> src_array = Cast<WasmArray>(args[2]);
  uint32_t src_index = args.positive_smi_value_at(3);
  uint32_t length = args.positive_smi_value_at(4);
  DCHECK_GT(length, 0);
  bool overlapping_ranges =
      dst_array.ptr() == src_array.ptr() &&
      (dst_index < src_index ? dst_index + length > src_index
                             : src_index + length > dst_index);
  wasm::CanonicalValueType element_type =
      src_array->map()->wasm_type_info()->element_type();
  if (element_type.is_reference()) {
    ObjectSlot dst_slot = dst_array->ElementSlot(dst_index);
    ObjectSlot src_slot = src_array->ElementSlot(src_index);
    if (overlapping_ranges) {
      isolate->heap()->MoveRange(dst_array, dst_slot, src_slot, length,
                                 UPDATE_WRITE_BARRIER);
    } else {
      isolate->heap()->CopyRange(dst_array, dst_slot, src_slot, length,
                                 UPDATE_WRITE_BARRIER);
    }
  } else {
    void* dst = reinterpret_cast<void*>(dst_array->ElementAddress(dst_index));
    void* src = reinterpret_cast<void*>(src_array->ElementAddress(src_index));
    size_t copy_size = length * element_type.value_kind_size();
    if (overlapping_ranges) {
      MemMove(dst, src, copy_size);
    } else {
      MemCopy(dst, src, copy_size);
    }
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmAllocateDescriptorStruct) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_data{
      Cast<WasmTrustedInstanceData>(args[0]), isolate};
  DirectHandle<Map> map{Cast<Map>(args[1]), isolate};
  wasm::ModuleTypeIndex type_index{args.positive_smi_value_at(2)};
  DirectHandle<Object> first_field{args[3], isolate};
  return *WasmStruct::AllocateDescriptorUninitialized(
      isolate, trusted_data, type_index, map, first_field);
}

RUNTIME_FUNCTION(Runtime_WasmArrayNewSegment) {
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t segment_index = args.positive_smi_value_at(1);
  uint32_t offset = args.positive_smi_value_at(2);
  uint32_t length = args.positive_smi_value_at(3);
  DirectHandle<Map> rtt(Cast<Map>(args[4]), isolate);

  wasm::CanonicalValueType element_type = rtt->wasm_type_info()->element_type();

  uint32_t element_size = element_type.value_kind_size();
  // This check also implies no overflow.
  if (length > static_cast<uint32_t>(WasmArray::MaxLength(element_size))) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapArrayTooLarge);
  }

  if (element_type.is_numeric()) {
    // No chance of overflow due to the check above.
    uint32_t length_in_bytes = length * element_size;

    if (!base::IsInBounds<uint32_t>(
            offset, length_in_bytes,
            trusted_instance_data->data_segment_sizes()->get(segment_index))) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
    }

    Address source =
        trusted_instance_data->data_segment_starts()->get(segment_index) +
        offset;
    return *isolate->factory()->NewWasmArrayFromMemory(length, rtt,
                                                       element_type, source);
  } else {
    DirectHandle<Object> elem_segment_raw(
        trusted_instance_data->element_segments()->get(segment_index), isolate);
    const wasm::WasmElemSegment* module_elem_segment =
        &trusted_instance_data->module()->elem_segments[segment_index];
    // If the segment is initialized in the instance, we have to get its length
    // from there, as it might have been dropped. If the segment is
    // uninitialized, we need to fetch its length from the module.
    int segment_length = IsFixedArray(*elem_segment_raw)
                             ? Cast<FixedArray>(elem_segment_raw)->length()
                             : module_elem_segment->element_count;
    if (!base::IsInBounds<size_t>(offset, length, segment_length)) {
      return ThrowWasmError(
          isolate, MessageTemplate::kWasmTrapElementSegmentOutOfBounds);
    }
    // TODO(14616): Pass the correct instance data.
    DirectHandle<Object> result =
        isolate->factory()->NewWasmArrayFromElementSegment(
            trusted_instance_data, trusted_instance_data, segment_index, offset,
            length, rtt, element_type);
    if (IsSmi(*result)) {
      return ThrowWasmError(
          isolate, static_cast<MessageTemplate>(Cast<Smi>(*result).value()));
    } else {
      return *result;
    }
  }
}

RUNTIME_FUNCTION(Runtime_WasmArrayInitSegment) {
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t segment_index = args.positive_smi_value_at(1);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[2]), isolate);
  uint32_t array_index = args.positive_smi_value_at(3);
  uint32_t segment_offset = args.positive_smi_value_at(4);
  uint32_t length = args.positive_smi_value_at(5);

  wasm::CanonicalValueType element_type =
      array->map()->wasm_type_info()->element_type();

  if (element_type.is_numeric()) {
    if (!base::IsInBounds<uint32_t>(array_index, length, array->length())) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapArrayOutOfBounds);
    }

    // No chance of overflow, due to the check above and the limit in array
    // length.
    uint32_t length_in_bytes = length * element_type.value_kind_size();

    if (!base::IsInBounds<uint32_t>(
            segment_offset, length_in_bytes,
            trusted_instance_data->data_segment_sizes()->get(segment_index))) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
    }

    Address source =
        trusted_instance_data->data_segment_starts()->get(segment_index) +
        segment_offset;
    Address dest = array->ElementAddress(array_index);
#if V8_TARGET_BIG_ENDIAN
    MemCopyAndSwitchEndianness(reinterpret_cast<void*>(dest),
                               reinterpret_cast<void*>(source), length,
                               element_type.value_kind_size());
#else
    MemCopy(reinterpret_cast<void*>(dest), reinterpret_cast<void*>(source),
            length_in_bytes);
#endif
    return *isolate->factory()->undefined_value();
  } else {
    DirectHandle<Object> elem_segment_raw(
        trusted_instance_data->element_segments()->get(segment_index), isolate);
    const wasm::WasmElemSegment* module_elem_segment =
        &trusted_instance_data->module()->elem_segments[segment_index];
    // If the segment is initialized in the instance, we have to get its length
    // from there, as it might have been dropped. If the segment is
    // uninitialized, we need to fetch its length from the module.
    int segment_length = IsFixedArray(*elem_segment_raw)
                             ? Cast<FixedArray>(elem_segment_raw)->length()
                             : module_elem_segment->element_count;
    if (!base::IsInBounds<size_t>(segment_offset, length, segment_length)) {
      return ThrowWasmError(
          isolate, MessageTemplate::kWasmTrapElementSegmentOutOfBounds);
    }
    if (!base::IsInBounds(array_index, length, array->length())) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapArrayOutOfBounds);
    }

    // If the element segment has not been initialized yet, lazily initialize it
    // now.
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    // TODO(14616): Fix the instance data.
    std::optional<MessageTemplate> opt_error =
        wasm::InitializeElementSegment(&zone, isolate, trusted_instance_data,
                                       trusted_instance_data, segment_index);
    if (opt_error.has_value()) {
      return ThrowWasmError(isolate, opt_error.value());
    }

    auto elements = direct_handle(
        Cast<FixedArray>(
            trusted_instance_data->element_segments()->get(segment_index)),
        isolate);
    if (length > 0) {
      isolate->heap()->CopyRange(*array, array->ElementSlot(array_index),
                                 elements->RawFieldOfElementAt(segment_offset),
                                 length, UPDATE_WRITE_BARRIER);
    }
    return *isolate->factory()->undefined_value();
  }
}

// Allocate a new suspender, and prepare for stack switching by updating the
// active continuation, active suspender and stack limit.
RUNTIME_FUNCTION(Runtime_WasmAllocateSuspender) {
  HandleScope scope(isolate);
  DirectHandle<WasmSuspenderObject> suspender =
      isolate->factory()->NewWasmSuspenderObject();

  // Update the stack state.
  wasm::StackMemory* active_stack = isolate->isolate_data()->active_stack();
  std::unique_ptr<wasm::StackMemory> target_stack =
      isolate->stack_pool().GetOrAllocate();
  target_stack->jmpbuf()->parent = active_stack;
  target_stack->jmpbuf()->stack_limit = target_stack->jslimit();
  target_stack->jmpbuf()->sp = target_stack->base();
  target_stack->jmpbuf()->fp = kNullAddress;
  target_stack->jmpbuf()->state = wasm::JumpBuffer::Suspended;
  isolate->isolate_data()->set_active_stack(target_stack.get());

  // Update the suspender state.
  if (!isolate->isolate_data()->active_suspender().IsSmi()) {
    suspender->set_parent(
        Cast<WasmSuspenderObject>(isolate->isolate_data()->active_suspender()));
  }
  suspender->set_stack(isolate, target_stack.get());
  isolate->isolate_data()->set_active_suspender(*suspender);

  target_stack->set_index(isolate->wasm_stacks().size());
  isolate->wasm_stacks().emplace_back(std::move(target_stack));
  for (size_t i = 0; i < isolate->wasm_stacks().size(); ++i) {
    SLOW_DCHECK(isolate->wasm_stacks()[i]->index() == i);
  }

  // Stack limit will be updated in WasmReturnPromiseOnSuspendAsm builtin.
  DCHECK_EQ(active_stack->jmpbuf()->state, wasm::JumpBuffer::Active);
  return *suspender;
}

// Helper function needed for the stress stack switching mode.
// This is a runtime function to avoid writing trusted space memory from
// generated code.
RUNTIME_FUNCTION(Runtime_ClearWasmSuspenderResumeField) {
  // Should only be used in stress stack switching mode.
  CHECK(v8_flags.stress_wasm_stack_switching);

  DCHECK_EQ(1, args.length());
  Tagged<WasmSuspenderObject> suspender = Cast<WasmSuspenderObject>(args[0]);
  suspender->set_resume(ReadOnlyRoots(isolate).undefined_value());
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {

class PrototypesSetup : public wasm::Decoder {
 public:
  struct Method {
    enum Kind : uint8_t { kMethod = 0, kGetter = 1, kSetter = 2 };

    Kind kind;
    bool is_static;
    base::Vector<const char> name;
  };

  PrototypesSetup(Isolate* isolate, const uint8_t* data_begin,
                  const uint8_t* data_end)
      : Decoder(data_begin, data_end), isolate_(isolate) {}

  Tagged<Object> SetupPrototypes(DirectHandle<JSObject> constructors) {
    WHILE_WITH_HANDLE_SCOPE(isolate(), more()) {
      DirectHandle<JSObject> prototype = NextPrototype();
      if (prototype.is_null()) return ReadOnlyRoots(isolate()).exception();
      uint32_t num_methods = consume_u32v("number of methods");
      if (!ok()) break;
      ToDictionaryMode(prototype, num_methods);
      for (uint32_t i = 0; i < num_methods; i++) {
        Method method = NextMethod(false);
        if (!ok()) break;
        DirectHandle<WasmExportedFunction> function = NextFunction();
        if (function.is_null() || !InstallMethod(prototype, method, function)) {
          DCHECK(isolate()->has_exception());
          return ReadOnlyRoots(isolate()).exception();
        }
      }
      if (!ok()) break;
      uint32_t has_constructor = consume_u32v("constructor");
      if (!ok()) break;
      if (has_constructor == 0) continue;
      if (has_constructor > 1) {
        errorf(pc_offset() - 1, "invalid constructor count: %d",
               has_constructor);
        break;
      }

      DirectHandle<WasmExportedFunction> constructor = NextFunction();
      uint32_t ctor_name_length = consume_u32v("constructor name length");
      if (!ok()) break;
      const char* ctor_name_start = reinterpret_cast<const char*>(pc());
      consume_bytes(ctor_name_length);
      if (!ok()) break;
      DirectHandle<String> ctor_name =
          isolate()->factory()->InternalizeUtf8String(
              {ctor_name_start, ctor_name_length});
      if (!InstallConstructor(prototype, constructor, ctor_name,
                              constructors)) {
        DCHECK(isolate()->has_exception());
        return ReadOnlyRoots(isolate()).exception();
      }
      uint32_t num_statics = consume_u32v("number of statics");
      if (!ok()) break;
      if (num_statics == 0) continue;
      ToDictionaryMode(constructor, num_statics);
      for (uint32_t i = 0; i < num_statics; i++) {
        Method method = NextMethod(true);
        if (!ok()) break;
        DirectHandle<WasmExportedFunction> function = NextFunction();
        if (function.is_null() ||
            !InstallMethod(constructor, method, function)) {
          DCHECK(isolate()->has_exception());
          return ReadOnlyRoots(isolate()).exception();
        }
      }
      if (!ok()) break;
    }
    if (!ok()) {
      DirectHandle<String> message =
          isolate()
              ->factory()
              ->NewStringFromUtf8(base::VectorOf(error().message()))
              .ToHandleChecked();
      DirectHandle<JSObject> error = isolate()->factory()->NewError(
          isolate()->wasm_compile_error_function(), message);
      return isolate()->Throw(*error);
    }

    return ReadOnlyRoots(isolate()).undefined_value();
  }

  Method NextMethod(bool is_static) {
    uint8_t kind = consume_u8("kind");
    if (kind > 2) {
      errorf(pc_offset() - 1, "invalid method kind %u", kind);
      return {};
    }
    uint32_t name_length = consume_u32v("name length");
    if (!ok()) return {};
    const char* name_start = reinterpret_cast<const char*>(pc());
    consume_bytes(name_length);
    if (!ok()) return {};
    return {.kind = static_cast<Method::Kind>(kind),
            .is_static = is_static,
            .name = {name_start, name_length}};
  }

  DirectHandle<WasmExportedFunction> NextFunction() {
    DirectHandle<Object> maybe_func = NextFunctionInternal();
    if (maybe_func.is_null()) {
      ThrowWasmError(isolate_, MessageTemplate::kWasmTrapArrayOutOfBounds);
      return {};
    }
    DirectHandle<WasmFuncRef> funcref = Cast<WasmFuncRef>(maybe_func);
    DirectHandle<WasmInternalFunction> internal_function(
        funcref->internal(isolate_), isolate_);
    return Cast<WasmExportedFunction>(
        WasmInternalFunction::GetOrCreateExternal(internal_function));
  }

  DirectHandle<JSObject> NextPrototype() {
    DirectHandle<Object> maybe_proto = NextPrototypeInternal();
    if (maybe_proto.is_null()) {
      ThrowWasmError(isolate_, MessageTemplate::kWasmTrapArrayOutOfBounds);
      return {};
    }
    if (!IsWasmDescriptorOptions(*maybe_proto)) {
      ThrowWasmError(isolate_, MessageTemplate::kWasmTrapIllegalCast);
      return {};
    }
    return Cast<JSObject>(direct_handle(
        Cast<WasmDescriptorOptions>(maybe_proto)->prototype(), isolate_));
  }

  // Adding multiple properties is more efficient when the prototype
  // object is in dictionary mode. ICs will transition it back to
  // "fast" (but slow to modify) properties.
  void ToDictionaryMode(DirectHandle<JSReceiver> object, int num_properties) {
    if (!IsJSObject(*object) || !object->HasFastProperties()) return;
    JSObject::NormalizeProperties(isolate_, Cast<JSObject>(object),
                                  KEEP_INOBJECT_PROPERTIES, num_properties,
                                  "Wasm prototype setup");
  }

  bool InstallMethod(DirectHandle<JSReceiver> receiver, Method method,
                     DirectHandle<WasmExportedFunction> function) {
    if (!method.is_static) {
      WasmExportedFunction::MarkAsReceiverIsFirstParam(isolate_, function);
    }
    DirectHandle<String> name =
        isolate_->factory()->InternalizeUtf8String(method.name);
    PropertyDescriptor prop;
    prop.set_enumerable(false);
    prop.set_configurable(true);
    if (method.kind == Method::kMethod) {
      prop.set_writable(true);
      prop.set_value(function);
    } else if (method.kind == Method::kGetter) {
      prop.set_get(function);
    } else if (method.kind == Method::kSetter) {
      prop.set_set(function);
    } else {
      UNREACHABLE();  // Ruled out by validation.
    }
    return JSReceiver::DefineOwnProperty(isolate_, receiver, name, &prop,
                                         Just(ShouldThrow::kThrowOnError))
        .FromMaybe(false);
  }

  bool InstallConstructor(DirectHandle<JSReceiver> prototype,
                          DirectHandle<WasmExportedFunction> wasm_function,
                          DirectHandle<String> name,
                          DirectHandle<JSObject> all_constructors) {
    DirectHandle<Context> context = isolate_->factory()->NewBuiltinContext(
        isolate_->native_context(), wasm::kConstructorFunctionContextLength);
    context->SetNoCell(wasm::kConstructorFunctionContextSlot, *wasm_function);
    Builtin code = Builtin::kWasmConstructorWrapper;
    int length = wasm_function->length();
    DirectHandle<SharedFunctionInfo> sfi =
        isolate_->factory()->NewSharedFunctionInfoForBuiltin(name, code, length,
                                                             kDontAdapt);
    sfi->set_native(true);
    sfi->set_language_mode(LanguageMode::kStrict);
    DirectHandle<JSFunction> constructor =
        Factory::JSFunctionBuilder{isolate_, sfi, context}
            .set_map(isolate_->strict_function_with_readonly_prototype_map())
            .Build();
    constructor->set_prototype_or_initial_map(*prototype, kReleaseStore);
    prototype->map()->SetConstructor(*constructor);

    PropertyDescriptor prop;
    prop.set_enumerable(true);
    prop.set_configurable(true);
    prop.set_writable(true);
    prop.set_value(constructor);
    return JSReceiver::DefineOwnProperty(isolate_, all_constructors, name,
                                         &prop,
                                         Just(ShouldThrow::kThrowOnError))
        .FromMaybe(false);
  }

 protected:
  Isolate* isolate() { return isolate_; }
  virtual DirectHandle<Object> NextFunctionInternal() = 0;
  virtual DirectHandle<Object> NextPrototypeInternal() = 0;

 private:
  Isolate* isolate_;
};

class PrototypesSetup_Arrays : public PrototypesSetup {
 public:
  PrototypesSetup_Arrays(Isolate* isolate, const uint8_t* data_begin,
                         const uint8_t* data_end,
                         DirectHandle<WasmArray> prototypes,
                         DirectHandle<WasmArray> functions)
      : PrototypesSetup(isolate, data_begin, data_end),
        prototypes_(prototypes),
        functions_(functions) {}

 protected:
  DirectHandle<Object> NextFunctionInternal() override {
    if (function_index_ >= functions_->length()) return {};
    return WasmArray::GetElement(isolate(), functions_, function_index_++);
  }

  DirectHandle<Object> NextPrototypeInternal() override {
    if (prototype_index_ >= prototypes_->length()) return {};
    return WasmArray::GetElement(isolate(), prototypes_, prototype_index_++);
  }

 private:
  DirectHandle<WasmArray> prototypes_;
  DirectHandle<WasmArray> functions_;
  uint32_t prototype_index_{0};
  uint32_t function_index_{0};
};

class PrototypesSetup_Sections : public PrototypesSetup {
 public:
  PrototypesSetup_Sections(
      Isolate* isolate, const uint8_t* data_begin, const uint8_t* data_end,
      DirectHandle<FixedArray> prototypes, uint32_t prototypes_start_index,
      uint32_t prototypes_length, DirectHandle<FixedArray> functions,
      uint32_t functions_start_index, uint32_t functions_length)
      : PrototypesSetup(isolate, data_begin, data_end),
        prototypes_(prototypes),
        functions_(functions),
        prototype_index_(prototypes_start_index),
        prototypes_end_(prototypes_start_index + prototypes_length),
        function_index_(functions_start_index),
        functions_end_(functions_start_index + functions_length) {}

 protected:
  DirectHandle<Object> NextFunctionInternal() override {
    if (function_index_ >= functions_end_) return {};
    return direct_handle(functions_->get(function_index_++), isolate());
  }

  DirectHandle<Object> NextPrototypeInternal() override {
    if (prototype_index_ >= prototypes_end_) return {};
    return direct_handle(prototypes_->get(prototype_index_++), isolate());
  }

 private:
  DirectHandle<FixedArray> prototypes_;
  DirectHandle<FixedArray> functions_;
  uint32_t prototype_index_;
  uint32_t prototypes_end_;
  uint32_t function_index_;
  uint32_t functions_end_;
};

MaybeDirectHandle<FixedArray> GetElementSegment(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance,
    uint32_t segment_index) {
  Tagged<Object> segment_raw = instance->element_segments()->get(segment_index);
  if (IsFixedArray(segment_raw)) {
    return {Cast<FixedArray>(segment_raw), isolate};
  }

  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  DirectHandle<WasmTrustedInstanceData> shared_instance =
      instance->has_shared_part() ? DirectHandle<WasmTrustedInstanceData>(
                                        instance->shared_part(), isolate)
                                  : instance;
  std::optional<MessageTemplate> opt_error =
      wasm::InitializeElementSegment(&zone, isolate, instance, shared_instance,
                                     segment_index, wasm::kPrecreateExternal);
  if (opt_error.has_value()) {
    ThrowWasmError(isolate, opt_error.value());
    return {};
  }
  return {Cast<FixedArray>(instance->element_segments()->get(segment_index)),
          isolate};
}

}  // namespace

RUNTIME_FUNCTION(Runtime_WasmConfigureAllPrototypes) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());

  MessageTemplate illegal_cast = MessageTemplate::kWasmTrapIllegalCast;
  if (!IsWasmArray(args[0])) return ThrowWasmError(isolate, illegal_cast);
  if (!IsWasmArray(args[1])) return ThrowWasmError(isolate, illegal_cast);
  if (!IsWasmArray(args[2])) return ThrowWasmError(isolate, illegal_cast);
  if (!IsJSObject(args[3])) return ThrowWasmError(isolate, illegal_cast);
  DirectHandle<WasmArray> prototypes(Cast<WasmArray>(args[0]), isolate);
  DirectHandle<WasmArray> functions(Cast<WasmArray>(args[1]), isolate);
  DirectHandle<WasmArray> data(Cast<WasmArray>(args[2]), isolate);
  DirectHandle<JSObject> constructors(Cast<JSObject>(args[3]), isolate);
  {
    Tagged<Object> expected_prototypes_map =
        MakeStrong(isolate->heap()->wasm_canonical_rtts()->get(
            wasm::TypeCanonicalizer::kPredefinedArrayExternRefIndex.index));
    Tagged<Object> expected_functions_map =
        MakeStrong(isolate->heap()->wasm_canonical_rtts()->get(
            wasm::TypeCanonicalizer::kPredefinedArrayFuncRefIndex.index));
    Tagged<Object> expected_data_map =
        MakeStrong(isolate->heap()->wasm_canonical_rtts()->get(
            wasm::TypeCanonicalizer::kPredefinedArrayI8Index.index));
    if (prototypes->map() != expected_prototypes_map ||
        functions->map() != expected_functions_map ||
        data->map() != expected_data_map) {
      return ThrowWasmError(isolate, illegal_cast);
    }
  }

  // Arrays on the heap can move on GC, so we create an immovable copy of
  // the data we'll need to decode.
  uint32_t length = data->length();
  std::vector<uint8_t> immovable_data(size_t{length});
  memcpy(immovable_data.data(),
         reinterpret_cast<const uint8_t*>(data->ElementAddress(0)), length);

  PrototypesSetup_Arrays decoder(isolate, immovable_data.data(),
                                 immovable_data.data() + length, prototypes,
                                 functions);
  return decoder.SetupPrototypes(constructors);
}

RUNTIME_FUNCTION(Runtime_WasmConfigureAllPrototypesOpt) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());

  uint32_t* stack_buffer = reinterpret_cast<uint32_t*>(args[0].ptr());
  DirectHandle<JSObject> constructors(Cast<JSObject>(args[1]), isolate);
  DirectHandle<WasmTrustedInstanceData> instance(
      Cast<WasmTrustedInstanceData>(args[2]), isolate);

  uint32_t prototypes_start = stack_buffer[0];
  uint32_t prototypes_length = stack_buffer[1];
  uint32_t prototypes_segment_index = stack_buffer[2];
  uint32_t functions_start = stack_buffer[3];
  uint32_t functions_length = stack_buffer[4];
  uint32_t functions_segment_index = stack_buffer[5];
  uint32_t data_start = stack_buffer[6];
  uint32_t data_length = stack_buffer[7];
  uint32_t data_segment_index = stack_buffer[8];

  DirectHandle<FixedArray> prototypes_segment;
  if (!GetElementSegment(isolate, instance, prototypes_segment_index)
           .ToHandle(&prototypes_segment)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  if (!base::IsInBounds<size_t>(prototypes_start, prototypes_length,
                                prototypes_segment->length())) {
    return ThrowWasmError(isolate,
                          MessageTemplate::kWasmTrapElementSegmentOutOfBounds);
  }

  DirectHandle<FixedArray> functions_segment;
  if (!GetElementSegment(isolate, instance, functions_segment_index)
           .ToHandle(&functions_segment)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  if (!base::IsInBounds<size_t>(functions_start, functions_length,
                                functions_segment->length())) {
    return ThrowWasmError(isolate,
                          MessageTemplate::kWasmTrapElementSegmentOutOfBounds);
  }

  if (!base::IsInBounds<uint32_t>(
          data_start, data_length,
          instance->data_segment_sizes()->get(data_segment_index))) {
    return ThrowWasmError(isolate,
                          MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
  }
  Address data_start_address =
      instance->data_segment_starts()->get(data_segment_index) + data_start;
  PrototypesSetup_Sections decoder(
      isolate, reinterpret_cast<const uint8_t*>(data_start_address),
      reinterpret_cast<const uint8_t*>(data_start_address + data_length),
      prototypes_segment, prototypes_start, prototypes_length,
      functions_segment, functions_start, functions_length);
  return decoder.SetupPrototypes(constructors);
}

#define RETURN_RESULT_OR_TRAP(call)                                            \
  do {                                                                         \
    DirectHandle<Object> result;                                               \
    if (!(call).ToHandle(&result)) {                                           \
      DCHECK(isolate->has_exception());                                        \
      /* Mark any exception as uncatchable by Wasm. */                         \
      DirectHandle<JSObject> exception(Cast<JSObject>(isolate->exception()),   \
                                       isolate);                               \
      DirectHandle<Name> uncatchable =                                         \
          isolate->factory()->wasm_uncatchable_symbol();                       \
      LookupIterator it(isolate, exception, uncatchable, LookupIterator::OWN); \
      if (!JSReceiver::HasProperty(&it).FromJust()) {                          \
        JSObject::AddProperty(isolate, exception, uncatchable,                 \
                              isolate->factory()->true_value(), NONE);         \
      }                                                                        \
      return ReadOnlyRoots(isolate).exception();                               \
    }                                                                          \
    DCHECK(!isolate->has_exception() ||                                        \
           IsTerminationException(isolate->exception()));                      \
    return *result;                                                            \
  } while (false)

// "Special" because the type must be in a recgroup of its own.
// Used by "JS String Builtins".
RUNTIME_FUNCTION(Runtime_WasmCastToSpecialPrimitiveArray) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());

  int bits = args.smi_value_at(1);
  DCHECK(bits == 8 || bits == 16);

  if (args[0] == ReadOnlyRoots(isolate).null_value()) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapNullDereference);
  }
  MessageTemplate illegal_cast = MessageTemplate::kWasmTrapIllegalCast;
  if (!IsWasmArray(args[0])) return ThrowWasmError(isolate, illegal_cast);
  Tagged<WasmArray> obj = Cast<WasmArray>(args[0]);
  wasm::CanonicalTypeIndex expected =
      bits == 8 ? wasm::TypeCanonicalizer::kPredefinedArrayI8Index
                : wasm::TypeCanonicalizer::kPredefinedArrayI16Index;
  Tagged<Object> expected_map =
      MakeStrong(isolate->heap()->wasm_canonical_rtts()->get(expected.index));
  // If the expected_map has been cleared or never even created, then there's
  // no chance of a match anyway.
  if (obj->map() != expected_map) {
    return ThrowWasmError(isolate, illegal_cast);
  }
  return obj;
}

// Returns the new string if the operation succeeds.  Otherwise throws an
// exception and returns an empty result.
RUNTIME_FUNCTION(Runtime_WasmStringNewWtf8) {
  DCHECK_EQ(5, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  uint32_t utf8_variant_value = args.positive_smi_value_at(2);
  double offset_double = args.number_value_at(3);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  uint32_t size = NumberToUint32(args[4]);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));

  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);

  uint64_t mem_size = trusted_instance_data->memory_size(memory);
  if (!base::IsInBounds<uint64_t>(offset, size, mem_size)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }

  const base::Vector<const uint8_t> bytes{
      trusted_instance_data->memory_base(memory) + offset, size};
  MaybeDirectHandle<v8::internal::String> result_string =
      isolate->factory()->NewStringFromUtf8(bytes, utf8_variant);
  if (utf8_variant == unibrow::Utf8Variant::kUtf8NoTrap) {
    // If the input was invalid, then the decoder has failed silently, and
    // the string.new_utf8_try instruction should return null.
    if (result_string.is_null() && !isolate->has_exception()) {
      return *isolate->factory()->wasm_null();
    }
    // Fall through in case of a valid result, and in case of a pending
    // exception because the requested string was too large.
  }
  RETURN_RESULT_OR_TRAP(result_string);
}

RUNTIME_FUNCTION(Runtime_WasmStringNewWtf8Array) {
  DCHECK_EQ(4, args.length());
  HandleScope scope(isolate);
  uint32_t utf8_variant_value = args.positive_smi_value_at(0);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[1]), isolate);
  uint32_t start = NumberToUint32(args[2]);
  uint32_t end = NumberToUint32(args[3]);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));
  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);

  MaybeDirectHandle<v8::internal::String> result_string =
      isolate->factory()->NewStringFromUtf8(array, start, end, utf8_variant);
  if (utf8_variant == unibrow::Utf8Variant::kUtf8NoTrap) {
    DCHECK(!isolate->has_exception());
    if (result_string.is_null()) {
      return *isolate->factory()->wasm_null();
    }
    return *result_string.ToHandleChecked();
  }
  RETURN_RESULT_OR_TRAP(result_string);
}

RUNTIME_FUNCTION(Runtime_WasmStringNewWtf16) {
  DCHECK_EQ(4, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  double offset_double = args.number_value_at(2);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  uint32_t size_in_codeunits = NumberToUint32(args[3]);

  uint64_t mem_size = trusted_instance_data->memory_size(memory);
  if (size_in_codeunits > kMaxUInt32 / 2 ||
      !base::IsInBounds<uint64_t>(offset, size_in_codeunits * 2, mem_size)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }
  if (offset & 1) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapUnalignedAccess);
  }

  const uint8_t* bytes = trusted_instance_data->memory_base(memory) + offset;
  const base::uc16* codeunits = reinterpret_cast<const base::uc16*>(bytes);
  RETURN_RESULT_OR_TRAP(isolate->factory()->NewStringFromTwoByteLittleEndian(
      {codeunits, size_in_codeunits}));
}

RUNTIME_FUNCTION(Runtime_WasmStringNewWtf16Array) {
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[0]), isolate);
  uint32_t start = NumberToUint32(args[1]);
  uint32_t end = NumberToUint32(args[2]);

  RETURN_RESULT_OR_TRAP(
      isolate->factory()->NewStringFromUtf16(array, start, end));
}

RUNTIME_FUNCTION(Runtime_WasmSubstring) {
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  DirectHandle<String> string(Cast<String>(args[0]), isolate);
  int start = args.positive_smi_value_at(1);
  int length = args.positive_smi_value_at(2);

  string = String::Flatten(isolate, string);
  return *isolate->factory()->NewCopiedSubstring(string, start, length);
}

// Returns the new string if the operation succeeds.  Otherwise traps.
RUNTIME_FUNCTION(Runtime_WasmStringConst) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  static_assert(
      base::IsInRange(wasm::kV8MaxWasmStringLiterals, 0, Smi::kMaxValue));
  uint32_t index = args.positive_smi_value_at(1);

  DCHECK_LT(index, trusted_instance_data->module()->stringref_literals.size());

  const wasm::WasmStringRefLiteral& literal =
      trusted_instance_data->module()->stringref_literals[index];
  const base::Vector<const uint8_t> module_bytes =
      trusted_instance_data->native_module()->wire_bytes();
  const base::Vector<const uint8_t> string_bytes = module_bytes.SubVector(
      literal.source.offset(), literal.source.end_offset());
  // TODO(12868): No need to re-validate WTF-8.  Also, result should be cached.
  return *isolate->factory()
              ->NewStringFromUtf8(string_bytes, unibrow::Utf8Variant::kWtf8)
              .ToHandleChecked();
}

RUNTIME_FUNCTION(Runtime_WasmStringNewSegmentWtf8) {
  DCHECK_EQ(5, args.length());
  HandleScope scope(isolate);
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t segment_index = args.positive_smi_value_at(1);
  uint32_t offset = args.positive_smi_value_at(2);
  uint32_t length = args.positive_smi_value_at(3);
  unibrow::Utf8Variant variant =
      static_cast<unibrow::Utf8Variant>(args.positive_smi_value_at(4));

  if (!base::IsInBounds<uint32_t>(
          offset, length,
          trusted_instance_data->data_segment_sizes()->get(segment_index))) {
    return ThrowWasmError(isolate,
                          MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
  }

  Address source =
      trusted_instance_data->data_segment_starts()->get(segment_index) + offset;
  MaybeDirectHandle<String> result = isolate->factory()->NewStringFromUtf8(
      {reinterpret_cast<const uint8_t*>(source), length}, variant);
  if (variant == unibrow::Utf8Variant::kUtf8NoTrap) {
    DCHECK(!isolate->has_exception());
    // Only instructions from the stringref proposal can set variant
    // kUtf8NoTrap, so WasmNull is appropriate here.
    if (result.is_null()) return *isolate->factory()->wasm_null();
    return *result.ToHandleChecked();
  }
  RETURN_RESULT_OR_FAILURE(isolate, result);
}

namespace {
// TODO(12868): Consider unifying with api.cc:String::Utf8Length.
template <typename T>
int MeasureWtf8(base::Vector<const T> wtf16) {
  int previous = unibrow::Utf16::kNoPreviousCharacter;
  int length = 0;
  DCHECK(wtf16.size() <= String::kMaxLength);
  static_assert(String::kMaxLength <=
                (kMaxInt / unibrow::Utf8::kMaxEncodedSize));
  for (size_t i = 0; i < wtf16.size(); i++) {
    int current = wtf16[i];
    length += unibrow::Utf8::Length(current, previous);
    previous = current;
  }
  return length;
}
int MeasureWtf8(Isolate* isolate, DirectHandle<String> string) {
  string = String::Flatten(isolate, string);
  DisallowGarbageCollection no_gc;
  String::FlatContent content = string->GetFlatContent(no_gc);
  DCHECK(content.IsFlat());
  return content.IsOneByte() ? MeasureWtf8(content.ToOneByteVector())
                             : MeasureWtf8(content.ToUC16Vector());
}
size_t MaxEncodedSize(base::Vector<const uint8_t> wtf16) {
  DCHECK(wtf16.size() < std::numeric_limits<size_t>::max() /
                            unibrow::Utf8::kMax8BitCodeUnitSize);
  return wtf16.size() * unibrow::Utf8::kMax8BitCodeUnitSize;
}
size_t MaxEncodedSize(base::Vector<const base::uc16> wtf16) {
  DCHECK(wtf16.size() < std::numeric_limits<size_t>::max() /
                            unibrow::Utf8::kMax16BitCodeUnitSize);
  return wtf16.size() * unibrow::Utf8::kMax16BitCodeUnitSize;
}
bool HasUnpairedSurrogate(base::Vector<const uint8_t> wtf16) { return false; }
bool HasUnpairedSurrogate(base::Vector<const base::uc16> wtf16) {
  return unibrow::Utf16::HasUnpairedSurrogate(wtf16.begin(), wtf16.size());
}
template <typename T>
int EncodeWtf8(base::Vector<char> bytes, size_t offset,
               base::Vector<const T> wtf16, unibrow::Utf8Variant variant,
               MessageTemplate* message, MessageTemplate out_of_bounds) {
  // The first check is a quick estimate to decide whether the second check
  // is worth the computation.
  if (!base::IsInBounds<size_t>(offset, MaxEncodedSize(wtf16), bytes.size()) &&
      !base::IsInBounds<size_t>(offset, MeasureWtf8(wtf16), bytes.size())) {
    *message = out_of_bounds;
    return -1;
  }

  bool replace_invalid = false;
  switch (variant) {
    case unibrow::Utf8Variant::kWtf8:
      break;
    case unibrow::Utf8Variant::kUtf8:
      if (HasUnpairedSurrogate(wtf16)) {
        *message = MessageTemplate::kWasmTrapStringIsolatedSurrogate;
        return -1;
      }
      break;
    case unibrow::Utf8Variant::kLossyUtf8:
      replace_invalid = true;
      break;
    default:
      UNREACHABLE();
  }

  bool write_null = false;
  unibrow::Utf8::EncodingResult result =
      unibrow::Utf8::Encode(wtf16, bytes.begin() + offset,
                            bytes.size() - offset, write_null, replace_invalid);
  DCHECK_EQ(result.characters_processed, wtf16.size());
  DCHECK_LE(result.bytes_written, kMaxInt);
  return static_cast<int>(result.bytes_written);
}
template <typename GetWritableBytes>
Tagged<Object> EncodeWtf8(Isolate* isolate, unibrow::Utf8Variant variant,
                          DirectHandle<String> string,
                          GetWritableBytes get_writable_bytes, size_t offset,
                          MessageTemplate out_of_bounds_message) {
  string = String::Flatten(isolate, string);
  MessageTemplate message;
  int written;
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent content = string->GetFlatContent(no_gc);
    base::Vector<char> dst = get_writable_bytes(no_gc);
    written = content.IsOneByte()
                  ? EncodeWtf8(dst, offset, content.ToOneByteVector(), variant,
                               &message, out_of_bounds_message)
                  : EncodeWtf8(dst, offset, content.ToUC16Vector(), variant,
                               &message, out_of_bounds_message);
  }
  if (written < 0) {
    DCHECK_NE(message, MessageTemplate::kNone);
    return ThrowWasmError(isolate, message);
  }
  return *isolate->factory()->NewNumberFromInt(written);
}
}  // namespace

// Used for storing the name of a string-constants imports module off the heap.
// Defined here to be able to make use of the helper functions above.
void ToUtf8Lossy(Isolate* isolate, DirectHandle<String> string,
                 std::string& out) {
  int utf8_length = MeasureWtf8(isolate, string);
  DisallowGarbageCollection no_gc;
  out.resize(utf8_length);
  String::FlatContent content = string->GetFlatContent(no_gc);
  DCHECK(content.IsFlat());
  static constexpr unibrow::Utf8Variant variant =
      unibrow::Utf8Variant::kLossyUtf8;
  MessageTemplate* error_cant_happen = nullptr;
  MessageTemplate oob_cant_happen = MessageTemplate::kInvalid;
  if (content.IsOneByte()) {
    EncodeWtf8({out.data(), out.size()}, 0, content.ToOneByteVector(), variant,
               error_cant_happen, oob_cant_happen);
  } else {
    EncodeWtf8({out.data(), out.size()}, 0, content.ToUC16Vector(), variant,
               error_cant_happen, oob_cant_happen);
  }
}

RUNTIME_FUNCTION(Runtime_WasmStringMeasureUtf8) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  DirectHandle<String> string(Cast<String>(args[0]), isolate);

  string = String::Flatten(isolate, string);
  int length;
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent content = string->GetFlatContent(no_gc);
    DCHECK(content.IsFlat());
    if (content.IsOneByte()) {
      length = MeasureWtf8(content.ToOneByteVector());
    } else {
      base::Vector<const base::uc16> code_units = content.ToUC16Vector();
      if (unibrow::Utf16::HasUnpairedSurrogate(code_units.begin(),
                                               code_units.size())) {
        length = -1;
      } else {
        length = MeasureWtf8(code_units);
      }
    }
  }
  return *isolate->factory()->NewNumberFromInt(length);
}

RUNTIME_FUNCTION(Runtime_WasmStringMeasureWtf8) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  DirectHandle<String> string(Cast<String>(args[0]), isolate);

  int length = MeasureWtf8(isolate, string);
  return *isolate->factory()->NewNumberFromInt(length);
}

RUNTIME_FUNCTION(Runtime_WasmStringEncodeWtf8) {
  DCHECK_EQ(5, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  uint32_t utf8_variant_value = args.positive_smi_value_at(2);
  DirectHandle<String> string(Cast<String>(args[3]), isolate);
  double offset_double = args.number_value_at(4);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));

  char* memory_start =
      reinterpret_cast<char*>(trusted_instance_data->memory_base(memory));
  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {memory_start, trusted_instance_data->memory_size(memory)};
  };
  return EncodeWtf8(isolate, utf8_variant, string, get_writable_bytes, offset,
                    MessageTemplate::kWasmTrapMemOutOfBounds);
}

RUNTIME_FUNCTION(Runtime_WasmStringEncodeWtf8Array) {
  DCHECK_EQ(4, args.length());
  HandleScope scope(isolate);
  uint32_t utf8_variant_value = args.positive_smi_value_at(0);
  DirectHandle<String> string(Cast<String>(args[1]), isolate);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[2]), isolate);
  uint32_t start = NumberToUint32(args[3]);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));
  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {reinterpret_cast<char*>(array->ElementAddress(0)), array->length()};
  };
  return EncodeWtf8(isolate, utf8_variant, string, get_writable_bytes, start,
                    MessageTemplate::kWasmTrapArrayOutOfBounds);
}

RUNTIME_FUNCTION(Runtime_WasmStringToUtf8Array) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  DirectHandle<String> string(Cast<String>(args[0]), isolate);
  uint32_t length = MeasureWtf8(isolate, string);
  wasm::WasmValue initial_value(int8_t{0});
  Tagged<WeakFixedArray> rtts = isolate->heap()->wasm_canonical_rtts();
  // This function can only get called from Wasm code, so we can safely assume
  // that the canonical RTT is still around.
  DirectHandle<Map> map(
      Cast<Map>(
          rtts->get(wasm::TypeCanonicalizer::kPredefinedArrayI8Index.index)
              .GetHeapObjectAssumeWeak()),
      isolate);
  DirectHandle<WasmArray> array = isolate->factory()->NewWasmArray(
      wasm::kWasmI8, length, initial_value, map);
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {reinterpret_cast<char*>(array->ElementAddress(0)), length};
  };
  Tagged<Object> write_result =
      EncodeWtf8(isolate, unibrow::Utf8Variant::kLossyUtf8, string,
                 get_writable_bytes, 0, MessageTemplate::kNone);
  DCHECK(IsNumber(write_result) && Object::NumberValue(write_result) == length);
  USE(write_result);
  return *array;
}

RUNTIME_FUNCTION(Runtime_WasmStringEncodeWtf16) {
  DCHECK_EQ(6, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  Tagged<String> string = Cast<String>(args[2]);
  double offset_double = args.number_value_at(3);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  uint32_t start = args.positive_smi_value_at(4);
  uint32_t length = args.positive_smi_value_at(5);

  DCHECK(base::IsInBounds<uint32_t>(start, length, string->length()));

  size_t mem_size = trusted_instance_data->memory_size(memory);
  static_assert(String::kMaxLength <=
                (std::numeric_limits<size_t>::max() / sizeof(base::uc16)));
  if (!base::IsInBounds<size_t>(offset, length * sizeof(base::uc16),
                                mem_size)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }
  if (offset & 1) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapUnalignedAccess);
  }

#if defined(V8_TARGET_LITTLE_ENDIAN)
  uint16_t* dst = reinterpret_cast<uint16_t*>(
      trusted_instance_data->memory_base(memory) + offset);
  String::WriteToFlat(string, dst, start, length);
#elif defined(V8_TARGET_BIG_ENDIAN)
  // TODO(12868): The host is big-endian but we need to write the string
  // contents as little-endian.
  USE(string);
  USE(start);
  UNIMPLEMENTED();
#else
#error Unknown endianness
#endif

  return Smi::zero();  // Unused.
}

RUNTIME_FUNCTION(Runtime_WasmStringAsWtf8) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  DirectHandle<String> string(Cast<String>(args[0]), isolate);
  int wtf8_length = MeasureWtf8(isolate, string);
  DirectHandle<ByteArray> array = isolate->factory()->NewByteArray(wtf8_length);

  auto utf8_variant = unibrow::Utf8Variant::kWtf8;
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {reinterpret_cast<char*>(array->begin()),
            static_cast<size_t>(wtf8_length)};
  };
  EncodeWtf8(isolate, utf8_variant, string, get_writable_bytes, 0,
             MessageTemplate::kWasmTrapArrayOutOfBounds);
  return *array;
}

RUNTIME_FUNCTION(Runtime_WasmStringViewWtf8Encode) {
  DCHECK_EQ(7, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t utf8_variant_value = args.positive_smi_value_at(1);
  DirectHandle<ByteArray> array(Cast<ByteArray>(args[2]), isolate);
  double addr_double = args.number_value_at(3);
  uintptr_t addr = static_cast<uintptr_t>(addr_double);
  uint32_t start = NumberToUint32(args[4]);
  uint32_t end = NumberToUint32(args[5]);
  uint32_t memory = args.positive_smi_value_at(6);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));
  DCHECK_LE(start, end);
  DCHECK(base::IsInBounds<size_t>(start, end - start, array->length()));

  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);
  size_t length = end - start;

  if (!base::IsInBounds<size_t>(addr, length,
                                trusted_instance_data->memory_size(memory))) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }

  uint8_t* memory_start = trusted_instance_data->memory_base(memory);
  const uint8_t* src = reinterpret_cast<const uint8_t*>(array->begin() + start);
  uint8_t* dst = memory_start + addr;

  std::vector<size_t> surrogates;
  if (utf8_variant != unibrow::Utf8Variant::kWtf8) {
    unibrow::Wtf8::ScanForSurrogates({src, length}, &surrogates);
    if (utf8_variant == unibrow::Utf8Variant::kUtf8 && !surrogates.empty()) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapStringIsolatedSurrogate);
    }
  }

  MemCopy(dst, src, length);

  for (size_t surrogate : surrogates) {
    DCHECK_LT(surrogate, length);
    DCHECK_EQ(utf8_variant, unibrow::Utf8Variant::kLossyUtf8);
    unibrow::Utf8::Encode(reinterpret_cast<char*>(dst + surrogate),
                          unibrow::Utf8::kBadChar, 0, false);
  }

  // Unused.
  return Tagged<Smi>(0);
}

RUNTIME_FUNCTION(Runtime_WasmStringViewWtf8Slice) {
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  DirectHandle<ByteArray> array(Cast<ByteArray>(args[0]), isolate);
  uint32_t start = NumberToUint32(args[1]);
  uint32_t end = NumberToUint32(args[2]);

  DCHECK_LT(start, end);
  DCHECK(base::IsInBounds<size_t>(start, end - start, array->length()));

  // This can't throw because the result can't be too long if the input wasn't,
  // and encoding failures are ruled out too because {start}/{end} are aligned.
  return *isolate->factory()
              ->NewStringFromUtf8(array, start, end,
                                  unibrow::Utf8Variant::kWtf8)
              .ToHandleChecked();
}

#ifdef V8_ENABLE_DRUMBRAKE
RUNTIME_FUNCTION(Runtime_WasmTraceBeginExecution) {
  DCHECK(v8_flags.slow_histograms && !v8_flags.wasm_jitless &&
         v8_flags.wasm_enable_exec_time_histograms);
  DCHECK_EQ(0, args.length());
  HandleScope scope(isolate);

  wasm::WasmExecutionTimer* timer = isolate->wasm_execution_timer();
  timer->Start();

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTraceEndExecution) {
  DCHECK(v8_flags.slow_histograms && !v8_flags.wasm_jitless &&
         v8_flags.wasm_enable_exec_time_histograms);
  DCHECK_EQ(0, args.length());
  HandleScope scope(isolate);

  wasm::WasmExecutionTimer* timer = isolate->wasm_execution_timer();
  timer->Stop();

  return ReadOnlyRoots(isolate).undefined_value();
}
#endif  // V8_ENABLE_DRUMBRAKE

RUNTIME_FUNCTION(Runtime_WasmStringFromCodePoint) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);

  uint32_t code_point = NumberToUint32(args[0]);
  if (code_point <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
    return *isolate->factory()->LookupSingleCharacterStringFromCode(code_point);
  }
  if (code_point > 0x10FFFF) {
    // Allocate a new number to preserve the to-uint conversion (e.g. if
    // args[0] == -1, we want the error message to report 4294967295).
    return ThrowWasmError(isolate, MessageTemplate::kInvalidCodePoint,
                          {isolate->factory()->NewNumberFromUint(code_point)});
  }

  base::uc16 char_buffer[] = {
      unibrow::Utf16::LeadSurrogate(code_point),
      unibrow::Utf16::TrailSurrogate(code_point),
  };
  DirectHandle<SeqTwoByteString> result =
      isolate->factory()
          ->NewRawTwoByteString(arraysize(char_buffer))
          .ToHandleChecked();
  DisallowGarbageCollection no_gc;
  CopyChars(result->GetChars(no_gc), char_buffer, arraysize(char_buffer));
  return *result;
}

RUNTIME_FUNCTION(Runtime_WasmStringHash) {
  DCHECK_EQ(1, args.length());
  Tagged<String> string(Cast<String>(args[0]));
  uint32_t hash = string->EnsureHash();
  return Smi::FromInt(static_cast<int>(hash));
}

}  // namespace v8::internal
