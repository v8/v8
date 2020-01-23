// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-promise-gen.h"

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-promise.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/objects/fixed-array.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

using Node = compiler::Node;
using IteratorRecord = TorqueStructIteratorRecord;
using PromiseResolvingFunctions = TorqueStructPromiseResolvingFunctions;

TNode<JSPromise> PromiseBuiltinsAssembler::AllocateJSPromise(
    TNode<Context> context) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<JSFunction> promise_fun =
      CAST(LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX));
  CSA_ASSERT(this, IsFunctionWithPrototypeSlotMap(LoadMap(promise_fun)));
  const TNode<Map> promise_map = LoadObjectField<Map>(
      promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  const TNode<HeapObject> promise =
      Allocate(JSPromise::kSizeWithEmbedderFields);
  StoreMapNoWriteBarrier(promise, promise_map);
  StoreObjectFieldRoot(promise, JSPromise::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(promise, JSPromise::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  return CAST(promise);
}

void PromiseBuiltinsAssembler::PromiseInit(TNode<JSPromise> promise) {
  STATIC_ASSERT(v8::Promise::kPending == 0);
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kReactionsOrResultOffset,
                                 SmiConstant(Smi::zero()));
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kFlagsOffset,
                                 SmiConstant(Smi::zero()));
  for (int offset = JSPromise::kHeaderSize;
       offset < JSPromise::kSizeWithEmbedderFields; offset += kTaggedSize) {
    StoreObjectFieldNoWriteBarrier(promise, offset, SmiConstant(Smi::zero()));
  }
}

TNode<JSPromise> PromiseBuiltinsAssembler::AllocateAndInitJSPromise(
    TNode<Context> context) {
  return AllocateAndInitJSPromise(context, UndefinedConstant());
}

TNode<JSPromise> PromiseBuiltinsAssembler::AllocateAndInitJSPromise(
    TNode<Context> context, TNode<Object> parent) {
  const TNode<JSPromise> instance = AllocateJSPromise(context);
  PromiseInit(instance);

  Label out(this);
  GotoIfNot(IsPromiseHookEnabledOrHasAsyncEventDelegate(), &out);
  CallRuntime(Runtime::kPromiseHookInit, context, instance, parent);
  Goto(&out);

  BIND(&out);
  return instance;
}

TNode<JSPromise> PromiseBuiltinsAssembler::AllocateAndSetJSPromise(
    TNode<Context> context, v8::Promise::PromiseState status,
    TNode<Object> result) {
  DCHECK_NE(Promise::kPending, status);

  const TNode<JSPromise> instance = AllocateJSPromise(context);
  StoreObjectFieldNoWriteBarrier(instance, JSPromise::kReactionsOrResultOffset,
                                 result);
  STATIC_ASSERT(JSPromise::kStatusShift == 0);
  StoreObjectFieldNoWriteBarrier(instance, JSPromise::kFlagsOffset,
                                 SmiConstant(status));
  for (int offset = JSPromise::kHeaderSize;
       offset < JSPromise::kSizeWithEmbedderFields; offset += kTaggedSize) {
    StoreObjectFieldNoWriteBarrier(instance, offset, SmiConstant(0));
  }

  Label out(this);
  GotoIfNot(IsPromiseHookEnabledOrHasAsyncEventDelegate(), &out);
  CallRuntime(Runtime::kPromiseHookInit, context, instance,
              UndefinedConstant());
  Goto(&out);

  BIND(&out);
  return instance;
}

TNode<BoolT> PromiseBuiltinsAssembler::PromiseHasHandler(
    TNode<JSPromise> promise) {
  const TNode<Smi> flags =
      LoadObjectField<Smi>(promise, JSPromise::kFlagsOffset);
  return IsSetWord(SmiUntag(flags), 1 << JSPromise::kHasHandlerBit);
}

TNode<PromiseReaction> PromiseBuiltinsAssembler::AllocatePromiseReaction(
    TNode<Object> next, TNode<HeapObject> promise_or_capability,
    TNode<HeapObject> fulfill_handler, TNode<HeapObject> reject_handler) {
  const TNode<HeapObject> reaction = Allocate(PromiseReaction::kSize);
  StoreMapNoWriteBarrier(reaction, RootIndex::kPromiseReactionMap);
  StoreObjectFieldNoWriteBarrier(reaction, PromiseReaction::kNextOffset, next);
  StoreObjectFieldNoWriteBarrier(reaction,
                                 PromiseReaction::kPromiseOrCapabilityOffset,
                                 promise_or_capability);
  StoreObjectFieldNoWriteBarrier(
      reaction, PromiseReaction::kFulfillHandlerOffset, fulfill_handler);
  StoreObjectFieldNoWriteBarrier(
      reaction, PromiseReaction::kRejectHandlerOffset, reject_handler);
  return CAST(reaction);
}

TNode<PromiseReactionJobTask>
PromiseBuiltinsAssembler::AllocatePromiseReactionJobTask(
    TNode<Map> map, TNode<Context> context, TNode<Object> argument,
    TNode<HeapObject> handler, TNode<HeapObject> promise_or_capability) {
  const TNode<HeapObject> microtask =
      Allocate(PromiseReactionJobTask::kSizeOfAllPromiseReactionJobTasks);
  StoreMapNoWriteBarrier(microtask, map);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseReactionJobTask::kArgumentOffset, argument);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseReactionJobTask::kContextOffset, context);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseReactionJobTask::kHandlerOffset, handler);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseReactionJobTask::kPromiseOrCapabilityOffset,
      promise_or_capability);
  return CAST(microtask);
}

TNode<PromiseResolveThenableJobTask>
PromiseBuiltinsAssembler::AllocatePromiseResolveThenableJobTask(
    TNode<JSPromise> promise_to_resolve, TNode<JSReceiver> then,
    TNode<JSReceiver> thenable, TNode<Context> context) {
  const TNode<HeapObject> microtask =
      Allocate(PromiseResolveThenableJobTask::kSize);
  StoreMapNoWriteBarrier(microtask,
                         RootIndex::kPromiseResolveThenableJobTaskMap);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kContextOffset, context);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kPromiseToResolveOffset,
      promise_to_resolve);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kThenOffset, then);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kThenableOffset, thenable);
  return CAST(microtask);
}

void PromiseBuiltinsAssembler::BranchIfPromiseResolveLookupChainIntact(
    TNode<NativeContext> native_context, TNode<Object> constructor,
    Label* if_fast, Label* if_slow) {
  GotoIfForceSlowPath(if_slow);
  TNode<Object> promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  GotoIfNot(TaggedEqual(promise_fun, constructor), if_slow);
  Branch(IsPromiseResolveProtectorCellInvalid(), if_slow, if_fast);
}

void PromiseBuiltinsAssembler::GotoIfNotPromiseResolveLookupChainIntact(
    TNode<NativeContext> native_context, TNode<Object> constructor,
    Label* if_slow) {
  Label if_fast(this);
  BranchIfPromiseResolveLookupChainIntact(native_context, constructor, &if_fast,
                                          if_slow);
  BIND(&if_fast);
}

void PromiseBuiltinsAssembler::BranchIfPromiseSpeciesLookupChainIntact(
    TNode<NativeContext> native_context, TNode<Map> promise_map, Label* if_fast,
    Label* if_slow) {
  TNode<Object> promise_prototype =
      LoadContextElement(native_context, Context::PROMISE_PROTOTYPE_INDEX);
  GotoIfForceSlowPath(if_slow);
  GotoIfNot(TaggedEqual(LoadMapPrototype(promise_map), promise_prototype),
            if_slow);
  Branch(IsPromiseSpeciesProtectorCellInvalid(), if_slow, if_fast);
}

void PromiseBuiltinsAssembler::BranchIfPromiseThenLookupChainIntact(
    TNode<NativeContext> native_context, TNode<Map> receiver_map,
    Label* if_fast, Label* if_slow) {
  GotoIfForceSlowPath(if_slow);
  GotoIfNot(IsJSPromiseMap(receiver_map), if_slow);
  const TNode<Object> promise_prototype =
      LoadContextElement(native_context, Context::PROMISE_PROTOTYPE_INDEX);
  GotoIfNot(TaggedEqual(LoadMapPrototype(receiver_map), promise_prototype),
            if_slow);
  Branch(IsPromiseThenProtectorCellInvalid(), if_slow, if_fast);
}

void PromiseBuiltinsAssembler::BranchIfAccessCheckFailed(
    TNode<Context> context, TNode<Context> native_context,
    TNode<Object> promise_constructor, TNode<Object> executor,
    Label* if_noaccess) {
  TVARIABLE(HeapObject, var_executor);
  var_executor = CAST(executor);
  Label has_access(this), call_runtime(this, Label::kDeferred);

  // If executor is a bound function, load the bound function until we've
  // reached an actual function.
  Label found_function(this), loop_over_bound_function(this, &var_executor);
  Goto(&loop_over_bound_function);
  BIND(&loop_over_bound_function);
  {
    TNode<Uint16T> executor_type = LoadInstanceType(var_executor.value());
    GotoIf(InstanceTypeEqual(executor_type, JS_FUNCTION_TYPE), &found_function);
    GotoIfNot(InstanceTypeEqual(executor_type, JS_BOUND_FUNCTION_TYPE),
              &call_runtime);
    var_executor = LoadObjectField<HeapObject>(
        var_executor.value(), JSBoundFunction::kBoundTargetFunctionOffset);
    Goto(&loop_over_bound_function);
  }

  // Load the context from the function and compare it to the Promise
  // constructor's context. If they match, everything is fine, otherwise, bail
  // out to the runtime.
  BIND(&found_function);
  {
    TNode<Context> function_context = LoadObjectField<Context>(
        var_executor.value(), JSFunction::kContextOffset);
    TNode<NativeContext> native_function_context =
        LoadNativeContext(function_context);
    Branch(TaggedEqual(native_context, native_function_context), &has_access,
           &call_runtime);
  }

  BIND(&call_runtime);
  {
    Branch(TaggedEqual(CallRuntime(Runtime::kAllowDynamicFunction, context,
                                   promise_constructor),
                       TrueConstant()),
           &has_access, if_noaccess);
  }

  BIND(&has_access);
}

}  // namespace internal
}  // namespace v8
