// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-promise.h"

namespace v8 {
namespace internal {

class AsyncFunctionBuiltinsAssembler : public AsyncBuiltinsAssembler {
 public:
  explicit AsyncFunctionBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : AsyncBuiltinsAssembler(state) {}

 protected:
  void AsyncFunctionAwait(Node* const context, Node* const generator,
                          Node* const awaited, Node* const outer_promise,
                          const bool is_predicted_as_caught);

  void AsyncFunctionAwaitResumeClosure(
      Node* const context, Node* const sent_value,
      JSGeneratorObject::ResumeMode resume_mode);
};

void AsyncFunctionBuiltinsAssembler::AsyncFunctionAwaitResumeClosure(
    Node* context, Node* sent_value,
    JSGeneratorObject::ResumeMode resume_mode) {
  DCHECK(resume_mode == JSGeneratorObject::kNext ||
         resume_mode == JSGeneratorObject::kThrow);

  Node* const generator = LoadContextElement(context, Context::EXTENSION_INDEX);
  CSA_SLOW_ASSERT(this, HasInstanceType(generator, JS_GENERATOR_OBJECT_TYPE));

  // Inline version of GeneratorPrototypeNext / GeneratorPrototypeReturn with
  // unnecessary runtime checks removed.
  // TODO(jgruber): Refactor to reuse code from builtins-generator.cc.

  // Ensure that the generator is neither closed nor running.
  CSA_SLOW_ASSERT(
      this,
      SmiGreaterThan(CAST(LoadObjectField(
                         generator, JSGeneratorObject::kContinuationOffset)),
                     SmiConstant(JSGeneratorObject::kGeneratorClosed)));

  // Remember the {resume_mode} for the {generator}.
  StoreObjectFieldNoWriteBarrier(generator,
                                 JSGeneratorObject::kResumeModeOffset,
                                 SmiConstant(resume_mode));

  // Resume the {receiver} using our trampoline.
  Callable callable = CodeFactory::ResumeGenerator(isolate());
  CallStub(callable, context, sent_value, generator);

  // The resulting Promise is a throwaway, so it doesn't matter what it
  // resolves to. What is important is that we don't end up keeping the
  // whole chain of intermediate Promises alive by returning the return value
  // of ResumeGenerator, as that would create a memory leak.
}

TF_BUILTIN(AsyncFunctionEnter, AsyncFunctionBuiltinsAssembler) {
  TNode<JSFunction> closure = CAST(Parameter(Descriptor::kClosure));
  TNode<Object> receiver = CAST(Parameter(Descriptor::kReceiver));
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));

  // Compute the number of registers and parameters.
  TNode<SharedFunctionInfo> shared = LoadObjectField<SharedFunctionInfo>(
      closure, JSFunction::kSharedFunctionInfoOffset);
  TNode<IntPtrT> formal_parameter_count = ChangeInt32ToIntPtr(
      LoadObjectField(shared, SharedFunctionInfo::kFormalParameterCountOffset,
                      MachineType::Uint16()));
  TNode<BytecodeArray> bytecode_array =
      LoadSharedFunctionInfoBytecodeArray(shared);
  TNode<IntPtrT> frame_size = ChangeInt32ToIntPtr(LoadObjectField(
      bytecode_array, BytecodeArray::kFrameSizeOffset, MachineType::Int32()));
  TNode<IntPtrT> parameters_and_register_length =
      Signed(IntPtrAdd(WordSar(frame_size, IntPtrConstant(kPointerSizeLog2)),
                       formal_parameter_count));

  // Allocate space for both the generator object and the register file.
  TNode<WordT> size = IntPtrAdd(
      IntPtrConstant(JSGeneratorObject::kSize + FixedArray::kHeaderSize),
      WordShl(parameters_and_register_length,
              IntPtrConstant(kPointerSizeLog2)));
  Node* base = AllocateInNewSpace(size);

  // Initialize the register file.
  TNode<FixedArray> parameters_and_registers =
      UncheckedCast<FixedArray>(InnerAllocate(base, JSGeneratorObject::kSize));
  StoreMapNoWriteBarrier(parameters_and_registers, RootIndex::kFixedArrayMap);
  StoreObjectFieldNoWriteBarrier(parameters_and_registers,
                                 FixedArray::kLengthOffset,
                                 SmiFromIntPtr(parameters_and_register_length));
  FillFixedArrayWithValue(HOLEY_ELEMENTS, parameters_and_registers,
                          IntPtrConstant(0), parameters_and_register_length,
                          RootIndex::kUndefinedValue);

  // Initialize the async function object.
  TNode<Context> native_context = LoadNativeContext(context);
  TNode<Map> async_function_object_map = CAST(LoadContextElement(
      native_context, Context::ASYNC_FUNCTION_OBJECT_MAP_INDEX));
  TNode<JSGeneratorObject> async_function_object =
      UncheckedCast<JSGeneratorObject>(base);
  StoreMapNoWriteBarrier(async_function_object, async_function_object_map);
  StoreObjectFieldRoot(async_function_object,
                       JSGeneratorObject::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(async_function_object,
                       JSGeneratorObject::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSGeneratorObject::kFunctionOffset, closure);
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSGeneratorObject::kContextOffset, context);
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSGeneratorObject::kReceiverOffset, receiver);
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSGeneratorObject::kInputOrDebugPosOffset,
                                 SmiConstant(0));
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSGeneratorObject::kResumeModeOffset,
                                 SmiConstant(JSGeneratorObject::kNext));
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSGeneratorObject::kContinuationOffset,
      SmiConstant(JSGeneratorObject::kGeneratorExecuting));
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSGeneratorObject::kParametersAndRegistersOffset,
      parameters_and_registers);

  Return(async_function_object);
}

TF_BUILTIN(AsyncFunctionReject, AsyncFunctionBuiltinsAssembler) {
  TNode<JSPromise> promise = CAST(Parameter(Descriptor::kPromise));
  TNode<Object> reason = CAST(Parameter(Descriptor::kReason));
  TNode<Oddball> can_suspend = CAST(Parameter(Descriptor::kCanSuspend));
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));

  // Reject the {promise} for the given {reason}, disabling the
  // additional debug event for the rejection since a debug event
  // already happend for the exception that got us here.
  CallBuiltin(Builtins::kRejectPromise, context, promise, reason,
              FalseConstant());

  Label if_debugging(this, Label::kDeferred);
  GotoIf(HasAsyncEventDelegate(), &if_debugging);
  GotoIf(IsDebugActive(), &if_debugging);
  Return(promise);

  BIND(&if_debugging);
  TailCallRuntime(Runtime::kDebugAsyncFunctionFinished, context, can_suspend,
                  promise);
}

TF_BUILTIN(AsyncFunctionResolve, AsyncFunctionBuiltinsAssembler) {
  TNode<JSPromise> promise = CAST(Parameter(Descriptor::kPromise));
  TNode<Object> value = CAST(Parameter(Descriptor::kValue));
  TNode<Oddball> can_suspend = CAST(Parameter(Descriptor::kCanSuspend));
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));

  CallBuiltin(Builtins::kResolvePromise, context, promise, value);

  Label if_debugging(this, Label::kDeferred);
  GotoIf(HasAsyncEventDelegate(), &if_debugging);
  GotoIf(IsDebugActive(), &if_debugging);
  Return(promise);

  BIND(&if_debugging);
  TailCallRuntime(Runtime::kDebugAsyncFunctionFinished, context, can_suspend,
                  promise);
}

// AsyncFunctionReject and AsyncFunctionResolve are both required to return
// the promise instead of the result of RejectPromise or ResolvePromise
// respectively from a lazy deoptimization.
TF_BUILTIN(AsyncFunctionLazyDeoptContinuation, AsyncFunctionBuiltinsAssembler) {
  TNode<JSPromise> promise = CAST(Parameter(Descriptor::kPromise));
  Return(promise);
}

TF_BUILTIN(AsyncFunctionAwaitRejectClosure, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 1);
  Node* const sentError = Parameter(Descriptor::kSentError);
  Node* const context = Parameter(Descriptor::kContext);

  AsyncFunctionAwaitResumeClosure(context, sentError,
                                  JSGeneratorObject::kThrow);
  Return(UndefinedConstant());
}

TF_BUILTIN(AsyncFunctionAwaitResolveClosure, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 1);
  Node* const sentValue = Parameter(Descriptor::kSentValue);
  Node* const context = Parameter(Descriptor::kContext);

  AsyncFunctionAwaitResumeClosure(context, sentValue, JSGeneratorObject::kNext);
  Return(UndefinedConstant());
}

// ES#abstract-ops-async-function-await
// AsyncFunctionAwait ( value )
// Shared logic for the core of await. The parser desugars
//   await awaited
// into
//   yield AsyncFunctionAwait{Caught,Uncaught}(.generator, awaited, .promise)
// The 'awaited' parameter is the value; the generator stands in
// for the asyncContext, and .promise is the larger promise under
// construction by the enclosing async function.
void AsyncFunctionBuiltinsAssembler::AsyncFunctionAwait(
    Node* const context, Node* const generator, Node* const awaited,
    Node* const outer_promise, const bool is_predicted_as_caught) {
  CSA_SLOW_ASSERT(this, HasInstanceType(generator, JS_GENERATOR_OBJECT_TYPE));
  CSA_SLOW_ASSERT(this, HasInstanceType(outer_promise, JS_PROMISE_TYPE));

  // TODO(jgruber): AsyncBuiltinsAssembler::Await currently does not reuse
  // the awaited promise if it is already a promise. Reuse is non-spec compliant
  // but part of our old behavior gives us a couple of percent
  // performance boost.
  // TODO(jgruber): Use a faster specialized version of
  // InternalPerformPromiseThen.

  Label after_debug_hook(this), call_debug_hook(this, Label::kDeferred);
  GotoIf(HasAsyncEventDelegate(), &call_debug_hook);
  Goto(&after_debug_hook);
  BIND(&after_debug_hook);

  Await(context, generator, awaited, outer_promise,
        Context::ASYNC_FUNCTION_AWAIT_RESOLVE_SHARED_FUN,
        Context::ASYNC_FUNCTION_AWAIT_REJECT_SHARED_FUN,
        is_predicted_as_caught);

  // Return outer promise to avoid adding an load of the outer promise before
  // suspending in BytecodeGenerator.
  Return(outer_promise);

  BIND(&call_debug_hook);
  CallRuntime(Runtime::kDebugAsyncFunctionSuspended, context, outer_promise);
  Goto(&after_debug_hook);
}

// Called by the parser from the desugaring of 'await' when catch
// prediction indicates that there is a locally surrounding catch block.
TF_BUILTIN(AsyncFunctionAwaitCaught, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 3);
  Node* const generator = Parameter(Descriptor::kGenerator);
  Node* const awaited = Parameter(Descriptor::kAwaited);
  Node* const outer_promise = Parameter(Descriptor::kOuterPromise);
  Node* const context = Parameter(Descriptor::kContext);

  static const bool kIsPredictedAsCaught = true;

  AsyncFunctionAwait(context, generator, awaited, outer_promise,
                     kIsPredictedAsCaught);
}

// Called by the parser from the desugaring of 'await' when catch
// prediction indicates no locally surrounding catch block.
TF_BUILTIN(AsyncFunctionAwaitUncaught, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 3);
  Node* const generator = Parameter(Descriptor::kGenerator);
  Node* const awaited = Parameter(Descriptor::kAwaited);
  Node* const outer_promise = Parameter(Descriptor::kOuterPromise);
  Node* const context = Parameter(Descriptor::kContext);

  static const bool kIsPredictedAsCaught = false;

  AsyncFunctionAwait(context, generator, awaited, outer_promise,
                     kIsPredictedAsCaught);
}

TF_BUILTIN(AsyncFunctionPromiseCreate, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 0);
  Node* const context = Parameter(Descriptor::kContext);

  Node* const promise = AllocateAndInitJSPromise(context);

  Label if_is_debug_active(this, Label::kDeferred);
  GotoIf(IsDebugActive(), &if_is_debug_active);

  // Early exit if debug is not active.
  Return(promise);

  BIND(&if_is_debug_active);
  {
    // Push the Promise under construction in an async function on
    // the catch prediction stack to handle exceptions thrown before
    // the first await.
    CallRuntime(Runtime::kDebugPushPromise, context, promise);
    Return(promise);
  }
}

}  // namespace internal
}  // namespace v8
