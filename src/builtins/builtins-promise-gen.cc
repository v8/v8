// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-promise-gen.h"

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

using compiler::Node;

Node* PromiseBuiltinsAssembler::AllocateJSPromise(Node* context) {
  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  CSA_ASSERT(this, IsFunctionWithPrototypeSlotMap(LoadMap(promise_fun)));
  Node* const promise_map =
      LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const promise = Allocate(JSPromise::kSizeWithEmbedderFields);
  StoreMapNoWriteBarrier(promise, promise_map);
  StoreObjectFieldRoot(promise, JSPromise::kPropertiesOrHashOffset,
                       Heap::kEmptyFixedArrayRootIndex);
  StoreObjectFieldRoot(promise, JSPromise::kElementsOffset,
                       Heap::kEmptyFixedArrayRootIndex);
  return promise;
}

void PromiseBuiltinsAssembler::PromiseInit(Node* promise) {
  STATIC_ASSERT(v8::Promise::kPending == 0);
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kReactionsOrResultOffset,
                                 SmiConstant(Smi::kZero));
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kFlagsOffset,
                                 SmiConstant(Smi::kZero));
  for (int i = 0; i < v8::Promise::kEmbedderFieldCount; i++) {
    int offset = JSPromise::kSize + i * kPointerSize;
    StoreObjectFieldNoWriteBarrier(promise, offset, SmiConstant(Smi::kZero));
  }
}

Node* PromiseBuiltinsAssembler::AllocateAndInitJSPromise(Node* context) {
  return AllocateAndInitJSPromise(context, UndefinedConstant());
}

Node* PromiseBuiltinsAssembler::AllocateAndInitJSPromise(Node* context,
                                                         Node* parent) {
  Node* const instance = AllocateJSPromise(context);
  PromiseInit(instance);

  Label out(this);
  GotoIfNot(IsPromiseHookEnabledOrDebugIsActive(), &out);
  CallRuntime(Runtime::kPromiseHookInit, context, instance, parent);
  Goto(&out);

  BIND(&out);
  return instance;
}

Node* PromiseBuiltinsAssembler::AllocateAndSetJSPromise(
    Node* context, v8::Promise::PromiseState status, Node* result) {
  DCHECK_NE(Promise::kPending, status);

  Node* const instance = AllocateJSPromise(context);
  StoreObjectFieldNoWriteBarrier(instance, JSPromise::kReactionsOrResultOffset,
                                 result);
  STATIC_ASSERT(JSPromise::kStatusShift == 0);
  StoreObjectFieldNoWriteBarrier(instance, JSPromise::kFlagsOffset,
                                 SmiConstant(status));
  for (int i = 0; i < v8::Promise::kEmbedderFieldCount; i++) {
    int offset = JSPromise::kSize + i * kPointerSize;
    StoreObjectFieldNoWriteBarrier(instance, offset, SmiConstant(0));
  }

  Label out(this);
  GotoIfNot(IsPromiseHookEnabledOrDebugIsActive(), &out);
  CallRuntime(Runtime::kPromiseHookInit, context, instance,
              UndefinedConstant());
  Goto(&out);

  BIND(&out);
  return instance;
}

std::pair<Node*, Node*>
PromiseBuiltinsAssembler::CreatePromiseResolvingFunctions(
    Node* promise, Node* debug_event, Node* native_context) {
  Node* const promise_context = CreatePromiseResolvingFunctionsContext(
      promise, debug_event, native_context);
  Node* const map = LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
  Node* const resolve_info =
      LoadContextElement(native_context, Context::PROMISE_RESOLVE_SHARED_FUN);
  Node* const resolve =
      AllocateFunctionWithMapAndContext(map, resolve_info, promise_context);
  Node* const reject_info =
      LoadContextElement(native_context, Context::PROMISE_REJECT_SHARED_FUN);
  Node* const reject =
      AllocateFunctionWithMapAndContext(map, reject_info, promise_context);
  return std::make_pair(resolve, reject);
}

Node* PromiseBuiltinsAssembler::NewPromiseCapability(Node* context,
                                                     Node* constructor,
                                                     Node* debug_event) {
  if (debug_event == nullptr) {
    debug_event = TrueConstant();
  }

  Label if_not_constructor(this, Label::kDeferred),
      if_notcallable(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(constructor), &if_not_constructor);
  GotoIfNot(IsConstructorMap(LoadMap(constructor)), &if_not_constructor);

  Node* native_context = LoadNativeContext(context);

  VARIABLE(var_capability, MachineRepresentation::kTagged);
  Label if_builtin_promise(this), if_custom_promise(this, Label::kDeferred),
      done(this);
  Branch(WordEqual(constructor,
                   LoadContextElement(native_context,
                                      Context::PROMISE_FUNCTION_INDEX)),
         &if_builtin_promise, &if_custom_promise);

  BIND(&if_builtin_promise);
  {
    Node* promise = AllocateAndInitJSPromise(context, UndefinedConstant());

    Node* resolve = nullptr;
    Node* reject = nullptr;
    std::tie(resolve, reject) =
        CreatePromiseResolvingFunctions(promise, debug_event, native_context);

    Node* capability = Allocate(PromiseCapability::kSize);
    StoreMapNoWriteBarrier(capability, Heap::kPromiseCapabilityMapRootIndex);
    StoreObjectFieldNoWriteBarrier(capability,
                                   PromiseCapability::kPromiseOffset, promise);
    StoreObjectFieldNoWriteBarrier(capability,
                                   PromiseCapability::kResolveOffset, resolve);
    StoreObjectFieldNoWriteBarrier(capability, PromiseCapability::kRejectOffset,
                                   reject);
    var_capability.Bind(capability);
    Goto(&done);
  }

  BIND(&if_custom_promise);
  {
    Node* capability = Allocate(PromiseCapability::kSize);
    StoreMapNoWriteBarrier(capability, Heap::kPromiseCapabilityMapRootIndex);
    StoreObjectFieldRoot(capability, PromiseCapability::kPromiseOffset,
                         Heap::kUndefinedValueRootIndex);
    StoreObjectFieldRoot(capability, PromiseCapability::kResolveOffset,
                         Heap::kUndefinedValueRootIndex);
    StoreObjectFieldRoot(capability, PromiseCapability::kRejectOffset,
                         Heap::kUndefinedValueRootIndex);
    var_capability.Bind(capability);

    Node* executor_context =
        CreatePromiseGetCapabilitiesExecutorContext(capability, native_context);
    Node* executor_info = LoadContextElement(
        native_context, Context::PROMISE_GET_CAPABILITIES_EXECUTOR_SHARED_FUN);
    Node* function_map = LoadContextElement(
        native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
    Node* executor = AllocateFunctionWithMapAndContext(
        function_map, executor_info, executor_context);

    Node* promise = ConstructJS(CodeFactory::Construct(isolate()), context,
                                constructor, executor);
    StoreObjectField(capability, PromiseCapability::kPromiseOffset, promise);

    Node* resolve =
        LoadObjectField(capability, PromiseCapability::kResolveOffset);
    GotoIf(TaggedIsSmi(resolve), &if_notcallable);
    GotoIfNot(IsCallable(resolve), &if_notcallable);

    Node* reject =
        LoadObjectField(capability, PromiseCapability::kRejectOffset);
    GotoIf(TaggedIsSmi(reject), &if_notcallable);
    GotoIfNot(IsCallable(reject), &if_notcallable);

    Goto(&done);
  }

  BIND(&if_not_constructor);
  ThrowTypeError(context, MessageTemplate::kNotConstructor, constructor);

  BIND(&if_notcallable);
  ThrowTypeError(context, MessageTemplate::kPromiseNonCallable);

  BIND(&done);
  return var_capability.value();
}

Node* PromiseBuiltinsAssembler::CreatePromiseContext(Node* native_context,
                                                     int slots) {
  DCHECK_GE(slots, Context::MIN_CONTEXT_SLOTS);

  Node* const context = AllocateInNewSpace(FixedArray::SizeFor(slots));
  InitializeFunctionContext(native_context, context, slots);
  return context;
}

Node* PromiseBuiltinsAssembler::CreatePromiseResolvingFunctionsContext(
    Node* promise, Node* debug_event, Node* native_context) {
  Node* const context =
      CreatePromiseContext(native_context, kPromiseContextLength);
  StoreContextElementNoWriteBarrier(context, kPromiseSlot, promise);
  StoreContextElementNoWriteBarrier(context, kDebugEventSlot, debug_event);
  return context;
}

Node* PromiseBuiltinsAssembler::CreatePromiseGetCapabilitiesExecutorContext(
    Node* promise_capability, Node* native_context) {
  int kContextLength = kCapabilitiesContextLength;
  Node* context = CreatePromiseContext(native_context, kContextLength);
  StoreContextElementNoWriteBarrier(context, kCapabilitySlot,
                                    promise_capability);
  return context;
}

Node* PromiseBuiltinsAssembler::PromiseHasHandler(Node* promise) {
  Node* const flags = LoadObjectField(promise, JSPromise::kFlagsOffset);
  return IsSetWord(SmiUntag(flags), 1 << JSPromise::kHasHandlerBit);
}

void PromiseBuiltinsAssembler::PromiseSetHasHandler(Node* promise) {
  Node* const flags = LoadObjectField(promise, JSPromise::kFlagsOffset);
  Node* const new_flags =
      SmiOr(flags, SmiConstant(1 << JSPromise::kHasHandlerBit));
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kFlagsOffset, new_flags);
}

Node* PromiseBuiltinsAssembler::IsPromiseStatus(
    Node* actual, v8::Promise::PromiseState expected) {
  return Word32Equal(actual, Int32Constant(expected));
}

Node* PromiseBuiltinsAssembler::PromiseStatus(Node* promise) {
  STATIC_ASSERT(JSPromise::kStatusShift == 0);
  Node* const flags = LoadObjectField(promise, JSPromise::kFlagsOffset);
  return Word32And(SmiToWord32(flags), Int32Constant(JSPromise::kStatusMask));
}

void PromiseBuiltinsAssembler::PromiseSetStatus(
    Node* promise, v8::Promise::PromiseState const status) {
  CSA_ASSERT(this,
             IsPromiseStatus(PromiseStatus(promise), v8::Promise::kPending));
  CHECK_NE(status, v8::Promise::kPending);

  Node* mask = SmiConstant(status);
  Node* const flags = LoadObjectField(promise, JSPromise::kFlagsOffset);
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kFlagsOffset,
                                 SmiOr(flags, mask));
}

void PromiseBuiltinsAssembler::PromiseSetHandledHint(Node* promise) {
  Node* const flags = LoadObjectField(promise, JSPromise::kFlagsOffset);
  Node* const new_flags =
      SmiOr(flags, SmiConstant(1 << JSPromise::kHandledHintBit));
  StoreObjectFieldNoWriteBarrier(promise, JSPromise::kFlagsOffset, new_flags);
}

void PromiseBuiltinsAssembler::AppendPromiseCallback(int offset, Node* promise,
                                                     Node* value) {
  Node* elements = LoadObjectField(promise, offset);
  Node* length = LoadFixedArrayBaseLength(elements);
  CodeStubAssembler::ParameterMode mode = OptimalParameterMode();
  length = TaggedToParameter(length, mode);

  Node* delta = IntPtrOrSmiConstant(1, mode);
  Node* new_capacity = IntPtrOrSmiAdd(length, delta, mode);

  const WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER;
  int additional_offset = 0;

  ExtractFixedArrayFlags flags;
  flags |= ExtractFixedArrayFlag::kFixedArrays;
  Node* new_elements =
      ExtractFixedArray(elements, nullptr, length, new_capacity, flags, mode);

  StoreFixedArrayElement(new_elements, length, value, barrier_mode,
                         additional_offset, mode);

  StoreObjectField(promise, offset, new_elements);
}

Node* PromiseBuiltinsAssembler::InternalPromiseThen(Node* context,
                                                    Node* promise,
                                                    Node* on_fulfill,
                                                    Node* on_reject) {
  // 2. If IsPromise(promise) is false, throw a TypeError exception.
  ThrowIfNotInstanceType(context, promise, JS_PROMISE_TYPE,
                         "Promise.prototype.then");

  // 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  Label fast_promise_capability(this), slow_constructor(this, Label::kDeferred),
      slow_promise_capability(this, Label::kDeferred);
  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const promise_prototype =
      LoadContextElement(native_context, Context::PROMISE_PROTOTYPE_INDEX);
  Node* const promise_map = LoadMap(promise);
  GotoIfNot(WordEqual(LoadMapPrototype(promise_map), promise_prototype),
            &slow_constructor);
  Branch(IsSpeciesProtectorCellInvalid(), &slow_constructor,
         &fast_promise_capability);

  BIND(&slow_constructor);
  Node* const constructor =
      SpeciesConstructor(native_context, promise, promise_fun);
  Branch(WordEqual(constructor, promise_fun), &fast_promise_capability,
         &slow_promise_capability);

  // 4. Let resultCapability be ? NewPromiseCapability(C).
  Label perform_promise_then(this);
  VARIABLE(var_result_promise, MachineRepresentation::kTagged);
  VARIABLE(var_result_promise_or_capability, MachineRepresentation::kTagged);

  BIND(&fast_promise_capability);
  {
    Node* const result_promise = AllocateAndInitJSPromise(context, promise);
    var_result_promise.Bind(result_promise);
    var_result_promise_or_capability.Bind(result_promise);
    CSA_ASSERT(this, IsJSPromise(var_result_promise_or_capability.value()));
    Goto(&perform_promise_then);
  }

  BIND(&slow_promise_capability);
  {
    Node* const capability = NewPromiseCapability(context, constructor);
    var_result_promise.Bind(
        LoadObjectField(capability, PromiseCapability::kPromiseOffset));
    var_result_promise_or_capability.Bind(capability);
    CSA_ASSERT(this,
               IsPromiseCapability(var_result_promise_or_capability.value()));
    Goto(&perform_promise_then);
  }

  // 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //    resultCapability).
  BIND(&perform_promise_then);
  CSA_ASSERT(
      this,
      Word32Or(IsJSPromise(var_result_promise_or_capability.value()),
               IsPromiseCapability(var_result_promise_or_capability.value())));
  InternalPerformPromiseThen(context, promise, on_fulfill, on_reject,
                             var_result_promise_or_capability.value());
  return var_result_promise.value();
}

void PromiseBuiltinsAssembler::InternalPerformPromiseThen(Node* context,
                                                          Node* promise,
                                                          Node* on_fulfilled,
                                                          Node* on_rejected,
                                                          Node* result) {
  CSA_ASSERT(this, TaggedIsNotSmi(result));
  CSA_ASSERT(this, Word32Or(IsJSPromise(result), IsPromiseCapability(result)));

  // 3. If IsCallable(onFulfilled) is false, then
  //    a. Set onFulfilled to undefined.
  VARIABLE(var_on_fulfilled, MachineRepresentation::kTagged, on_fulfilled);
  Label if_fulfilled_done(this), if_fulfilled_notcallable(this);
  GotoIf(TaggedIsSmi(on_fulfilled), &if_fulfilled_notcallable);
  Branch(IsCallable(on_fulfilled), &if_fulfilled_done,
         &if_fulfilled_notcallable);
  BIND(&if_fulfilled_notcallable);
  var_on_fulfilled.Bind(UndefinedConstant());
  Goto(&if_fulfilled_done);
  BIND(&if_fulfilled_done);

  // 4. If IsCallable(onRejected) is false, then
  //    a. Set onRejected to undefined.
  VARIABLE(var_on_rejected, MachineRepresentation::kTagged, on_rejected);
  Label if_rejected_done(this), if_rejected_notcallable(this);
  GotoIf(TaggedIsSmi(on_rejected), &if_rejected_notcallable);
  Branch(IsCallable(on_rejected), &if_rejected_done, &if_rejected_notcallable);
  BIND(&if_rejected_notcallable);
  var_on_rejected.Bind(UndefinedConstant());
  Goto(&if_rejected_done);
  BIND(&if_rejected_done);

  Label if_pending(this), if_notpending(this), done(this);
  Node* const status = PromiseStatus(promise);
  Branch(IsPromiseStatus(status, v8::Promise::kPending), &if_pending,
         &if_notpending);

  BIND(&if_pending);
  {
    // The {promise} is still in "Pending" state, so we just record a new
    // PromiseReaction holding both the onFulfilled and onRejected callbacks.
    // Once the {promise} is resolved we decide on the concrete handler to
    // push onto the microtask queue.
    Node* const promise_reactions =
        LoadObjectField(promise, JSPromise::kReactionsOrResultOffset);
    Node* const reaction = AllocatePromiseReaction(promise_reactions, result,
                                                   var_on_fulfilled.value(),
                                                   var_on_rejected.value());
    StoreObjectField(promise, JSPromise::kReactionsOrResultOffset, reaction);
    Goto(&done);
  }

  BIND(&if_notpending);
  {
    VARIABLE(var_map, MachineRepresentation::kTagged);
    VARIABLE(var_handler, MachineRepresentation::kTagged);
    Label if_fulfilled(this), if_rejected(this, Label::kDeferred),
        enqueue(this);
    Branch(IsPromiseStatus(status, v8::Promise::kFulfilled), &if_fulfilled,
           &if_rejected);

    BIND(&if_fulfilled);
    {
      var_map.Bind(LoadRoot(Heap::kPromiseFulfillReactionJobTaskMapRootIndex));
      var_handler.Bind(var_on_fulfilled.value());
      Goto(&enqueue);
    }

    BIND(&if_rejected);
    {
      CSA_ASSERT(this, IsPromiseStatus(status, v8::Promise::kRejected));
      var_map.Bind(LoadRoot(Heap::kPromiseRejectReactionJobTaskMapRootIndex));
      var_handler.Bind(var_on_rejected.value());
      GotoIf(PromiseHasHandler(promise), &enqueue);
      CallRuntime(Runtime::kPromiseRevokeReject, context, promise);
      Goto(&enqueue);
    }

    BIND(&enqueue);
    Node* argument =
        LoadObjectField(promise, JSPromise::kReactionsOrResultOffset);
    Node* microtask = AllocatePromiseReactionJobTask(
        var_map.value(), context, argument, var_handler.value(), result);
    CallBuiltin(Builtins::kEnqueueMicrotask, NoContextConstant(), microtask);
    Goto(&done);
  }

  BIND(&done);
  PromiseSetHasHandler(promise);
}

// Promise fast path implementations rely on unmodified JSPromise instances.
// We use a fairly coarse granularity for this and simply check whether both
// the promise itself is unmodified (i.e. its map has not changed) and its
// prototype is unmodified.
// TODO(gsathya): Refactor this out to prevent code dupe with builtins-regexp
void PromiseBuiltinsAssembler::BranchIfFastPath(Node* context, Node* promise,
                                                Label* if_isunmodified,
                                                Label* if_ismodified) {
  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  BranchIfFastPath(native_context, promise_fun, promise, if_isunmodified,
                   if_ismodified);
}

void PromiseBuiltinsAssembler::BranchIfFastPath(Node* native_context,
                                                Node* promise_fun,
                                                Node* promise,
                                                Label* if_isunmodified,
                                                Label* if_ismodified) {
  CSA_ASSERT(this, IsNativeContext(native_context));
  CSA_ASSERT(this,
             WordEqual(promise_fun,
                       LoadContextElement(native_context,
                                          Context::PROMISE_FUNCTION_INDEX)));

  GotoIfForceSlowPath(if_ismodified);

  Node* const map = LoadMap(promise);
  Node* const initial_map =
      LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const has_initialmap = WordEqual(map, initial_map);

  GotoIfNot(has_initialmap, if_ismodified);

  Node* const initial_proto_initial_map =
      LoadContextElement(native_context, Context::PROMISE_PROTOTYPE_MAP_INDEX);
  Node* const proto_map = LoadMap(CAST(LoadMapPrototype(map)));
  Node* const proto_has_initialmap =
      WordEqual(proto_map, initial_proto_initial_map);

  Branch(proto_has_initialmap, if_isunmodified, if_ismodified);
}

Node* PromiseBuiltinsAssembler::AllocatePromiseReaction(
    Node* next, Node* promise_or_capability, Node* fulfill_handler,
    Node* reject_handler) {
  Node* const reaction = Allocate(PromiseReaction::kSize);
  StoreMapNoWriteBarrier(reaction, Heap::kPromiseReactionMapRootIndex);
  StoreObjectFieldNoWriteBarrier(reaction, PromiseReaction::kNextOffset, next);
  StoreObjectFieldNoWriteBarrier(reaction,
                                 PromiseReaction::kPromiseOrCapabilityOffset,
                                 promise_or_capability);
  StoreObjectFieldNoWriteBarrier(
      reaction, PromiseReaction::kFulfillHandlerOffset, fulfill_handler);
  StoreObjectFieldNoWriteBarrier(
      reaction, PromiseReaction::kRejectHandlerOffset, reject_handler);
  return reaction;
}

Node* PromiseBuiltinsAssembler::AllocatePromiseReactionJobTask(
    Node* map, Node* context, Node* argument, Node* handler,
    Node* promise_or_capability) {
  Node* const microtask = Allocate(PromiseReactionJobTask::kSize);
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
  return microtask;
}

Node* PromiseBuiltinsAssembler::AllocatePromiseReactionJobTask(
    Heap::RootListIndex map_root_index, Node* context, Node* argument,
    Node* handler, Node* promise_or_capability) {
  DCHECK(map_root_index == Heap::kPromiseFulfillReactionJobTaskMapRootIndex ||
         map_root_index == Heap::kPromiseRejectReactionJobTaskMapRootIndex);
  Node* const map = LoadRoot(map_root_index);
  return AllocatePromiseReactionJobTask(map, context, argument, handler,
                                        promise_or_capability);
}

Node* PromiseBuiltinsAssembler::AllocatePromiseResolveThenableJobTask(
    Node* promise_to_resolve, Node* then, Node* thenable, Node* context) {
  Node* const microtask = Allocate(PromiseResolveThenableJobTask::kSize);
  StoreMapNoWriteBarrier(microtask,
                         Heap::kPromiseResolveThenableJobTaskMapRootIndex);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kContextOffset, context);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kPromiseToResolveOffset,
      promise_to_resolve);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kThenOffset, then);
  StoreObjectFieldNoWriteBarrier(
      microtask, PromiseResolveThenableJobTask::kThenableOffset, thenable);
  return microtask;
}

void PromiseBuiltinsAssembler::InternalResolvePromise(Node* context,
                                                      Node* promise,
                                                      Node* result) {
  Isolate* isolate = this->isolate();

  VARIABLE(var_reason, MachineRepresentation::kTagged);
  VARIABLE(var_then, MachineRepresentation::kTagged);

  Label do_enqueue(this), fulfill(this), if_nocycle(this),
      if_cycle(this, Label::kDeferred),
      if_rejectpromise(this, Label::kDeferred), out(this);

  Label cycle_check(this);
  GotoIfNot(IsPromiseHookEnabledOrDebugIsActive(), &cycle_check);
  CallRuntime(Runtime::kPromiseHookResolve, context, promise);
  Goto(&cycle_check);

  BIND(&cycle_check);
  // 6. If SameValue(resolution, promise) is true, then
  BranchIfSameValue(promise, result, &if_cycle, &if_nocycle);
  BIND(&if_nocycle);

  // 7. If Type(resolution) is not Object, then
  GotoIf(TaggedIsSmi(result), &fulfill);
  GotoIfNot(IsJSReceiver(result), &fulfill);

  Label if_nativepromise(this), if_notnativepromise(this, Label::kDeferred);
  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  BranchIfFastPath(native_context, promise_fun, result, &if_nativepromise,
                   &if_notnativepromise);

  // Resolution is a native promise and if it's already resolved or
  // rejected, shortcircuit the resolution procedure by directly
  // reusing the value from the promise.
  BIND(&if_nativepromise);
  {
    Node* const thenable_status = PromiseStatus(result);
    Node* const thenable_value =
        LoadObjectField(result, JSPromise::kReactionsOrResultOffset);

    Label if_isnotpending(this);
    GotoIfNot(IsPromiseStatus(thenable_status, v8::Promise::kPending),
              &if_isnotpending);

    // TODO(gsathya): Use a marker here instead of the actual then
    // callback, and check for the marker in PromiseResolveThenableJob
    // and perform PromiseThen.
    Node* const then =
        LoadContextElement(native_context, Context::PROMISE_THEN_INDEX);
    var_then.Bind(then);
    Goto(&do_enqueue);

    BIND(&if_isnotpending);
    {
      Label if_fulfilled(this), if_rejected(this);
      Branch(IsPromiseStatus(thenable_status, v8::Promise::kFulfilled),
             &if_fulfilled, &if_rejected);

      BIND(&if_fulfilled);
      {
        PromiseFulfill(context, promise, thenable_value,
                       v8::Promise::kFulfilled);
        PromiseSetHasHandler(promise);
        Goto(&out);
      }

      BIND(&if_rejected);
      {
        Label reject(this);
        Node* const has_handler = PromiseHasHandler(result);

        // Promise has already been rejected, but had no handler.
        // Revoke previously triggered reject event.
        GotoIf(has_handler, &reject);
        CallRuntime(Runtime::kPromiseRevokeReject, context, result);
        Goto(&reject);

        BIND(&reject);
        // Don't cause a debug event as this case is forwarding a rejection.
        InternalPromiseReject(context, promise, thenable_value, false);
        PromiseSetHasHandler(result);
        Goto(&out);
      }
    }
  }

  BIND(&if_notnativepromise);
  {
    // 8. Let then be Get(resolution, "then").
    Node* const then =
        GetProperty(context, result, isolate->factory()->then_string());

    // 9. If then is an abrupt completion, then
    GotoIfException(then, &if_rejectpromise, &var_reason);

    // 11. If IsCallable(thenAction) is false, then
    GotoIf(TaggedIsSmi(then), &fulfill);
    Node* const then_map = LoadMap(then);
    GotoIfNot(IsCallableMap(then_map), &fulfill);
    var_then.Bind(then);
    Goto(&do_enqueue);
  }

  BIND(&do_enqueue);
  {
    Node* const info = AllocatePromiseResolveThenableJobTask(
        promise, var_then.value(), result, native_context);

    Label enqueue(this);
    GotoIfNot(IsDebugActive(), &enqueue);

    GotoIf(TaggedIsSmi(result), &enqueue);
    GotoIfNot(HasInstanceType(result, JS_PROMISE_TYPE), &enqueue);

    // Mark the dependency of the new promise on the resolution
    Node* const key =
        HeapConstant(isolate->factory()->promise_handled_by_symbol());
    CallRuntime(Runtime::kSetProperty, context, result, key, promise,
                SmiConstant(LanguageMode::kStrict));
    Goto(&enqueue);

    // 12. Perform EnqueueJob("PromiseJobs",
    // PromiseResolveThenableJob, « promise, resolution, thenAction»).
    BIND(&enqueue);
    CallBuiltin(Builtins::kEnqueueMicrotask, NoContextConstant(), info);
    Goto(&out);
  }

  // 7.b Return FulfillPromise(promise, resolution).
  BIND(&fulfill);
  {
    PromiseFulfill(context, promise, result, v8::Promise::kFulfilled);
    Goto(&out);
  }

  BIND(&if_cycle);
  {
    // 6.a Let selfResolutionError be a newly created TypeError object.
    Node* const message_id = SmiConstant(MessageTemplate::kPromiseCyclic);
    Node* const error =
        CallRuntime(Runtime::kNewTypeError, context, message_id, result);
    var_reason.Bind(error);

    // 6.b Return RejectPromise(promise, selfResolutionError).
    Goto(&if_rejectpromise);
  }

  // 9.a Return RejectPromise(promise, then.[[Value]]).
  BIND(&if_rejectpromise);
  {
    // Don't cause a debug event as this case is forwarding a rejection.
    InternalPromiseReject(context, promise, var_reason.value(), false);
    Goto(&out);
  }

  BIND(&out);
}

void PromiseBuiltinsAssembler::PromiseFulfill(
    Node* context, Node* promise, Node* result,
    v8::Promise::PromiseState status) {
  CSA_ASSERT(this, IsJSPromise(promise));

  // Gather the reactions from the {promise}. We need to reverse the
  // reactions here, since we record them on the {promise} in the wrong
  // order.
  Node* reactions;
  {
    VARIABLE(var_current, MachineRepresentation::kTagged,
             LoadObjectField(promise, JSPromise::kReactionsOrResultOffset));
    VARIABLE(var_reversed, MachineRepresentation::kTagged,
             SmiConstant(Smi::kZero));

    Label loop(this, {&var_current, &var_reversed}), done_loop(this);
    Goto(&loop);
    BIND(&loop);
    {
      Node* current = var_current.value();
      GotoIf(TaggedIsSmi(current), &done_loop);
      var_current.Bind(LoadObjectField(current, PromiseReaction::kNextOffset));
      StoreObjectField(current, PromiseReaction::kNextOffset,
                       var_reversed.value());
      var_reversed.Bind(current);
      Goto(&loop);
    }
    BIND(&done_loop);
    reactions = var_reversed.value();
  }

  // Reset the {promise}.
  StoreObjectField(promise, JSPromise::kReactionsOrResultOffset, result);
  PromiseSetStatus(promise, status);

  // Morph the {reactions} into PromiseReactionJobTasks and push them
  // onto the microtask queue.
  {
    VARIABLE(var_current, MachineRepresentation::kTagged, reactions);

    Label loop(this, {&var_current}), done_loop(this);
    Goto(&loop);
    BIND(&loop);
    {
      Node* current = var_current.value();
      GotoIf(TaggedIsSmi(current), &done_loop);
      var_current.Bind(LoadObjectField(current, PromiseReaction::kNextOffset));

      // Morph {current} from a PromiseReaction into a PromiseReactionJobTask
      // and schedule that on the microtask queue. We try to minimize the number
      // of stores here to avoid screwing up the store buffer.
      STATIC_ASSERT(PromiseReaction::kSize == PromiseReactionJobTask::kSize);
      if (status == Promise::kFulfilled) {
        StoreMapNoWriteBarrier(
            current, Heap::kPromiseFulfillReactionJobTaskMapRootIndex);
        StoreObjectField(current, PromiseReactionJobTask::kArgumentOffset,
                         result);
        StoreObjectField(current, PromiseReactionJobTask::kContextOffset,
                         context);
        STATIC_ASSERT(PromiseReaction::kFulfillHandlerOffset ==
                      PromiseRejectReactionJobTask::kHandlerOffset);
        STATIC_ASSERT(PromiseReaction::kPromiseOrCapabilityOffset ==
                      PromiseReactionJobTask::kPromiseOrCapabilityOffset);
      } else {
        DCHECK_EQ(Promise::kRejected, status);
        Node* handler =
            LoadObjectField(current, PromiseReaction::kRejectHandlerOffset);
        StoreMapNoWriteBarrier(current,
                               Heap::kPromiseRejectReactionJobTaskMapRootIndex);
        StoreObjectField(current, PromiseReactionJobTask::kArgumentOffset,
                         result);
        StoreObjectField(current, PromiseReactionJobTask::kContextOffset,
                         context);
        StoreObjectField(current, PromiseReactionJobTask::kHandlerOffset,
                         handler);
        STATIC_ASSERT(PromiseReaction::kPromiseOrCapabilityOffset ==
                      PromiseReactionJobTask::kPromiseOrCapabilityOffset);
      }
      CallBuiltin(Builtins::kEnqueueMicrotask, NoContextConstant(), current);
      Goto(&loop);
    }
    BIND(&done_loop);
  }
}

void PromiseBuiltinsAssembler::BranchIfAccessCheckFailed(
    Node* context, Node* native_context, Node* promise_constructor,
    Node* executor, Label* if_noaccess) {
  VARIABLE(var_executor, MachineRepresentation::kTagged);
  var_executor.Bind(executor);
  Label has_access(this), call_runtime(this, Label::kDeferred);

  // If executor is a bound function, load the bound function until we've
  // reached an actual function.
  Label found_function(this), loop_over_bound_function(this, &var_executor);
  Goto(&loop_over_bound_function);
  BIND(&loop_over_bound_function);
  {
    Node* executor_type = LoadInstanceType(var_executor.value());
    GotoIf(InstanceTypeEqual(executor_type, JS_FUNCTION_TYPE), &found_function);
    GotoIfNot(InstanceTypeEqual(executor_type, JS_BOUND_FUNCTION_TYPE),
              &call_runtime);
    var_executor.Bind(LoadObjectField(
        var_executor.value(), JSBoundFunction::kBoundTargetFunctionOffset));
    Goto(&loop_over_bound_function);
  }

  // Load the context from the function and compare it to the Promise
  // constructor's context. If they match, everything is fine, otherwise, bail
  // out to the runtime.
  BIND(&found_function);
  {
    Node* function_context =
        LoadObjectField(var_executor.value(), JSFunction::kContextOffset);
    Node* native_function_context = LoadNativeContext(function_context);
    Branch(WordEqual(native_context, native_function_context), &has_access,
           &call_runtime);
  }

  BIND(&call_runtime);
  {
    Branch(WordEqual(CallRuntime(Runtime::kAllowDynamicFunction, context,
                                 promise_constructor),
                     TrueConstant()),
           &has_access, if_noaccess);
  }

  BIND(&has_access);
}

void PromiseBuiltinsAssembler::InternalPromiseReject(Node* context,
                                                     Node* promise, Node* value,
                                                     Node* debug_event) {
  Label out(this);
  GotoIfNot(IsDebugActive(), &out);
  GotoIfNot(WordEqual(TrueConstant(), debug_event), &out);
  CallRuntime(Runtime::kDebugPromiseReject, context, promise, value);
  Goto(&out);

  BIND(&out);
  InternalPromiseReject(context, promise, value, false);
}

// This duplicates a lot of logic from PromiseRejectEvent in
// runtime-promise.cc
void PromiseBuiltinsAssembler::InternalPromiseReject(Node* context,
                                                     Node* promise, Node* value,
                                                     bool debug_event) {
  Label fulfill(this), exit(this);

  GotoIfNot(IsPromiseHookEnabledOrDebugIsActive(), &fulfill);
  if (debug_event) {
    CallRuntime(Runtime::kDebugPromiseReject, context, promise, value);
  }
  CallRuntime(Runtime::kPromiseHookResolve, context, promise);
  Goto(&fulfill);

  BIND(&fulfill);
  PromiseFulfill(context, promise, value, v8::Promise::kRejected);

  GotoIf(PromiseHasHandler(promise), &exit);
  CallRuntime(Runtime::kReportPromiseReject, context, promise, value);
  Goto(&exit);

  BIND(&exit);
}

void PromiseBuiltinsAssembler::SetForwardingHandlerIfTrue(
    Node* context, Node* condition, const NodeGenerator& object) {
  Label done(this);
  GotoIfNot(condition, &done);
  CallRuntime(Runtime::kSetProperty, context, object(),
              HeapConstant(factory()->promise_forwarding_handler_symbol()),
              TrueConstant(), SmiConstant(LanguageMode::kStrict));
  Goto(&done);
  BIND(&done);
}

void PromiseBuiltinsAssembler::SetPromiseHandledByIfTrue(
    Node* context, Node* condition, Node* promise,
    const NodeGenerator& handled_by) {
  Label done(this);
  GotoIfNot(condition, &done);
  GotoIf(TaggedIsSmi(promise), &done);
  GotoIfNot(HasInstanceType(promise, JS_PROMISE_TYPE), &done);
  CallRuntime(Runtime::kSetProperty, context, promise,
              HeapConstant(factory()->promise_handled_by_symbol()),
              handled_by(), SmiConstant(LanguageMode::kStrict));
  Goto(&done);
  BIND(&done);
}

void PromiseBuiltinsAssembler::PerformFulfillClosure(
    Node* context, Node* value, PromiseReaction::Type type) {
  Label out(this);

  // 2. Let promise be F.[[Promise]].
  Node* const promise_slot = IntPtrConstant(kPromiseSlot);
  Node* const promise = LoadContextElement(context, promise_slot);

  // We use `undefined` as a marker to know that this callback was
  // already called.
  GotoIf(IsUndefined(promise), &out);

  if (type == PromiseReaction::kFulfill) {
    InternalResolvePromise(context, promise, value);
  } else {
    Node* const debug_event =
        LoadContextElement(context, IntPtrConstant(kDebugEventSlot));
    InternalPromiseReject(context, promise, value, debug_event);
  }

  StoreContextElement(context, promise_slot, UndefinedConstant());
  Goto(&out);

  BIND(&out);
}

// ES#sec-promise-reject-functions
// Promise Reject Functions
TF_BUILTIN(PromiseRejectClosure, PromiseBuiltinsAssembler) {
  Node* const value = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  PerformFulfillClosure(context, value, PromiseReaction::kReject);
  Return(UndefinedConstant());
}

// ES6 #sec-promise-executor
TF_BUILTIN(PromiseConstructor, PromiseBuiltinsAssembler) {
  Node* const executor = Parameter(Descriptor::kExecutor);
  Node* const new_target = Parameter(Descriptor::kNewTarget);
  Node* const context = Parameter(Descriptor::kContext);
  Isolate* isolate = this->isolate();

  Label if_targetisundefined(this, Label::kDeferred);

  GotoIf(IsUndefined(new_target), &if_targetisundefined);

  Label if_notcallable(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(executor), &if_notcallable);

  Node* const executor_map = LoadMap(executor);
  GotoIfNot(IsCallableMap(executor_map), &if_notcallable);

  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const is_debug_active = IsDebugActive();
  Label if_targetisnotmodified(this),
      if_targetismodified(this, Label::kDeferred), run_executor(this),
      debug_push(this), if_noaccess(this, Label::kDeferred);

  BranchIfAccessCheckFailed(context, native_context, promise_fun, executor,
                            &if_noaccess);

  Branch(WordEqual(promise_fun, new_target), &if_targetisnotmodified,
         &if_targetismodified);

  VARIABLE(var_result, MachineRepresentation::kTagged);
  VARIABLE(var_reject_call, MachineRepresentation::kTagged);
  VARIABLE(var_reason, MachineRepresentation::kTagged);

  BIND(&if_targetisnotmodified);
  {
    Node* const instance = AllocateAndInitJSPromise(context);
    var_result.Bind(instance);
    Goto(&debug_push);
  }

  BIND(&if_targetismodified);
  {
    ConstructorBuiltinsAssembler constructor_assembler(this->state());
    Node* const instance = constructor_assembler.EmitFastNewObject(
        context, promise_fun, new_target);
    PromiseInit(instance);
    var_result.Bind(instance);

    GotoIfNot(IsPromiseHookEnabledOrDebugIsActive(), &debug_push);
    CallRuntime(Runtime::kPromiseHookInit, context, instance,
                UndefinedConstant());
    Goto(&debug_push);
  }

  BIND(&debug_push);
  {
    GotoIfNot(is_debug_active, &run_executor);
    CallRuntime(Runtime::kDebugPushPromise, context, var_result.value());
    Goto(&run_executor);
  }

  BIND(&run_executor);
  {
    Label out(this), if_rejectpromise(this), debug_pop(this, Label::kDeferred);

    Node *resolve, *reject;
    std::tie(resolve, reject) = CreatePromiseResolvingFunctions(
        var_result.value(), TrueConstant(), native_context);

    Node* const maybe_exception = CallJS(
        CodeFactory::Call(isolate, ConvertReceiverMode::kNullOrUndefined),
        context, executor, UndefinedConstant(), resolve, reject);

    GotoIfException(maybe_exception, &if_rejectpromise, &var_reason);
    Branch(is_debug_active, &debug_pop, &out);

    BIND(&if_rejectpromise);
    {
      CallJS(CodeFactory::Call(isolate, ConvertReceiverMode::kNullOrUndefined),
             context, reject, UndefinedConstant(), var_reason.value());
      Branch(is_debug_active, &debug_pop, &out);
    }

    BIND(&debug_pop);
    {
      CallRuntime(Runtime::kDebugPopPromise, context);
      Goto(&out);
    }
    BIND(&out);
    Return(var_result.value());
  }

  // 1. If NewTarget is undefined, throw a TypeError exception.
  BIND(&if_targetisundefined);
  ThrowTypeError(context, MessageTemplate::kNotAPromise, new_target);

  // 2. If IsCallable(executor) is false, throw a TypeError exception.
  BIND(&if_notcallable);
  ThrowTypeError(context, MessageTemplate::kResolverNotAFunction, executor);

  // Silently fail if the stack looks fishy.
  BIND(&if_noaccess);
  {
    Node* const counter_id =
        SmiConstant(v8::Isolate::kPromiseConstructorReturnedUndefined);
    CallRuntime(Runtime::kIncrementUseCounter, context, counter_id);
    Return(UndefinedConstant());
  }
}

TF_BUILTIN(PromiseInternalConstructor, PromiseBuiltinsAssembler) {
  Node* const parent = Parameter(Descriptor::kParent);
  Node* const context = Parameter(Descriptor::kContext);
  Return(AllocateAndInitJSPromise(context, parent));
}

// ES#sec-promise.prototype.then
// Promise.prototype.then ( onFulfilled, onRejected )
TF_BUILTIN(PromisePrototypeThen, PromiseBuiltinsAssembler) {
  // 1. Let promise be the this value.
  Node* const promise = Parameter(Descriptor::kReceiver);
  Node* const on_resolve = Parameter(Descriptor::kOnFullfilled);
  Node* const on_reject = Parameter(Descriptor::kOnRejected);
  Node* const context = Parameter(Descriptor::kContext);

  Node* const result =
      InternalPromiseThen(context, promise, on_resolve, on_reject);
  Return(result);
}

// ES#sec-promise-resolve-functions
// Promise Resolve Functions
TF_BUILTIN(PromiseResolveClosure, PromiseBuiltinsAssembler) {
  Node* const value = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  PerformFulfillClosure(context, value, PromiseReaction::kFulfill);
  Return(UndefinedConstant());
}

// ES #sec-fulfillpromise
TF_BUILTIN(ResolvePromise, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const result = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  InternalResolvePromise(context, promise, result);
  Return(UndefinedConstant());
}

// ES#sec-promise.prototype.catch
// Promise.prototype.catch ( onRejected )
TF_BUILTIN(PromisePrototypeCatch, PromiseBuiltinsAssembler) {
  // 1. Let promise be the this value.
  Node* const promise = Parameter(Descriptor::kReceiver);
  Node* const on_fulfilled = UndefinedConstant();
  Node* const on_rejected = Parameter(Descriptor::kOnRejected);
  Node* const context = Parameter(Descriptor::kContext);

  // 2. Return ? Invoke(promise, "then", « undefined, onRejected »).
  VARIABLE(var_then, MachineRepresentation::kTagged);
  Label if_fast(this), if_slow(this, Label::kDeferred), done(this);
  GotoIf(TaggedIsSmi(promise), &if_slow);
  BranchIfFastPath(context, promise, &if_fast, &if_slow);

  BIND(&if_fast);
  {
    Node* const native_context = LoadNativeContext(context);
    var_then.Bind(
        LoadContextElement(native_context, Context::PROMISE_THEN_INDEX));
    Goto(&done);
  }

  BIND(&if_slow);
  {
    var_then.Bind(
        GetProperty(context, promise, isolate()->factory()->then_string()));
    Goto(&done);
  }

  BIND(&done);
  Node* const then = var_then.value();
  Node* const result = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
      context, then, promise, on_fulfilled, on_rejected);
  Return(result);
}

// ES section #sec-promiseresolvethenablejob
TF_BUILTIN(PromiseResolveThenableJob, PromiseBuiltinsAssembler) {
  Node* context = Parameter(Descriptor::kContext);
  Node* promise_to_resolve = Parameter(Descriptor::kPromiseToResolve);
  Node* thenable = Parameter(Descriptor::kThenable);
  Node* then = Parameter(Descriptor::kThen);

  Node* native_context = LoadNativeContext(context);

  // TODO(bmeurer): Add a fast-path for the case that {thenable} is a
  // JSPromise and {then} is the original Promise.prototype.then method.
  Node* resolve = nullptr;
  Node* reject = nullptr;
  std::tie(resolve, reject) = CreatePromiseResolvingFunctions(
      promise_to_resolve, FalseConstant(), native_context);

  Label if_exception(this, Label::kDeferred);
  VARIABLE(var_exception, MachineRepresentation::kTagged, TheHoleConstant());
  Node* result = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
      native_context, then, thenable, resolve, reject);
  GotoIfException(result, &if_exception, &var_exception);
  Return(result);

  BIND(&if_exception);
  {
    // We need to reject the thenable.
    Node* result = CallJS(
        CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
        native_context, UndefinedConstant(), var_exception.value());
    Return(result);
  }
}

// ES section #sec-promisereactionjob
void PromiseBuiltinsAssembler::PromiseReactionJob(Node* context, Node* argument,
                                                  Node* handler,
                                                  Node* promise_or_capability,
                                                  PromiseReaction::Type type) {
  CSA_ASSERT(this, TaggedIsNotSmi(handler));
  CSA_ASSERT(this, Word32Or(IsUndefined(handler), IsCallable(handler)));
  CSA_ASSERT(this, TaggedIsNotSmi(promise_or_capability));
  CSA_ASSERT(this, Word32Or(IsJSPromise(promise_or_capability),
                            IsPromiseCapability(promise_or_capability)));

  VARIABLE(var_handler_result, MachineRepresentation::kTagged, argument);
  Label if_handler_callable(this), if_fulfill(this), if_reject(this),
      done(this);
  Branch(IsUndefined(handler),
         type == PromiseReaction::kFulfill ? &if_fulfill : &if_reject,
         &if_handler_callable);

  BIND(&if_handler_callable);
  {
    Node* const result = CallJS(
        CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
        context, handler, UndefinedConstant(), argument);
    GotoIfException(result, &if_reject, &var_handler_result);
    var_handler_result.Bind(result);
    Goto(&if_fulfill);
  }

  BIND(&if_fulfill);
  {
    Label if_promise(this), if_promise_capability(this, Label::kDeferred);
    Node* const value = var_handler_result.value();
    Branch(IsPromiseCapability(promise_or_capability), &if_promise_capability,
           &if_promise);

    BIND(&if_promise);
    {
      // For fast native promises we can skip the indirection
      // via the promiseCapability.[[Resolve]] function and
      // run the resolve logic directly from here.
      InternalResolvePromise(context, promise_or_capability, value);
      Goto(&done);
    }

    BIND(&if_promise_capability);
    {
      // In the general case we need to call the (user provided)
      // promiseCapability.[[Resolve]] function.
      Node* const resolve = LoadObjectField(promise_or_capability,
                                            PromiseCapability::kResolveOffset);
      Node* const result = CallJS(
          CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
          context, resolve, UndefinedConstant(), value);
      GotoIfException(result, &if_reject, &var_handler_result);
      Goto(&done);
    }
  }

  BIND(&if_reject);
  if (type == PromiseReaction::kReject) {
    Label if_promise(this), if_promise_capability(this, Label::kDeferred);
    Node* const reason = var_handler_result.value();
    Branch(IsPromiseCapability(promise_or_capability), &if_promise_capability,
           &if_promise);

    BIND(&if_promise);
    {
      // For fast native promises we can skip the indirection
      // via the promiseCapability.[[Reject]] function and
      // run the resolve logic directly from here.
      InternalPromiseReject(context, promise_or_capability, reason, false);
      Goto(&done);
    }

    BIND(&if_promise_capability);
    {
      // In the general case we need to call the (user provided)
      // promiseCapability.[[Reject]] function.
      Label if_exception(this, Label::kDeferred);
      VARIABLE(var_exception, MachineRepresentation::kTagged,
               TheHoleConstant());
      Node* const reject = LoadObjectField(promise_or_capability,
                                           PromiseCapability::kRejectOffset);
      Node* const result = CallJS(
          CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
          context, reject, UndefinedConstant(), reason);
      GotoIfException(result, &if_exception, &var_exception);
      Goto(&done);

      // Swallow the exception here.
      BIND(&if_exception);
      CallRuntime(Runtime::kReportMessage, context, var_exception.value());
      Goto(&done);
    }
  } else {
    // We have to call out to the dedicated PromiseRejectReactionJob builtin
    // here, instead of just doing the work inline, as otherwise the catch
    // predictions in the debugger will be wrong, which just walks the stack
    // and checks for certain builtins.
    CallBuiltin(Builtins::kPromiseRejectReactionJob, context,
                var_handler_result.value(), UndefinedConstant(),
                promise_or_capability);
    Goto(&done);
  }

  BIND(&done);
}

// ES section #sec-promisereactionjob
TF_BUILTIN(PromiseFulfillReactionJob, PromiseBuiltinsAssembler) {
  Node* const context = Parameter(Descriptor::kContext);
  Node* const value = Parameter(Descriptor::kValue);
  Node* const handler = Parameter(Descriptor::kHandler);
  Node* const promise_or_capability =
      Parameter(Descriptor::kPromiseOrCapability);

  PromiseReactionJob(context, value, handler, promise_or_capability,
                     PromiseReaction::kFulfill);
  Return(UndefinedConstant());
}

// ES section #sec-promisereactionjob
TF_BUILTIN(PromiseRejectReactionJob, PromiseBuiltinsAssembler) {
  Node* const context = Parameter(Descriptor::kContext);
  Node* const reason = Parameter(Descriptor::kReason);
  Node* const handler = Parameter(Descriptor::kHandler);
  Node* const promise_or_capability =
      Parameter(Descriptor::kPromiseOrCapability);

  PromiseReactionJob(context, reason, handler, promise_or_capability,
                     PromiseReaction::kReject);
  Return(UndefinedConstant());
}

TF_BUILTIN(PromiseResolveWrapper, PromiseBuiltinsAssembler) {
  //  1. Let C be the this value.
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* value = Parameter(Descriptor::kValue);
  Node* context = Parameter(Descriptor::kContext);

  // 2. If Type(C) is not Object, throw a TypeError exception.
  ThrowIfNotJSReceiver(context, receiver, MessageTemplate::kCalledOnNonObject,
                       "PromiseResolve");

  // 3. Return ? PromiseResolve(C, x).
  Return(CallBuiltin(Builtins::kPromiseResolve, context, receiver, value));
}

TF_BUILTIN(PromiseResolve, PromiseBuiltinsAssembler) {
  Node* constructor = Parameter(Descriptor::kConstructor);
  Node* value = Parameter(Descriptor::kValue);
  Node* context = Parameter(Descriptor::kContext);

  CSA_ASSERT(this, IsJSReceiver(constructor));

  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);

  Label if_slow_constructor(this, Label::kDeferred), if_need_to_allocate(this);

  // Check if {value} is a JSPromise.
  GotoIf(TaggedIsSmi(value), &if_need_to_allocate);
  Node* const value_map = LoadMap(value);
  GotoIfNot(IsJSPromiseMap(value_map), &if_need_to_allocate);

  // We can skip the "constructor" lookup on {value} if it's [[Prototype]]
  // is the (initial) Promise.prototype and the @@species protector is
  // intact, as that guards the lookup path for "constructor" on
  // JSPromise instances which have the (initial) Promise.prototype.
  Node* const promise_prototype =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  GotoIfNot(WordEqual(LoadMapPrototype(value_map), promise_prototype),
            &if_slow_constructor);
  GotoIf(IsSpeciesProtectorCellInvalid(), &if_slow_constructor);

  // If the {constructor} is the Promise function, we just immediately
  // return the {value} here and don't bother wrapping it into a
  // native Promise.
  GotoIfNot(WordEqual(promise_fun, constructor), &if_slow_constructor);
  Return(value);

  // At this point, value or/and constructor are not native promises, but
  // they could be of the same subclass.
  BIND(&if_slow_constructor);
  {
    Node* const value_constructor =
        GetProperty(context, value, isolate()->factory()->constructor_string());
    GotoIfNot(WordEqual(value_constructor, constructor), &if_need_to_allocate);
    Return(value);
  }

  BIND(&if_need_to_allocate);
  {
    Label if_nativepromise(this), if_notnativepromise(this, Label::kDeferred);
    Branch(WordEqual(promise_fun, constructor), &if_nativepromise,
           &if_notnativepromise);

    // This adds a fast path for native promises that don't need to
    // create NewPromiseCapability.
    BIND(&if_nativepromise);
    {
      Node* const result = AllocateAndInitJSPromise(context);
      InternalResolvePromise(context, result, value);
      Return(result);
    }

    BIND(&if_notnativepromise);
    {
      Node* const capability = NewPromiseCapability(context, constructor);

      Node* const resolve =
          LoadObjectField(capability, PromiseCapability::kResolveOffset);
      CallJS(
          CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
          context, resolve, UndefinedConstant(), value);

      Node* const result =
          LoadObjectField(capability, PromiseCapability::kPromiseOffset);
      Return(result);
    }
  }
}

// ES6 #sec-getcapabilitiesexecutor-functions
TF_BUILTIN(PromiseGetCapabilitiesExecutor, PromiseBuiltinsAssembler) {
  Node* const resolve = Parameter(Descriptor::kResolve);
  Node* const reject = Parameter(Descriptor::kReject);
  Node* const context = Parameter(Descriptor::kContext);

  Node* const capability = LoadContextElement(context, kCapabilitySlot);

  Label if_alreadyinvoked(this, Label::kDeferred);
  GotoIfNot(IsUndefined(
                LoadObjectField(capability, PromiseCapability::kResolveOffset)),
            &if_alreadyinvoked);
  GotoIfNot(IsUndefined(
                LoadObjectField(capability, PromiseCapability::kRejectOffset)),
            &if_alreadyinvoked);

  StoreObjectField(capability, PromiseCapability::kResolveOffset, resolve);
  StoreObjectField(capability, PromiseCapability::kRejectOffset, reject);

  Return(UndefinedConstant());

  BIND(&if_alreadyinvoked);
  ThrowTypeError(context, MessageTemplate::kPromiseExecutorAlreadyInvoked);
}

// ES6 #sec-newpromisecapability
TF_BUILTIN(NewPromiseCapability, PromiseBuiltinsAssembler) {
  Node* constructor = Parameter(Descriptor::kConstructor);
  Node* debug_event = Parameter(Descriptor::kDebugEvent);
  Node* context = Parameter(Descriptor::kContext);

  CSA_ASSERT_JS_ARGC_EQ(this, 2);

  Return(NewPromiseCapability(context, constructor, debug_event));
}

TF_BUILTIN(PromiseReject, PromiseBuiltinsAssembler) {
  // 1. Let C be the this value.
  Node* const receiver = Parameter(Descriptor::kReceiver);
  Node* const reason = Parameter(Descriptor::kReason);
  Node* const context = Parameter(Descriptor::kContext);

  // 2. If Type(C) is not Object, throw a TypeError exception.
  ThrowIfNotJSReceiver(context, receiver, MessageTemplate::kCalledOnNonObject,
                       "PromiseReject");

  Label if_nativepromise(this), if_custompromise(this, Label::kDeferred);
  Node* const native_context = LoadNativeContext(context);

  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Branch(WordEqual(promise_fun, receiver), &if_nativepromise,
         &if_custompromise);

  BIND(&if_nativepromise);
  {
    Node* const promise =
        AllocateAndSetJSPromise(context, v8::Promise::kRejected, reason);
    CallRuntime(Runtime::kPromiseRejectEventFromStack, context, promise,
                reason);
    Return(promise);
  }

  BIND(&if_custompromise);
  {
    // 3. Let promiseCapability be ? NewPromiseCapability(C).
    Node* const capability = NewPromiseCapability(context, receiver);

    // 4. Perform ? Call(promiseCapability.[[Reject]], undefined, « r »).
    Node* const reject =
        LoadObjectField(capability, PromiseCapability::kRejectOffset);
    CallJS(CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
           context, reject, UndefinedConstant(), reason);

    // 5. Return promiseCapability.[[Promise]].
    Node* const promise =
        LoadObjectField(capability, PromiseCapability::kPromiseOffset);
    Return(promise);
  }
}

TF_BUILTIN(InternalPromiseReject, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const reason = Parameter(Descriptor::kReason);
  Node* const debug_event = Parameter(Descriptor::kDebugEvent);
  Node* const context = Parameter(Descriptor::kContext);

  InternalPromiseReject(context, promise, reason, debug_event);
  Return(UndefinedConstant());
}

std::pair<Node*, Node*> PromiseBuiltinsAssembler::CreatePromiseFinallyFunctions(
    Node* on_finally, Node* constructor, Node* native_context) {
  Node* const promise_context =
      CreatePromiseContext(native_context, kPromiseFinallyContextLength);
  StoreContextElementNoWriteBarrier(promise_context, kOnFinallySlot,
                                    on_finally);
  StoreContextElementNoWriteBarrier(promise_context, kConstructorSlot,
                                    constructor);
  Node* const map = LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
  Node* const then_finally_info = LoadContextElement(
      native_context, Context::PROMISE_THEN_FINALLY_SHARED_FUN);
  Node* const then_finally = AllocateFunctionWithMapAndContext(
      map, then_finally_info, promise_context);
  Node* const catch_finally_info = LoadContextElement(
      native_context, Context::PROMISE_CATCH_FINALLY_SHARED_FUN);
  Node* const catch_finally = AllocateFunctionWithMapAndContext(
      map, catch_finally_info, promise_context);
  return std::make_pair(then_finally, catch_finally);
}

TF_BUILTIN(PromiseValueThunkFinally, PromiseBuiltinsAssembler) {
  Node* const context = Parameter(Descriptor::kContext);

  Node* const value = LoadContextElement(context, kValueSlot);
  Return(value);
}

Node* PromiseBuiltinsAssembler::CreateValueThunkFunction(Node* value,
                                                         Node* native_context) {
  Node* const value_thunk_context = CreatePromiseContext(
      native_context, kPromiseValueThunkOrReasonContextLength);
  StoreContextElementNoWriteBarrier(value_thunk_context, kValueSlot, value);
  Node* const map = LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
  Node* const value_thunk_info = LoadContextElement(
      native_context, Context::PROMISE_VALUE_THUNK_FINALLY_SHARED_FUN);
  Node* const value_thunk = AllocateFunctionWithMapAndContext(
      map, value_thunk_info, value_thunk_context);
  return value_thunk;
}

TF_BUILTIN(PromiseThenFinally, PromiseBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 1);

  Node* const value = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  // 1. Let onFinally be F.[[OnFinally]].
  Node* const on_finally = LoadContextElement(context, kOnFinallySlot);

  // 2.  Assert: IsCallable(onFinally) is true.
  CSA_ASSERT(this, IsCallable(on_finally));

  // 3. Let result be ?  Call(onFinally).
  Node* const result = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
      context, on_finally, UndefinedConstant());

  // 4. Let C be F.[[Constructor]].
  Node* const constructor = LoadContextElement(context, kConstructorSlot);

  // 5. Assert: IsConstructor(C) is true.
  CSA_ASSERT(this, IsConstructor(constructor));

  // 6. Let promise be ? PromiseResolve(C, result).
  Node* const promise =
    CallBuiltin(Builtins::kPromiseResolve, context, constructor, result);

  // 7. Let valueThunk be equivalent to a function that returns value.
  Node* native_context = LoadNativeContext(context);
  Node* const value_thunk = CreateValueThunkFunction(value, native_context);

  // 8. Return ? Invoke(promise, "then", « valueThunk »).
  Node* const promise_then =
    GetProperty(context, promise, factory()->then_string());
  Node* const result_promise = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
      context, promise_then, promise, value_thunk);
  Return(result_promise);
}

TF_BUILTIN(PromiseThrowerFinally, PromiseBuiltinsAssembler) {
  Node* const context = Parameter(Descriptor::kContext);

  Node* const reason = LoadContextElement(context, kValueSlot);
  CallRuntime(Runtime::kThrow, context, reason);
  Unreachable();
}

Node* PromiseBuiltinsAssembler::CreateThrowerFunction(Node* reason,
                                                      Node* native_context) {
  Node* const thrower_context = CreatePromiseContext(
      native_context, kPromiseValueThunkOrReasonContextLength);
  StoreContextElementNoWriteBarrier(thrower_context, kValueSlot, reason);
  Node* const map = LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
  Node* const thrower_info = LoadContextElement(
      native_context, Context::PROMISE_THROWER_FINALLY_SHARED_FUN);
  Node* const thrower =
      AllocateFunctionWithMapAndContext(map, thrower_info, thrower_context);
  return thrower;
}

TF_BUILTIN(PromiseCatchFinally, PromiseBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 1);

  Node* const reason = Parameter(Descriptor::kReason);
  Node* const context = Parameter(Descriptor::kContext);

  // 1. Let onFinally be F.[[OnFinally]].
  Node* const on_finally = LoadContextElement(context, kOnFinallySlot);

  // 2. Assert: IsCallable(onFinally) is true.
  CSA_ASSERT(this, IsCallable(on_finally));

  // 3. Let result be ? Call(onFinally).
  Node* result = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
      context, on_finally, UndefinedConstant());

  // 4. Let C be F.[[Constructor]].
  Node* const constructor = LoadContextElement(context, kConstructorSlot);

  // 5. Assert: IsConstructor(C) is true.
  CSA_ASSERT(this, IsConstructor(constructor));

  // 6. Let promise be ? PromiseResolve(C, result).
  Node* const promise =
    CallBuiltin(Builtins::kPromiseResolve, context, constructor, result);

  // 7. Let thrower be equivalent to a function that throws reason.
  Node* native_context = LoadNativeContext(context);
  Node* const thrower = CreateThrowerFunction(reason, native_context);

  // 8. Return ? Invoke(promise, "then", « thrower »).
  Node* const promise_then =
    GetProperty(context, promise, factory()->then_string());
  Node* const result_promise = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
      context, promise_then, promise, thrower);
  Return(result_promise);
}

TF_BUILTIN(PromisePrototypeFinally, PromiseBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 1);

  // 1.  Let promise be the this value.
  Node* const promise = Parameter(Descriptor::kReceiver);
  Node* const on_finally = Parameter(Descriptor::kOnFinally);
  Node* const context = Parameter(Descriptor::kContext);

  // 2. If Type(promise) is not Object, throw a TypeError exception.
  ThrowIfNotJSReceiver(context, promise, MessageTemplate::kCalledOnNonObject,
                       "Promise.prototype.finally");

  // 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const constructor = SpeciesConstructor(context, promise, promise_fun);

  // 4. Assert: IsConstructor(C) is true.
  CSA_ASSERT(this, IsConstructor(constructor));

  VARIABLE(var_then_finally, MachineRepresentation::kTagged);
  VARIABLE(var_catch_finally, MachineRepresentation::kTagged);

  Label if_notcallable(this, Label::kDeferred), perform_finally(this);

  GotoIf(TaggedIsSmi(on_finally), &if_notcallable);
  GotoIfNot(IsCallable(on_finally), &if_notcallable);

  // 6. Else,
  //   a. Let thenFinally be a new built-in function object as defined
  //   in ThenFinally Function.
  //   b. Let catchFinally be a new built-in function object as
  //   defined in CatchFinally Function.
  //   c. Set thenFinally and catchFinally's [[Constructor]] internal
  //   slots to C.
  //   d. Set thenFinally and catchFinally's [[OnFinally]] internal
  //   slots to onFinally.
  Node* then_finally = nullptr;
  Node* catch_finally = nullptr;
  std::tie(then_finally, catch_finally) =
    CreatePromiseFinallyFunctions(on_finally, constructor, native_context);
  var_then_finally.Bind(then_finally);
  var_catch_finally.Bind(catch_finally);
  Goto(&perform_finally);

  // 5. If IsCallable(onFinally) is not true,
  //    a. Let thenFinally be onFinally.
  //    b. Let catchFinally be onFinally.
  BIND(&if_notcallable);
  {
    var_then_finally.Bind(on_finally);
    var_catch_finally.Bind(on_finally);
    Goto(&perform_finally);
  }

  // 7. Return ? Invoke(promise, "then", « thenFinally, catchFinally »).
  BIND(&perform_finally);
  Node* const promise_then =
    GetProperty(context, promise, factory()->then_string());
  Node* const result_promise = CallJS(
      CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
      context, promise_then, promise, var_then_finally.value(),
      var_catch_finally.value());
  Return(result_promise);
}

TF_BUILTIN(ResolveNativePromise, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const value = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  CSA_ASSERT(this, HasInstanceType(promise, JS_PROMISE_TYPE));
  InternalResolvePromise(context, promise, value);
  Return(UndefinedConstant());
}

TF_BUILTIN(RejectNativePromise, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const value = Parameter(Descriptor::kValue);
  Node* const debug_event = Parameter(Descriptor::kDebugEvent);
  Node* const context = Parameter(Descriptor::kContext);

  CSA_ASSERT(this, HasInstanceType(promise, JS_PROMISE_TYPE));
  CSA_ASSERT(this, IsBoolean(debug_event));
  InternalPromiseReject(context, promise, value, debug_event);
  Return(UndefinedConstant());
}

TF_BUILTIN(PerformNativePromiseThen, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const resolve_reaction = Parameter(Descriptor::kResolveReaction);
  Node* const reject_reaction = Parameter(Descriptor::kRejectReaction);
  Node* const result_promise = Parameter(Descriptor::kResultPromise);
  Node* const context = Parameter(Descriptor::kContext);

  CSA_ASSERT(this, IsJSPromise(result_promise));

  InternalPerformPromiseThen(context, promise, resolve_reaction,
                             reject_reaction, result_promise);
  Return(result_promise);
}

Node* PromiseBuiltinsAssembler::PerformPromiseAll(
    Node* context, Node* constructor, Node* capability,
    const IteratorRecord& iterator, Label* if_exception,
    Variable* var_exception) {
  IteratorBuiltinsAssembler iter_assembler(state());
  Label close_iterator(this);

  Node* const instrumenting = IsDebugActive();

  // For catch prediction, don't treat the .then calls as handling it;
  // instead, recurse outwards.
  SetForwardingHandlerIfTrue(
      context, instrumenting,
      LoadObjectField(capability, PromiseCapability::kRejectOffset));

  Node* const native_context = LoadNativeContext(context);
  Node* const array_map = LoadContextElement(
      native_context, Context::JS_ARRAY_PACKED_ELEMENTS_MAP_INDEX);
  Node* const values_array = AllocateJSArray(PACKED_ELEMENTS, array_map,
                                             IntPtrConstant(0), SmiConstant(0));
  Node* const remaining_elements = AllocateSmiCell(1);

  VARIABLE(var_index, MachineRepresentation::kTagged, SmiConstant(0));

  Label loop(this, &var_index), break_loop(this);
  Goto(&loop);
  BIND(&loop);
  {
    // Let next be IteratorStep(iteratorRecord.[[Iterator]]).
    // If next is an abrupt completion, set iteratorRecord.[[Done]] to true.
    // ReturnIfAbrupt(next).
    Node* const fast_iterator_result_map =
        LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX);
    Node* const next = iter_assembler.IteratorStep(
        context, iterator, &break_loop, fast_iterator_result_map, if_exception,
        var_exception);

    // Let nextValue be IteratorValue(next).
    // If nextValue is an abrupt completion, set iteratorRecord.[[Done]] to
    //     true.
    // ReturnIfAbrupt(nextValue).
    Node* const next_value = iter_assembler.IteratorValue(
        context, next, fast_iterator_result_map, if_exception, var_exception);

    // Let nextPromise be ? Invoke(constructor, "resolve", « nextValue »).
    Node* const promise_resolve =
        GetProperty(context, constructor, factory()->resolve_string());
    GotoIfException(promise_resolve, &close_iterator, var_exception);

    Node* const next_promise = CallJS(
        CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
        context, promise_resolve, constructor, next_value);
    GotoIfException(next_promise, &close_iterator, var_exception);

    // Let resolveElement be a new built-in function object as defined in
    // Promise.all Resolve Element Functions.
    Node* const resolve_context =
        CreatePromiseContext(native_context, kPromiseAllResolveElementLength);
    StoreContextElementNoWriteBarrier(
        resolve_context, kPromiseAllResolveElementAlreadyVisitedSlot,
        SmiConstant(0));
    StoreContextElementNoWriteBarrier(
        resolve_context, kPromiseAllResolveElementIndexSlot, var_index.value());
    StoreContextElementNoWriteBarrier(
        resolve_context, kPromiseAllResolveElementRemainingElementsSlot,
        remaining_elements);
    StoreContextElementNoWriteBarrier(
        resolve_context, kPromiseAllResolveElementCapabilitySlot, capability);
    StoreContextElementNoWriteBarrier(resolve_context,
                                      kPromiseAllResolveElementValuesArraySlot,
                                      values_array);

    Node* const map = LoadContextElement(
        native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
    Node* const resolve_info = LoadContextElement(
        native_context, Context::PROMISE_ALL_RESOLVE_ELEMENT_SHARED_FUN);
    Node* const resolve =
        AllocateFunctionWithMapAndContext(map, resolve_info, resolve_context);

    // Set remainingElementsCount.[[Value]] to
    //     remainingElementsCount.[[Value]] + 1.
    {
      Label if_outofrange(this, Label::kDeferred), done(this);
      IncrementSmiCell(remaining_elements, &if_outofrange);
      Goto(&done);

      BIND(&if_outofrange);
      {
        // If the incremented value is out of Smi range, crash.
        Abort(AbortReason::kOffsetOutOfRange);
      }

      BIND(&done);
    }

    // Perform ? Invoke(nextPromise, "then", « resolveElement,
    //                  resultCapability.[[Reject]] »).
    Node* const then =
        GetProperty(context, next_promise, factory()->then_string());
    GotoIfException(then, &close_iterator, var_exception);

    Node* const then_call = CallJS(
        CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined),
        context, then, next_promise, resolve,
        LoadObjectField(capability, PromiseCapability::kRejectOffset));
    GotoIfException(then_call, &close_iterator, var_exception);

    // For catch prediction, mark that rejections here are semantically
    // handled by the combined Promise.
    SetPromiseHandledByIfTrue(context, instrumenting, then_call, [=]() {
      // Load promiseCapability.[[Promise]]
      return LoadObjectField(capability, PromiseCapability::kPromiseOffset);
    });

    // Set index to index + 1
    var_index.Bind(NumberInc(var_index.value()));
    Goto(&loop);
  }

  BIND(&close_iterator);
  {
    // Exception must be bound to a JS value.
    CSA_ASSERT(this, IsNotTheHole(var_exception->value()));
    iter_assembler.IteratorCloseOnException(context, iterator, if_exception,
                                            var_exception);
  }

  BIND(&break_loop);
  {
    Label resolve_promise(this), return_promise(this);
    // Set iteratorRecord.[[Done]] to true.
    // Set remainingElementsCount.[[Value]] to
    //    remainingElementsCount.[[Value]] - 1.
    Node* const remaining = DecrementSmiCell(remaining_elements);
    Branch(SmiEqual(remaining, SmiConstant(0)), &resolve_promise,
           &return_promise);

    // If remainingElementsCount.[[Value]] is 0, then
    //     Let valuesArray be CreateArrayFromList(values).
    //     Perform ? Call(resultCapability.[[Resolve]], undefined,
    //                    « valuesArray »).
    BIND(&resolve_promise);

    Node* const resolve =
        LoadObjectField(capability, PromiseCapability::kResolveOffset);
    Node* const resolve_call = CallJS(
        CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
        context, resolve, UndefinedConstant(), values_array);
    GotoIfException(resolve_call, if_exception, var_exception);
    Goto(&return_promise);

    // Return resultCapability.[[Promise]].
    BIND(&return_promise);
  }

  Node* const promise =
      LoadObjectField(capability, PromiseCapability::kPromiseOffset);
  return promise;
}

Node* PromiseBuiltinsAssembler::IncrementSmiCell(Node* cell,
                                                 Label* if_overflow) {
  CSA_SLOW_ASSERT(this, HasInstanceType(cell, CELL_TYPE));
  Node* value = LoadCellValue(cell);
  CSA_SLOW_ASSERT(this, TaggedIsSmi(value));

  if (if_overflow != nullptr) {
    GotoIf(SmiEqual(value, SmiConstant(Smi::kMaxValue)), if_overflow);
  }

  Node* result = SmiAdd(value, SmiConstant(1));
  StoreCellValue(cell, result, SKIP_WRITE_BARRIER);
  return result;
}

Node* PromiseBuiltinsAssembler::DecrementSmiCell(Node* cell) {
  CSA_SLOW_ASSERT(this, HasInstanceType(cell, CELL_TYPE));
  Node* value = LoadCellValue(cell);
  CSA_SLOW_ASSERT(this, TaggedIsSmi(value));

  Node* result = SmiSub(value, SmiConstant(1));
  StoreCellValue(cell, result, SKIP_WRITE_BARRIER);
  return result;
}

// ES#sec-promise.all
// Promise.all ( iterable )
TF_BUILTIN(PromiseAll, PromiseBuiltinsAssembler) {
  IteratorBuiltinsAssembler iter_assembler(state());

  // Let C be the this value.
  // If Type(C) is not Object, throw a TypeError exception.
  Node* const receiver = Parameter(Descriptor::kReceiver);
  Node* const context = Parameter(Descriptor::kContext);
  ThrowIfNotJSReceiver(context, receiver, MessageTemplate::kCalledOnNonObject,
                       "Promise.all");

  // Let promiseCapability be ? NewPromiseCapability(C).
  // Don't fire debugEvent so that forwarding the rejection through all does not
  // trigger redundant ExceptionEvents
  Node* const debug_event = FalseConstant();
  Node* const capability = NewPromiseCapability(context, receiver, debug_event);

  VARIABLE(var_exception, MachineRepresentation::kTagged, TheHoleConstant());
  Label reject_promise(this, &var_exception, Label::kDeferred);

  // Let iterator be GetIterator(iterable).
  // IfAbruptRejectPromise(iterator, promiseCapability).
  Node* const iterable = Parameter(Descriptor::kIterable);
  IteratorRecord iterator = iter_assembler.GetIterator(
      context, iterable, &reject_promise, &var_exception);

  // Let result be PerformPromiseAll(iteratorRecord, C, promiseCapability).
  // If result is an abrupt completion, then
  //   If iteratorRecord.[[Done]] is false, let result be
  //       IteratorClose(iterator, result).
  //    IfAbruptRejectPromise(result, promiseCapability).
  Node* const result = PerformPromiseAll(
      context, receiver, capability, iterator, &reject_promise, &var_exception);

  Return(result);

  BIND(&reject_promise);
  {
    // Exception must be bound to a JS value.
    CSA_SLOW_ASSERT(this, IsNotTheHole(var_exception.value()));
    Node* const reject =
        LoadObjectField(capability, PromiseCapability::kRejectOffset);
    CallJS(CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
           context, reject, UndefinedConstant(), var_exception.value());

    Node* const promise =
        LoadObjectField(capability, PromiseCapability::kPromiseOffset);
    Return(promise);
  }
}

TF_BUILTIN(PromiseAllResolveElementClosure, PromiseBuiltinsAssembler) {
  Node* const value = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  CSA_ASSERT(this, SmiEqual(LoadFixedArrayBaseLength(context),
                            SmiConstant(kPromiseAllResolveElementLength)));

  Label already_called(this), resolve_promise(this);
  GotoIf(SmiEqual(LoadContextElement(
                      context, kPromiseAllResolveElementAlreadyVisitedSlot),
                  SmiConstant(1)),
         &already_called);
  StoreContextElementNoWriteBarrier(
      context, kPromiseAllResolveElementAlreadyVisitedSlot, SmiConstant(1));

  Node* const index =
      LoadContextElement(context, kPromiseAllResolveElementIndexSlot);
  Node* const values_array =
      LoadContextElement(context, kPromiseAllResolveElementValuesArraySlot);

  // Set element in FixedArray
  Label runtime_set_element(this), did_set_element(this);
  GotoIfNot(TaggedIsPositiveSmi(index), &runtime_set_element);
  {
    VARIABLE(var_elements, MachineRepresentation::kTagged,
             LoadElements(values_array));
    PossiblyGrowElementsCapacity(SMI_PARAMETERS, PACKED_ELEMENTS, values_array,
                                 index, &var_elements, SmiConstant(1),
                                 &runtime_set_element);
    StoreFixedArrayElement(var_elements.value(), index, value,
                           UPDATE_WRITE_BARRIER, 0, SMI_PARAMETERS);

    // Update array length
    Label did_set_length(this);
    Node* const length = LoadJSArrayLength(values_array);
    GotoIfNot(TaggedIsPositiveSmi(length), &did_set_length);
    Node* const new_length = SmiAdd(index, SmiConstant(1));
    GotoIfNot(SmiLessThan(length, new_length), &did_set_length);
    StoreObjectFieldNoWriteBarrier(values_array, JSArray::kLengthOffset,
                                   new_length);
    // Assert that valuesArray.[[Length]] is less than or equal to the
    // elements backing-store length.e
    CSA_SLOW_ASSERT(
        this, SmiAboveOrEqual(LoadFixedArrayBaseLength(var_elements.value()),
                              new_length));
    Goto(&did_set_length);
    BIND(&did_set_length);
  }
  Goto(&did_set_element);
  BIND(&runtime_set_element);
  // New-space filled up or index too large, set element via runtime
  CallRuntime(Runtime::kCreateDataProperty, context, values_array, index,
              value);
  Goto(&did_set_element);
  BIND(&did_set_element);

  Node* const remaining_elements = LoadContextElement(
      context, kPromiseAllResolveElementRemainingElementsSlot);
  Node* const result = DecrementSmiCell(remaining_elements);
  GotoIf(SmiEqual(result, SmiConstant(0)), &resolve_promise);
  Return(UndefinedConstant());

  BIND(&resolve_promise);
  Node* const capability =
      LoadContextElement(context, kPromiseAllResolveElementCapabilitySlot);
  Node* const resolve =
      LoadObjectField(capability, PromiseCapability::kResolveOffset);
  CallJS(CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
         context, resolve, UndefinedConstant(), values_array);
  Return(UndefinedConstant());

  BIND(&already_called);
  Return(UndefinedConstant());
}

// ES#sec-promise.race
// Promise.race ( iterable )
TF_BUILTIN(PromiseRace, PromiseBuiltinsAssembler) {
  IteratorBuiltinsAssembler iter_assembler(state());
  VARIABLE(var_exception, MachineRepresentation::kTagged, TheHoleConstant());

  Node* const receiver = Parameter(Descriptor::kReceiver);
  Node* const context = Parameter(Descriptor::kContext);
  ThrowIfNotJSReceiver(context, receiver, MessageTemplate::kCalledOnNonObject,
                       "Promise.race");

  // Let promiseCapability be ? NewPromiseCapability(C).
  // Don't fire debugEvent so that forwarding the rejection through all does not
  // trigger redundant ExceptionEvents
  Node* const debug_event = FalseConstant();
  Node* const capability = NewPromiseCapability(context, receiver, debug_event);

  Node* const resolve =
      LoadObjectField(capability, PromiseCapability::kResolveOffset);
  Node* const reject =
      LoadObjectField(capability, PromiseCapability::kRejectOffset);

  Node* const instrumenting = IsDebugActive();

  Label close_iterator(this, Label::kDeferred);
  Label reject_promise(this, Label::kDeferred);

  // For catch prediction, don't treat the .then calls as handling it;
  // instead, recurse outwards.
  SetForwardingHandlerIfTrue(context, instrumenting, reject);

  // Let iterator be GetIterator(iterable).
  // IfAbruptRejectPromise(iterator, promiseCapability).
  Node* const iterable = Parameter(Descriptor::kIterable);
  IteratorRecord iterator = iter_assembler.GetIterator(
      context, iterable, &reject_promise, &var_exception);

  // Let result be PerformPromiseRace(iteratorRecord, C, promiseCapability).
  {
    Label loop(this), break_loop(this);
    Goto(&loop);
    BIND(&loop);
    {
      Node* const native_context = LoadNativeContext(context);
      Node* const fast_iterator_result_map = LoadContextElement(
          native_context, Context::ITERATOR_RESULT_MAP_INDEX);

      // Let next be IteratorStep(iteratorRecord.[[Iterator]]).
      // If next is an abrupt completion, set iteratorRecord.[[Done]] to true.
      // ReturnIfAbrupt(next).
      Node* const next = iter_assembler.IteratorStep(
          context, iterator, &break_loop, fast_iterator_result_map,
          &reject_promise, &var_exception);

      // Let nextValue be IteratorValue(next).
      // If nextValue is an abrupt completion, set iteratorRecord.[[Done]] to
      //     true.
      // ReturnIfAbrupt(nextValue).
      Node* const next_value =
          iter_assembler.IteratorValue(context, next, fast_iterator_result_map,
                                       &reject_promise, &var_exception);

      // Let nextPromise be ? Invoke(constructor, "resolve", « nextValue »).
      Node* const promise_resolve =
          GetProperty(context, receiver, factory()->resolve_string());
      GotoIfException(promise_resolve, &close_iterator, &var_exception);

      Node* const next_promise =
          CallJS(CodeFactory::Call(isolate(),
                                   ConvertReceiverMode::kNotNullOrUndefined),
                 context, promise_resolve, receiver, next_value);
      GotoIfException(next_promise, &close_iterator, &var_exception);

      // Perform ? Invoke(nextPromise, "then", « resolveElement,
      //                  resultCapability.[[Reject]] »).
      Node* const then =
          GetProperty(context, next_promise, factory()->then_string());
      GotoIfException(then, &close_iterator, &var_exception);

      Node* const then_call =
          CallJS(CodeFactory::Call(isolate(),
                                   ConvertReceiverMode::kNotNullOrUndefined),
                 context, then, next_promise, resolve, reject);
      GotoIfException(then_call, &close_iterator, &var_exception);

      // For catch prediction, mark that rejections here are semantically
      // handled by the combined Promise.
      SetPromiseHandledByIfTrue(context, instrumenting, then_call, [=]() {
        // Load promiseCapability.[[Promise]]
        return LoadObjectField(capability, PromiseCapability::kPromiseOffset);
      });
      Goto(&loop);
    }

    BIND(&break_loop);
    Return(LoadObjectField(capability, PromiseCapability::kPromiseOffset));
  }

  BIND(&close_iterator);
  {
    CSA_ASSERT(this, IsNotTheHole(var_exception.value()));
    iter_assembler.IteratorCloseOnException(context, iterator, &reject_promise,
                                            &var_exception);
  }

  BIND(&reject_promise);
  {
    Node* const reject =
        LoadObjectField(capability, PromiseCapability::kRejectOffset);
    CallJS(CodeFactory::Call(isolate(), ConvertReceiverMode::kNullOrUndefined),
           context, reject, UndefinedConstant(), var_exception.value());

    Node* const promise =
        LoadObjectField(capability, PromiseCapability::kPromiseOffset);
    Return(promise);
  }
}

}  // namespace internal
}  // namespace v8
