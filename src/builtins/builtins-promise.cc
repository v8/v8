// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-promise.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/promise-utils.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;
typedef CodeStubAssembler::ParameterMode ParameterMode;
typedef compiler::CodeAssemblerState CodeAssemblerState;

Node* PromiseBuiltinsAssembler::ThrowIfNotJSReceiver(
    Node* context, Node* value, MessageTemplate::Template msg_template) {
  Label out(this), throw_exception(this, Label::kDeferred);
  Variable var_value_map(this, MachineRepresentation::kTagged);

  GotoIf(TaggedIsSmi(value), &throw_exception);

  // Load the instance type of the {value}.
  var_value_map.Bind(LoadMap(value));
  Node* const value_instance_type = LoadMapInstanceType(var_value_map.value());

  Branch(IsJSReceiverInstanceType(value_instance_type), &out, &throw_exception);

  // The {value} is not a compatible receiver for this method.
  Bind(&throw_exception);
  {
    Node* const message_id = SmiConstant(msg_template);
    CallRuntime(Runtime::kThrowTypeError, context, message_id);
    var_value_map.Bind(UndefinedConstant());
    Goto(&out);  // Never reached.
  }

  Bind(&out);
  return var_value_map.value();
}

Node* PromiseBuiltinsAssembler::PromiseHasHandler(Node* promise) {
  Node* const flags = LoadObjectField(promise, JSPromise::kFlagsOffset);
  return IsSetWord(SmiUntag(flags), 1 << JSPromise::kHasHandlerBit);
}

void PromiseBuiltinsAssembler::PromiseSetHasHandler(Node* promise) {
  Node* const flags =
      SmiUntag(LoadObjectField(promise, JSPromise::kFlagsOffset));
  Node* const new_flags =
      WordOr(flags, IntPtrConstant(1 << JSPromise::kHasHandlerBit));
  StoreObjectField(promise, JSPromise::kFlagsOffset, SmiTag(new_flags));
}

Node* PromiseBuiltinsAssembler::SpeciesConstructor(Node* context, Node* object,
                                                   Node* default_constructor) {
  Isolate* isolate = this->isolate();
  Variable var_result(this, MachineRepresentation::kTagged);
  var_result.Bind(default_constructor);

  // 2. Let C be ? Get(O, "constructor").
  Node* const constructor_str =
      HeapConstant(isolate->factory()->constructor_string());
  Callable getproperty_callable = CodeFactory::GetProperty(isolate);
  Node* const constructor =
      CallStub(getproperty_callable, context, object, constructor_str);

  // 3. If C is undefined, return defaultConstructor.
  Label out(this);
  GotoIf(IsUndefined(constructor), &out);

  // 4. If Type(C) is not Object, throw a TypeError exception.
  ThrowIfNotJSReceiver(context, constructor,
                       MessageTemplate::kConstructorNotReceiver);

  // 5. Let S be ? Get(C, @@species).
  Node* const species_symbol =
      HeapConstant(isolate->factory()->species_symbol());
  Node* const species =
      CallStub(getproperty_callable, context, constructor, species_symbol);

  // 6. If S is either undefined or null, return defaultConstructor.
  GotoIf(IsUndefined(species), &out);
  GotoIf(WordEqual(species, NullConstant()), &out);

  // 7. If IsConstructor(S) is true, return S.
  Label throw_error(this);
  Node* species_bitfield = LoadMapBitField(LoadMap(species));
  GotoUnless(Word32Equal(Word32And(species_bitfield,
                                   Int32Constant((1 << Map::kIsConstructor))),
                         Int32Constant(1 << Map::kIsConstructor)),
             &throw_error);
  var_result.Bind(species);
  Goto(&out);

  // 8. Throw a TypeError exception.
  Bind(&throw_error);
  {
    Node* const message_id =
        SmiConstant(MessageTemplate::kSpeciesNotConstructor);
    CallRuntime(Runtime::kThrowTypeError, context, message_id);
    Goto(&out);
  }

  Bind(&out);
  return var_result.value();
}

void PromiseBuiltinsAssembler::AppendPromiseCallback(int offset, Node* promise,
                                                     Node* value) {
  Node* elements = LoadObjectField(promise, offset);
  Node* length = LoadFixedArrayBaseLength(elements);
  CodeStubAssembler::ParameterMode mode = OptimalParameterMode();
  length = TaggedToParameter(length, mode);

  Node* delta = IntPtrOrSmiConstant(1, mode);
  Node* new_capacity = IntPtrOrSmiAdd(length, delta, mode);

  const ElementsKind kind = FAST_ELEMENTS;
  const WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER;
  const CodeStubAssembler::AllocationFlags flags =
      CodeStubAssembler::kAllowLargeObjectAllocation;
  int additional_offset = 0;

  Node* new_elements = AllocateFixedArray(kind, new_capacity, mode, flags);

  CopyFixedArrayElements(kind, elements, new_elements, length, barrier_mode,
                         mode);
  StoreFixedArrayElement(new_elements, length, value, barrier_mode,
                         additional_offset, mode);

  StoreObjectField(promise, offset, new_elements);
}

Node* PromiseBuiltinsAssembler::InternalPerformPromiseThen(Node* context,
                                                           Node* promise,
                                                           Node* on_resolve,
                                                           Node* on_reject,
                                                           Node* deferred) {
  Node* const native_context = LoadNativeContext(context);

  Variable var_on_resolve(this, MachineRepresentation::kTagged),
      var_on_reject(this, MachineRepresentation::kTagged);

  var_on_resolve.Bind(on_resolve);
  var_on_reject.Bind(on_reject);

  Label out(this), if_onresolvenotcallable(this), onrejectcheck(this),
      append_callbacks(this);
  GotoIf(TaggedIsSmi(on_resolve), &if_onresolvenotcallable);

  Node* const on_resolve_map = LoadMap(on_resolve);
  Branch(IsCallableMap(on_resolve_map), &onrejectcheck,
         &if_onresolvenotcallable);

  Bind(&if_onresolvenotcallable);
  {
    var_on_resolve.Bind(LoadContextElement(
        native_context, Context::PROMISE_ID_RESOLVE_HANDLER_INDEX));
    Goto(&onrejectcheck);
  }

  Bind(&onrejectcheck);
  {
    Label if_onrejectnotcallable(this);
    GotoIf(TaggedIsSmi(on_reject), &if_onrejectnotcallable);

    Node* const on_reject_map = LoadMap(on_reject);
    Branch(IsCallableMap(on_reject_map), &append_callbacks,
           &if_onrejectnotcallable);

    Bind(&if_onrejectnotcallable);
    {
      var_on_reject.Bind(LoadContextElement(
          native_context, Context::PROMISE_ID_REJECT_HANDLER_INDEX));
      Goto(&append_callbacks);
    }
  }

  Bind(&append_callbacks);
  {
    Label fulfilled_check(this);
    Node* const status = LoadObjectField(promise, JSPromise::kStatusOffset);
    GotoUnless(SmiEqual(status, SmiConstant(kPromisePending)),
               &fulfilled_check);

    Node* const existing_deferred =
        LoadObjectField(promise, JSPromise::kDeferredOffset);

    Label if_noexistingcallbacks(this), if_existingcallbacks(this);
    Branch(IsUndefined(existing_deferred), &if_noexistingcallbacks,
           &if_existingcallbacks);

    Bind(&if_noexistingcallbacks);
    {
      // Store callbacks directly in the slots.
      StoreObjectField(promise, JSPromise::kDeferredOffset, deferred);
      StoreObjectField(promise, JSPromise::kFulfillReactionsOffset,
                       var_on_resolve.value());
      StoreObjectField(promise, JSPromise::kRejectReactionsOffset,
                       var_on_reject.value());
      Goto(&out);
    }

    Bind(&if_existingcallbacks);
    {
      Label if_singlecallback(this), if_multiplecallbacks(this);
      BranchIfJSObject(existing_deferred, &if_singlecallback,
                       &if_multiplecallbacks);

      Bind(&if_singlecallback);
      {
        // Create new FixedArrays to store callbacks, and migrate
        // existing callbacks.
        Node* const deferreds =
            AllocateFixedArray(FAST_ELEMENTS, IntPtrConstant(2));
        StoreFixedArrayElement(deferreds, 0, existing_deferred);
        StoreFixedArrayElement(deferreds, 1, deferred);

        Node* const fulfill_reactions =
            AllocateFixedArray(FAST_ELEMENTS, IntPtrConstant(2));
        StoreFixedArrayElement(
            fulfill_reactions, 0,
            LoadObjectField(promise, JSPromise::kFulfillReactionsOffset));
        StoreFixedArrayElement(fulfill_reactions, 1, var_on_resolve.value());

        Node* const reject_reactions =
            AllocateFixedArray(FAST_ELEMENTS, IntPtrConstant(2));
        StoreFixedArrayElement(
            reject_reactions, 0,
            LoadObjectField(promise, JSPromise::kRejectReactionsOffset));
        StoreFixedArrayElement(reject_reactions, 1, var_on_reject.value());

        // Store new FixedArrays in promise.
        StoreObjectField(promise, JSPromise::kDeferredOffset, deferreds);
        StoreObjectField(promise, JSPromise::kFulfillReactionsOffset,
                         fulfill_reactions);
        StoreObjectField(promise, JSPromise::kRejectReactionsOffset,
                         reject_reactions);
        Goto(&out);
      }

      Bind(&if_multiplecallbacks);
      {
        AppendPromiseCallback(JSPromise::kDeferredOffset, promise, deferred);
        AppendPromiseCallback(JSPromise::kFulfillReactionsOffset, promise,
                              var_on_resolve.value());
        AppendPromiseCallback(JSPromise::kRejectReactionsOffset, promise,
                              var_on_reject.value());
        Goto(&out);
      }
    }

    Bind(&fulfilled_check);
    {
      Label reject(this);
      Node* const result = LoadObjectField(promise, JSPromise::kResultOffset);
      GotoUnless(WordEqual(status, SmiConstant(kPromiseFulfilled)), &reject);

      // TODO(gsathya): Move this to TF.
      CallRuntime(Runtime::kEnqueuePromiseReactionJob, context, promise, result,
                  var_on_resolve.value(), deferred,
                  SmiConstant(kPromiseFulfilled));
      Goto(&out);

      Bind(&reject);
      {
        Node* const has_handler = PromiseHasHandler(promise);
        Label enqueue(this);

        // TODO(gsathya): Fold these runtime calls and move to TF.
        GotoIf(has_handler, &enqueue);
        CallRuntime(Runtime::kPromiseRevokeReject, context, promise);
        Goto(&enqueue);

        Bind(&enqueue);
        {
          CallRuntime(Runtime::kEnqueuePromiseReactionJob, context, promise,
                      result, var_on_reject.value(), deferred,
                      SmiConstant(kPromiseRejected));

          Goto(&out);
        }
      }
    }
  }

  Bind(&out);
  PromiseSetHasHandler(promise);

  // TODO(gsathya): This call will be removed once we don't have to
  // deal with deferred objects.
  Isolate* isolate = this->isolate();
  Callable getproperty_callable = CodeFactory::GetProperty(isolate);
  Node* const key =
      HeapConstant(isolate->factory()->NewStringFromAsciiChecked("promise"));
  Node* const result = CallStub(getproperty_callable, context, deferred, key);

  return result;
}

// Promise fast path implementations rely on unmodified JSPromise instances.
// We use a fairly coarse granularity for this and simply check whether both
// the promise itself is unmodified (i.e. its map has not changed) and its
// prototype is unmodified.
// TODO(gsathya): Refactor this out to prevent code dupe with builtins-regexp
void PromiseBuiltinsAssembler::BranchIfFastPath(Node* context, Node* promise,
                                                Label* if_isunmodified,
                                                Label* if_ismodified) {
  // TODO(gsathya): Assert if promise is receiver
  Node* const map = LoadMap(promise);
  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const initial_map =
      LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const has_initialmap = WordEqual(map, initial_map);

  GotoUnless(has_initialmap, if_ismodified);

  Node* const initial_proto_initial_map =
      LoadContextElement(native_context, Context::PROMISE_PROTOTYPE_MAP_INDEX);
  Node* const proto_map = LoadMap(LoadMapPrototype(map));
  Node* const proto_has_initialmap =
      WordEqual(proto_map, initial_proto_initial_map);

  Branch(proto_has_initialmap, if_isunmodified, if_ismodified);
}

void PromiseBuiltinsAssembler::InternalResolvePromise(Node* context,
                                                      Node* promise,
                                                      Node* result,
                                                      Label* out) {
  Isolate* isolate = this->isolate();

  Variable var_reason(this, MachineRepresentation::kTagged),
      var_then(this, MachineRepresentation::kTagged);

  Label do_enqueue(this), fulfill(this), if_cycle(this, Label::kDeferred),
      if_rejectpromise(this, Label::kDeferred);

  Label cycle_check(this);
  GotoUnless(IsPromiseHookEnabled(), &cycle_check);
  CallRuntime(Runtime::kPromiseHookResolve, context, promise);
  Goto(&cycle_check);

  Bind(&cycle_check);
  // 6. If SameValue(resolution, promise) is true, then
  GotoIf(SameValue(promise, result, context), &if_cycle);

  // 7. If Type(resolution) is not Object, then
  GotoIf(TaggedIsSmi(result), &fulfill);
  GotoUnless(IsJSReceiver(result), &fulfill);

  Label if_nativepromise(this), if_notnativepromise(this, Label::kDeferred);
  BranchIfFastPath(context, result, &if_nativepromise, &if_notnativepromise);

  // Resolution is a native promise and if it's already resolved or
  // rejected, shortcircuit the resolution procedure by directly
  // reusing the value from the promise.
  Bind(&if_nativepromise);
  {
    Node* const thenable_status =
        LoadObjectField(result, JSPromise::kStatusOffset);
    Node* const thenable_value =
        LoadObjectField(result, JSPromise::kResultOffset);

    Label if_isnotpending(this);
    GotoUnless(SmiEqual(SmiConstant(kPromisePending), thenable_status),
               &if_isnotpending);

    // TODO(gsathya): Use a marker here instead of the actual then
    // callback, and check for the marker in PromiseResolveThenableJob
    // and perform PromiseThen.
    Node* const native_context = LoadNativeContext(context);
    Node* const then =
        LoadContextElement(native_context, Context::PROMISE_THEN_INDEX);
    var_then.Bind(then);
    Goto(&do_enqueue);

    Bind(&if_isnotpending);
    {
      Label if_fulfilled(this), if_rejected(this);
      Branch(SmiEqual(SmiConstant(kPromiseFulfilled), thenable_status),
             &if_fulfilled, &if_rejected);

      Bind(&if_fulfilled);
      {
        CallRuntime(Runtime::kPromiseFulfill, context, promise,
                    SmiConstant(kPromiseFulfilled), thenable_value);
        PromiseSetHasHandler(promise);
        Goto(out);
      }

      Bind(&if_rejected);
      {
        Label reject(this);
        Node* const has_handler = PromiseHasHandler(result);

        // Promise has already been rejected, but had no handler.
        // Revoke previously triggered reject event.
        GotoIf(has_handler, &reject);
        CallRuntime(Runtime::kPromiseRevokeReject, context, result);
        Goto(&reject);

        Bind(&reject);
        // Don't cause a debug event as this case is forwarding a rejection
        CallRuntime(Runtime::kPromiseReject, context, promise, thenable_value,
                    FalseConstant());
        PromiseSetHasHandler(result);
        Goto(out);
      }
    }
  }

  Bind(&if_notnativepromise);
  {
    // 8. Let then be Get(resolution, "then").
    Node* const then_str = HeapConstant(isolate->factory()->then_string());
    Callable getproperty_callable = CodeFactory::GetProperty(isolate);
    Node* const then =
        CallStub(getproperty_callable, context, result, then_str);

    // 9. If then is an abrupt completion, then
    GotoIfException(then, &if_rejectpromise, &var_reason);

    // 11. If IsCallable(thenAction) is false, then
    GotoIf(TaggedIsSmi(then), &fulfill);
    Node* const then_map = LoadMap(then);
    GotoUnless(IsCallableMap(then_map), &fulfill);
    var_then.Bind(then);
    Goto(&do_enqueue);
  }

  Bind(&do_enqueue);
  {
    Label enqueue(this);
    GotoUnless(IsDebugActive(), &enqueue);
    GotoIf(TaggedIsSmi(result), &enqueue);
    GotoUnless(HasInstanceType(result, JS_PROMISE_TYPE), &enqueue);
    // Mark the dependency of the new promise on the resolution
    Node* const key =
        HeapConstant(isolate->factory()->promise_handled_by_symbol());
    CallRuntime(Runtime::kSetProperty, context, result, key, promise,
                SmiConstant(STRICT));
    Goto(&enqueue);

    // 12. Perform EnqueueJob("PromiseJobs",
    // PromiseResolveThenableJob, « promise, resolution, thenAction
    // »).
    Bind(&enqueue);
    CallRuntime(Runtime::kEnqueuePromiseResolveThenableJob, context, promise,
                result, var_then.value());
    Goto(out);
  }

  // 7.b Return FulfillPromise(promise, resolution).
  Bind(&fulfill);
  {
    CallRuntime(Runtime::kPromiseFulfill, context, promise,
                SmiConstant(kPromiseFulfilled), result);
    Goto(out);
  }

  Bind(&if_cycle);
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
  Bind(&if_rejectpromise);
  {
    CallRuntime(Runtime::kPromiseReject, context, promise, var_reason.value(),
                TrueConstant());
    Goto(out);
  }
}

// ES#sec-promise-reject-functions
// Promise Reject Functions
BUILTIN(PromiseRejectClosure) {
  HandleScope scope(isolate);

  Handle<Context> context(isolate->context(), isolate);

  if (PromiseUtils::HasAlreadyVisited(context)) {
    return isolate->heap()->undefined_value();
  }

  PromiseUtils::SetAlreadyVisited(context);
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<JSObject> promise = handle(PromiseUtils::GetPromise(context), isolate);
  Handle<Object> debug_event =
      handle(PromiseUtils::GetDebugEvent(context), isolate);
  MaybeHandle<Object> maybe_result;
  Handle<Object> argv[] = {promise, value, debug_event};
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, Execution::Call(isolate, isolate->promise_internal_reject(),
                               isolate->factory()->undefined_value(),
                               arraysize(argv), argv));
  return isolate->heap()->undefined_value();
}

// ES#sec-createresolvingfunctions
// CreateResolvingFunctions ( promise )
BUILTIN(CreateResolvingFunctions) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());

  Handle<JSObject> promise = args.at<JSObject>(1);
  Handle<Object> debug_event = args.at<Object>(2);
  Handle<JSFunction> resolve, reject;

  PromiseUtils::CreateResolvingFunctions(isolate, promise, debug_event,
                                         &resolve, &reject);

  Handle<FixedArray> result = isolate->factory()->NewFixedArray(2);
  result->set(0, *resolve);
  result->set(1, *reject);

  return *isolate->factory()->NewJSArrayWithElements(result, FAST_ELEMENTS, 2,
                                                     NOT_TENURED);
}

TF_BUILTIN(PromiseConstructor, PromiseBuiltinsAssembler) {
  Node* const executor = Parameter(1);
  Node* const new_target = Parameter(2);
  Node* const context = Parameter(4);
  Isolate* isolate = this->isolate();

  Label if_targetisundefined(this, Label::kDeferred);

  GotoIf(IsUndefined(new_target), &if_targetisundefined);

  Label if_notcallable(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(executor), &if_notcallable);

  Node* const executor_map = LoadMap(executor);
  GotoUnless(IsCallableMap(executor_map), &if_notcallable);

  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const is_debug_active = IsDebugActive();
  Label if_targetisnotmodified(this),
      if_targetismodified(this, Label::kDeferred), run_executor(this),
      debug_push(this, Label::kDeferred), init(this);

  Branch(WordEqual(promise_fun, new_target), &if_targetisnotmodified,
         &if_targetismodified);

  Variable var_result(this, MachineRepresentation::kTagged),
      var_reject_call(this, MachineRepresentation::kTagged),
      var_reason(this, MachineRepresentation::kTagged);

  Bind(&if_targetisnotmodified);
  {
    Node* const instance = AllocateJSPromise(context);
    var_result.Bind(instance);
    GotoUnless(IsPromiseHookEnabled(), &init);
    CallRuntime(Runtime::kPromiseHookInit, context, instance,
                UndefinedConstant());
    Goto(&init);
  }

  Bind(&if_targetismodified);
  {
    Callable fast_new_object_stub = CodeFactory::FastNewObject(isolate);
    Node* const instance =
        CallStub(fast_new_object_stub, context, promise_fun, new_target);

    var_result.Bind(instance);
    Goto(&init);
  }

  Bind(&init);
  {
    PromiseInit(var_result.value());
    Branch(is_debug_active, &debug_push, &run_executor);
  }

  Bind(&debug_push);
  {
    CallRuntime(Runtime::kDebugPushPromise, context, var_result.value());
    Goto(&run_executor);
  }

  Bind(&run_executor);
  {
    Label out(this), if_rejectpromise(this), debug_pop(this, Label::kDeferred);

    // TODO(gsathya): Move this to TF.
    Node* const resolving_functions = CallRuntime(
        Runtime::kCreateResolvingFunctions, context, var_result.value());
    Node* const resolve =
        LoadFixedArrayElement(resolving_functions, IntPtrConstant(0));
    Node* const reject =
        LoadFixedArrayElement(resolving_functions, IntPtrConstant(1));
    Callable call_callable = CodeFactory::Call(isolate);

    Node* const maybe_exception = CallJS(call_callable, context, executor,
                                         UndefinedConstant(), resolve, reject);

    GotoIfException(maybe_exception, &if_rejectpromise, &var_reason);
    Branch(is_debug_active, &debug_pop, &out);

    Bind(&if_rejectpromise);
    {
      Callable call_callable = CodeFactory::Call(isolate);
      CallJS(call_callable, context, reject, UndefinedConstant(),
             var_reason.value());
      Branch(is_debug_active, &debug_pop, &out);
    }

    Bind(&debug_pop);
    {
      CallRuntime(Runtime::kDebugPopPromise, context);
      Goto(&out);
    }
    Bind(&out);
    Return(var_result.value());
  }

  // 1. If NewTarget is undefined, throw a TypeError exception.
  Bind(&if_targetisundefined);
  {
    Node* const message_id = SmiConstant(MessageTemplate::kNotAPromise);
    CallRuntime(Runtime::kThrowTypeError, context, message_id, new_target);
    Return(UndefinedConstant());  // Never reached.
  }

  // 2. If IsCallable(executor) is false, throw a TypeError exception.
  Bind(&if_notcallable);
  {
    Node* const message_id =
        SmiConstant(MessageTemplate::kResolverNotAFunction);
    CallRuntime(Runtime::kThrowTypeError, context, message_id, executor);
    Return(UndefinedConstant());  // Never reached.
  }
}

TF_BUILTIN(PromiseInternalConstructor, PromiseBuiltinsAssembler) {
  Node* const parent = Parameter(1);
  Node* const context = Parameter(4);
  Node* const instance = AllocateJSPromise(context);
  PromiseInit(instance);

  Label out(this);
  GotoUnless(IsPromiseHookEnabled(), &out);
  CallRuntime(Runtime::kPromiseHookInit, context, instance, parent);
  Goto(&out);
  Bind(&out);

  Return(instance);
}

TF_BUILTIN(PromiseCreateAndSet, PromiseBuiltinsAssembler) {
  Node* const status = Parameter(1);
  Node* const result = Parameter(2);
  Node* const context = Parameter(5);

  Node* const instance = AllocateJSPromise(context);
  PromiseSet(instance, status, result);

  Label out(this);
  GotoUnless(IsPromiseHookEnabled(), &out);
  CallRuntime(Runtime::kPromiseHookInit, context, instance,
              UndefinedConstant());
  Goto(&out);
  Bind(&out);
  Return(instance);
}

TF_BUILTIN(IsPromise, PromiseBuiltinsAssembler) {
  Node* const maybe_promise = Parameter(1);
  Label if_notpromise(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(maybe_promise), &if_notpromise);

  Node* const result =
      SelectBooleanConstant(HasInstanceType(maybe_promise, JS_PROMISE_TYPE));
  Return(result);

  Bind(&if_notpromise);
  Return(FalseConstant());
}

TF_BUILTIN(PerformPromiseThen, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(1);
  Node* const on_resolve = Parameter(2);
  Node* const on_reject = Parameter(3);
  Node* const deferred = Parameter(4);
  Node* const context = Parameter(7);

  Node* const result = InternalPerformPromiseThen(context, promise, on_resolve,
                                                  on_reject, deferred);

  // TODO(gsathya): This is unused, but value is returned according to spec.
  Return(result);
}

TF_BUILTIN(PromiseThen, PromiseBuiltinsAssembler) {
  // 1. Let promise be the this value.
  Node* const promise = Parameter(0);
  Node* const on_resolve = Parameter(1);
  Node* const on_reject = Parameter(2);
  Node* const context = Parameter(5);
  Isolate* isolate = this->isolate();

  // 2. If IsPromise(promise) is false, throw a TypeError exception.
  ThrowIfNotInstanceType(context, promise, JS_PROMISE_TYPE,
                         "Promise.prototype.then");

  Node* const native_context = LoadNativeContext(context);
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);

  // 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  Node* constructor = SpeciesConstructor(context, promise, promise_fun);

  // 4. Let resultCapability be ? NewPromiseCapability(C).
  Callable call_callable = CodeFactory::Call(isolate);
  Label fast_promise_capability(this), promise_capability(this),
      perform_promise_then(this);
  Variable var_deferred(this, MachineRepresentation::kTagged);

  Branch(WordEqual(promise_fun, constructor), &fast_promise_capability,
         &promise_capability);

  // TODO(gsathya): Remove deferred object and move
  // NewPromiseCapabability functions to TF.
  Bind(&fast_promise_capability);
  {
    // TODO(gsathya): Move this to TF.
    Node* const promise_internal_capability = LoadContextElement(
        native_context, Context::INTERNAL_PROMISE_CAPABILITY_INDEX);
    Node* const capability =
        CallJS(call_callable, context, promise_internal_capability,
               UndefinedConstant(), promise);
    var_deferred.Bind(capability);
    Goto(&perform_promise_then);
  }

  Bind(&promise_capability);
  {
    // TODO(gsathya): Move this to TF.
    Node* const new_promise_capability = LoadContextElement(
        native_context, Context::NEW_PROMISE_CAPABILITY_INDEX);
    Node* const capability =
        CallJS(call_callable, context, new_promise_capability,
               UndefinedConstant(), constructor);
    var_deferred.Bind(capability);
    Goto(&perform_promise_then);
  }

  // 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //    resultCapability).
  Bind(&perform_promise_then);
  Node* const result = InternalPerformPromiseThen(
      context, promise, on_resolve, on_reject, var_deferred.value());
  Return(result);
}

// ES#sec-promise-resolve-functions
// Promise Resolve Functions
TF_BUILTIN(PromiseResolveClosure, PromiseBuiltinsAssembler) {
  Node* const value = Parameter(1);
  Node* const context = Parameter(4);

  Label out(this);

  // 3. Let alreadyResolved be F.[[AlreadyResolved]].
  Node* const has_already_visited_slot =
      IntPtrConstant(PromiseUtils::kAlreadyVisitedSlot);

  Node* const has_already_visited =
      LoadFixedArrayElement(context, has_already_visited_slot);

  // 4. If alreadyResolved.[[Value]] is true, return undefined.
  GotoIf(SmiEqual(has_already_visited, SmiConstant(1)), &out);

  // 5.Set alreadyResolved.[[Value]] to true.
  StoreFixedArrayElement(context, has_already_visited_slot, SmiConstant(1));

  // 2. Let promise be F.[[Promise]].
  Node* const promise = LoadFixedArrayElement(
      context, IntPtrConstant(PromiseUtils::kPromiseSlot));

  InternalResolvePromise(context, promise, value, &out);

  Bind(&out);
  Return(UndefinedConstant());
}

TF_BUILTIN(ResolvePromise, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(1);
  Node* const result = Parameter(2);
  Node* const context = Parameter(5);

  Label out(this);
  InternalResolvePromise(context, promise, result, &out);

  Bind(&out);
  Return(UndefinedConstant());
}

TF_BUILTIN(PromiseHandleReject, PromiseBuiltinsAssembler) {
  typedef PromiseHandleRejectDescriptor Descriptor;

  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const on_reject = Parameter(Descriptor::kOnReject);
  Node* const exception = Parameter(Descriptor::kException);
  Node* const context = Parameter(Descriptor::kContext);

  Callable call_callable = CodeFactory::Call(isolate());
  Variable var_unused(this, MachineRepresentation::kTagged);

  Label if_internalhandler(this), if_customhandler(this, Label::kDeferred);
  Branch(IsUndefined(on_reject), &if_internalhandler, &if_customhandler);

  Bind(&if_internalhandler);
  {
    CallRuntime(Runtime::kPromiseReject, context, promise, exception,
                FalseConstant());
    Return(UndefinedConstant());
  }

  Bind(&if_customhandler);
  {
    CallJS(call_callable, context, on_reject, UndefinedConstant(), exception);
    Return(UndefinedConstant());
  }
}

TF_BUILTIN(PromiseHandle, PromiseBuiltinsAssembler) {
  Node* const promise = Parameter(1);
  Node* const value = Parameter(2);
  Node* const handler = Parameter(3);
  Node* const deferred = Parameter(4);
  Node* const context = Parameter(7);
  Isolate* isolate = this->isolate();

  // Get promise from deferred
  // TODO(gsathya): Remove this lookup by getting rid of the deferred object.
  Callable getproperty_callable = CodeFactory::GetProperty(isolate);
  Node* const key = HeapConstant(isolate->factory()->promise_string());
  Node* const deferred_promise =
      CallStub(getproperty_callable, context, deferred, key);

  Variable var_reason(this, MachineRepresentation::kTagged);

  Node* const is_debug_active = IsDebugActive();
  Label run_handler(this), if_rejectpromise(this), promisehook_before(this),
      promisehook_after(this), debug_pop(this);

  GotoUnless(is_debug_active, &promisehook_before);
  CallRuntime(Runtime::kDebugPushPromise, context, deferred_promise);
  Goto(&promisehook_before);

  Bind(&promisehook_before);
  {
    GotoUnless(IsPromiseHookEnabled(), &run_handler);
    CallRuntime(Runtime::kPromiseHookBefore, context, promise);
    Goto(&run_handler);
  }

  Bind(&run_handler);
  {
    Callable call_callable = CodeFactory::Call(isolate);

    Node* const result =
        CallJS(call_callable, context, handler, UndefinedConstant(), value);

    GotoIfException(result, &if_rejectpromise, &var_reason);

    // TODO(gsathya): Remove this lookup by getting rid of the deferred object.
    Node* const key = HeapConstant(isolate->factory()->resolve_string());
    Node* const on_resolve =
        CallStub(getproperty_callable, context, deferred, key);

    Label if_internalhandler(this), if_customhandler(this, Label::kDeferred);
    Branch(IsUndefined(on_resolve), &if_internalhandler, &if_customhandler);

    Bind(&if_internalhandler);
    InternalResolvePromise(context, deferred_promise, result,
                           &promisehook_after);

    Bind(&if_customhandler);
    {
      Node* const maybe_exception = CallJS(call_callable, context, on_resolve,
                                           UndefinedConstant(), result);
      GotoIfException(maybe_exception, &if_rejectpromise, &var_reason);
      Goto(&promisehook_after);
    }
  }

  Bind(&if_rejectpromise);
  {
    // TODO(gsathya): Remove this lookup by getting rid of the deferred object.
    Node* const key = HeapConstant(isolate->factory()->reject_string());
    Node* const on_reject =
        CallStub(getproperty_callable, context, deferred, key);

    Callable promise_handle_reject = CodeFactory::PromiseHandleReject(isolate);
    CallStub(promise_handle_reject, context, deferred_promise, on_reject,
             var_reason.value());
    Goto(&promisehook_after);
  }

  Bind(&promisehook_after);
  {
    GotoUnless(IsPromiseHookEnabled(), &debug_pop);
    CallRuntime(Runtime::kPromiseHookAfter, context, promise);
    Goto(&debug_pop);
  }

  Bind(&debug_pop);
  {
    Label out(this);

    GotoUnless(is_debug_active, &out);
    CallRuntime(Runtime::kDebugPopPromise, context);
    Goto(&out);

    Bind(&out);
    Return(UndefinedConstant());
  }
}

}  // namespace internal
}  // namespace v8
