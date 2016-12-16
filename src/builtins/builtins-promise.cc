// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/promise-utils.h"

namespace v8 {
namespace internal {

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
  Handle<Object> debug_event = args.at(2);
  Handle<JSFunction> resolve, reject;

  PromiseUtils::CreateResolvingFunctions(isolate, promise, debug_event,
                                         &resolve, &reject);

  Handle<FixedArray> result = isolate->factory()->NewFixedArray(2);
  result->set(0, *resolve);
  result->set(1, *reject);

  return *isolate->factory()->NewJSArrayWithElements(result, FAST_ELEMENTS, 2,
                                                     NOT_TENURED);
}

void Builtins::Generate_PromiseConstructor(
    compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* const executor = a.Parameter(1);
  Node* const new_target = a.Parameter(2);
  Node* const context = a.Parameter(4);
  Isolate* isolate = a.isolate();

  Label if_targetisundefined(&a, Label::kDeferred);

  a.GotoIf(a.IsUndefined(new_target), &if_targetisundefined);

  Label if_notcallable(&a, Label::kDeferred);

  a.GotoIf(a.TaggedIsSmi(executor), &if_notcallable);

  Node* const executor_map = a.LoadMap(executor);
  a.GotoUnless(a.IsCallableMap(executor_map), &if_notcallable);

  Node* const native_context = a.LoadNativeContext(context);
  Node* const promise_fun =
      a.LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const is_debug_active = a.IsDebugActive();
  Label if_targetisnotmodified(&a), if_targetismodified(&a, Label::kDeferred),
      run_executor(&a), debug_push(&a, Label::kDeferred), init(&a);

  a.Branch(a.WordEqual(promise_fun, new_target), &if_targetisnotmodified,
           &if_targetismodified);

  Variable var_result(&a, MachineRepresentation::kTagged),
      var_reject_call(&a, MachineRepresentation::kTagged),
      var_reason(&a, MachineRepresentation::kTagged);

  a.Bind(&if_targetisnotmodified);
  {
    Node* const instance = a.AllocateJSPromise(context);
    var_result.Bind(instance);
    a.Goto(&init);
  }

  a.Bind(&if_targetismodified);
  {
    Callable fast_new_object_stub = CodeFactory::FastNewObject(isolate);
    Node* const instance =
        a.CallStub(fast_new_object_stub, context, promise_fun, new_target);

    var_result.Bind(instance);
    a.Goto(&init);
  }

  a.Bind(&init);
  {
    a.PromiseInit(var_result.value());
    a.Branch(is_debug_active, &debug_push, &run_executor);
  }

  a.Bind(&debug_push);
  {
    a.CallRuntime(Runtime::kDebugPushPromise, context, var_result.value());
    a.Goto(&run_executor);
  }

  a.Bind(&run_executor);
  {
    Label out(&a), if_rejectpromise(&a), debug_pop(&a, Label::kDeferred);

    // TODO(gsathya): Move this to TF.
    Node* const resolving_functions = a.CallRuntime(
        Runtime::kCreateResolvingFunctions, context, var_result.value());
    Node* const resolve =
        a.LoadFixedArrayElement(resolving_functions, a.IntPtrConstant(0));
    Node* const reject =
        a.LoadFixedArrayElement(resolving_functions, a.IntPtrConstant(1));
    Callable call_callable = CodeFactory::Call(isolate);

    Node* const maybe_exception =
        a.CallJS(call_callable, context, executor, a.UndefinedConstant(),
                 resolve, reject);

    a.GotoIfException(maybe_exception, &if_rejectpromise, &var_reason);
    a.Branch(is_debug_active, &debug_pop, &out);

    a.Bind(&if_rejectpromise);
    {
      Callable call_callable = CodeFactory::Call(isolate);
      a.CallJS(call_callable, context, reject, a.UndefinedConstant(),
               var_reason.value());
      a.Branch(is_debug_active, &debug_pop, &out);
    }

    a.Bind(&debug_pop);
    {
      a.CallRuntime(Runtime::kDebugPopPromise, context);
      a.Goto(&out);
    }
    a.Bind(&out);
    a.Return(var_result.value());
  }

  // 1. If NewTarget is undefined, throw a TypeError exception.
  a.Bind(&if_targetisundefined);
  {
    Node* const message_id = a.SmiConstant(MessageTemplate::kNotAPromise);
    a.CallRuntime(Runtime::kThrowTypeError, context, message_id, new_target);
    a.Return(a.UndefinedConstant());  // Never reached.
  }

  // 2. If IsCallable(executor) is false, throw a TypeError exception.
  a.Bind(&if_notcallable);
  {
    Node* const message_id =
        a.SmiConstant(MessageTemplate::kResolverNotAFunction);
    a.CallRuntime(Runtime::kThrowTypeError, context, message_id, executor);
    a.Return(a.UndefinedConstant());  // Never reached.
  }
}

void Builtins::Generate_PromiseInternalConstructor(
    compiler::CodeAssemblerState* state) {
  typedef compiler::Node Node;
  CodeStubAssembler a(state);

  Node* const context = a.Parameter(3);
  Node* const instance = a.AllocateJSPromise(context);
  a.PromiseInit(instance);
  a.Return(instance);
}

void Builtins::Generate_PromiseCreateAndSet(
    compiler::CodeAssemblerState* state) {
  typedef compiler::Node Node;
  CodeStubAssembler a(state);

  Node* const status = a.Parameter(1);
  Node* const result = a.Parameter(2);
  Node* const context = a.Parameter(5);

  Node* const instance = a.AllocateJSPromise(context);
  a.PromiseSet(instance, status, result);
  a.Return(instance);
}

namespace {

compiler::Node* ThrowIfNotJSReceiver(CodeStubAssembler* a, Isolate* isolate,
                                     compiler::Node* context,
                                     compiler::Node* value,
                                     MessageTemplate::Template msg_template) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Label out(a), throw_exception(a, Label::kDeferred);
  Variable var_value_map(a, MachineRepresentation::kTagged);

  a->GotoIf(a->TaggedIsSmi(value), &throw_exception);

  // Load the instance type of the {value}.
  var_value_map.Bind(a->LoadMap(value));
  Node* const value_instance_type =
      a->LoadMapInstanceType(var_value_map.value());

  a->Branch(a->IsJSReceiverInstanceType(value_instance_type), &out,
            &throw_exception);

  // The {value} is not a compatible receiver for this method.
  a->Bind(&throw_exception);
  {
    Node* const message_id = a->SmiConstant(msg_template);
    a->CallRuntime(Runtime::kThrowTypeError, context, message_id);
    var_value_map.Bind(a->UndefinedConstant());
    a->Goto(&out);  // Never reached.
  }

  a->Bind(&out);
  return var_value_map.value();
}

}  // namespace

void Builtins::Generate_IsPromise(compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Node* const maybe_promise = a.Parameter(1);
  Label if_notpromise(&a, Label::kDeferred);

  a.GotoIf(a.TaggedIsSmi(maybe_promise), &if_notpromise);

  Node* const result = a.SelectBooleanConstant(
      a.HasInstanceType(maybe_promise, JS_PROMISE_TYPE));
  a.Return(result);

  a.Bind(&if_notpromise);
  a.Return(a.FalseConstant());
}

namespace {

compiler::Node* PromiseHasHandler(CodeStubAssembler* a,
                                  compiler::Node* promise) {
  typedef compiler::Node Node;

  Node* const flags = a->LoadObjectField(promise, JSPromise::kFlagsOffset);
  return a->IsSetWord(a->SmiUntag(flags), 1 << JSPromise::kHasHandlerBit);
}

void PromiseSetHasHandler(CodeStubAssembler* a, compiler::Node* promise) {
  typedef compiler::Node Node;

  Node* const flags =
      a->SmiUntag(a->LoadObjectField(promise, JSPromise::kFlagsOffset));
  Node* const new_flags =
      a->WordOr(flags, a->IntPtrConstant(1 << JSPromise::kHasHandlerBit));
  a->StoreObjectField(promise, JSPromise::kFlagsOffset, a->SmiTag(new_flags));
}

compiler::Node* SpeciesConstructor(CodeStubAssembler* a, Isolate* isolate,
                                   compiler::Node* context,
                                   compiler::Node* object,
                                   compiler::Node* default_constructor) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Variable var_result(a, MachineRepresentation::kTagged);
  var_result.Bind(default_constructor);

  // 2. Let C be ? Get(O, "constructor").
  Node* const constructor_str =
      a->HeapConstant(isolate->factory()->constructor_string());
  Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
  Node* const constructor =
      a->CallStub(getproperty_callable, context, object, constructor_str);

  // 3. If C is undefined, return defaultConstructor.
  Label out(a);
  a->GotoIf(a->IsUndefined(constructor), &out);

  // 4. If Type(C) is not Object, throw a TypeError exception.
  ThrowIfNotJSReceiver(a, a->isolate(), context, constructor,
                       MessageTemplate::kConstructorNotReceiver);

  // 5. Let S be ? Get(C, @@species).
  Node* const species_symbol =
      a->HeapConstant(isolate->factory()->species_symbol());
  Node* const species =
      a->CallStub(getproperty_callable, context, constructor, species_symbol);

  // 6. If S is either undefined or null, return defaultConstructor.
  a->GotoIf(a->IsUndefined(species), &out);
  a->GotoIf(a->WordEqual(species, a->NullConstant()), &out);

  // 7. If IsConstructor(S) is true, return S.
  Label throw_error(a);
  Node* species_bitfield = a->LoadMapBitField(a->LoadMap(species));
  a->GotoUnless(
      a->Word32Equal(a->Word32And(species_bitfield,
                                  a->Int32Constant((1 << Map::kIsConstructor))),
                     a->Int32Constant(1 << Map::kIsConstructor)),
      &throw_error);
  var_result.Bind(species);
  a->Goto(&out);

  // 8. Throw a TypeError exception.
  a->Bind(&throw_error);
  {
    Node* const message_id =
        a->SmiConstant(MessageTemplate::kSpeciesNotConstructor);
    a->CallRuntime(Runtime::kThrowTypeError, context, message_id);
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

void AppendPromiseCallback(CodeStubAssembler* a, int offset,
                           compiler::Node* promise, compiler::Node* value) {
  typedef compiler::Node Node;

  Node* elements = a->LoadObjectField(promise, offset);
  Node* length = a->LoadFixedArrayBaseLength(elements);
  CodeStubAssembler::ParameterMode mode = a->OptimalParameterMode();
  length = a->TaggedToParameter(length, mode);

  Node* delta = a->IntPtrOrSmiConstant(1, mode);
  Node* new_capacity = a->IntPtrOrSmiAdd(length, delta, mode);

  const ElementsKind kind = FAST_ELEMENTS;
  const WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER;
  const CodeStubAssembler::AllocationFlags flags =
      CodeStubAssembler::kAllowLargeObjectAllocation;
  int additional_offset = 0;

  Node* new_elements = a->AllocateFixedArray(kind, new_capacity, mode, flags);

  a->CopyFixedArrayElements(kind, elements, new_elements, length, barrier_mode,
                            mode);
  a->StoreFixedArrayElement(new_elements, length, value, barrier_mode,
                            additional_offset, mode);

  a->StoreObjectField(promise, offset, new_elements);
}

compiler::Node* InternalPerformPromiseThen(CodeStubAssembler* a,
                                           compiler::Node* context,
                                           compiler::Node* promise,
                                           compiler::Node* on_resolve,
                                           compiler::Node* on_reject,
                                           compiler::Node* deferred) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  Isolate* isolate = a->isolate();
  Node* const native_context = a->LoadNativeContext(context);

  Variable var_on_resolve(a, MachineRepresentation::kTagged),
      var_on_reject(a, MachineRepresentation::kTagged);

  var_on_resolve.Bind(on_resolve);
  var_on_reject.Bind(on_reject);

  Label out(a), if_onresolvenotcallable(a), onrejectcheck(a),
      append_callbacks(a);
  a->GotoIf(a->TaggedIsSmi(on_resolve), &if_onresolvenotcallable);

  Node* const on_resolve_map = a->LoadMap(on_resolve);
  a->Branch(a->IsCallableMap(on_resolve_map), &onrejectcheck,
            &if_onresolvenotcallable);

  a->Bind(&if_onresolvenotcallable);
  {
    var_on_resolve.Bind(a->LoadContextElement(
        native_context, Context::PROMISE_ID_RESOLVE_HANDLER_INDEX));
    a->Goto(&onrejectcheck);
  }

  a->Bind(&onrejectcheck);
  {
    Label if_onrejectnotcallable(a);
    a->GotoIf(a->TaggedIsSmi(on_reject), &if_onrejectnotcallable);

    Node* const on_reject_map = a->LoadMap(on_reject);
    a->Branch(a->IsCallableMap(on_reject_map), &append_callbacks,
              &if_onrejectnotcallable);

    a->Bind(&if_onrejectnotcallable);
    {
      var_on_reject.Bind(a->LoadContextElement(
          native_context, Context::PROMISE_ID_REJECT_HANDLER_INDEX));
      a->Goto(&append_callbacks);
    }
  }

  a->Bind(&append_callbacks);
  {
    Label fulfilled_check(a);
    Node* const status = a->LoadObjectField(promise, JSPromise::kStatusOffset);
    a->GotoUnless(a->SmiEqual(status, a->SmiConstant(kPromisePending)),
                  &fulfilled_check);

    Node* const existing_deferred =
        a->LoadObjectField(promise, JSPromise::kDeferredOffset);

    Label if_noexistingcallbacks(a), if_existingcallbacks(a);
    a->Branch(a->IsUndefined(existing_deferred), &if_noexistingcallbacks,
              &if_existingcallbacks);

    a->Bind(&if_noexistingcallbacks);
    {
      // Store callbacks directly in the slots.
      a->StoreObjectField(promise, JSPromise::kDeferredOffset, deferred);
      a->StoreObjectField(promise, JSPromise::kFulfillReactionsOffset,
                          var_on_resolve.value());
      a->StoreObjectField(promise, JSPromise::kRejectReactionsOffset,
                          var_on_reject.value());
      a->Goto(&out);
    }

    a->Bind(&if_existingcallbacks);
    {
      Label if_singlecallback(a), if_multiplecallbacks(a);
      a->BranchIfJSObject(existing_deferred, &if_singlecallback,
                          &if_multiplecallbacks);

      a->Bind(&if_singlecallback);
      {
        // Create new FixedArrays to store callbacks, and migrate
        // existing callbacks.
        Node* const deferreds =
            a->AllocateFixedArray(FAST_ELEMENTS, a->IntPtrConstant(2));
        a->StoreFixedArrayElement(deferreds, 0, existing_deferred);
        a->StoreFixedArrayElement(deferreds, 1, deferred);

        Node* const fulfill_reactions =
            a->AllocateFixedArray(FAST_ELEMENTS, a->IntPtrConstant(2));
        a->StoreFixedArrayElement(
            fulfill_reactions, 0,
            a->LoadObjectField(promise, JSPromise::kFulfillReactionsOffset));
        a->StoreFixedArrayElement(fulfill_reactions, 1, var_on_resolve.value());

        Node* const reject_reactions =
            a->AllocateFixedArray(FAST_ELEMENTS, a->IntPtrConstant(2));
        a->StoreFixedArrayElement(
            reject_reactions, 0,
            a->LoadObjectField(promise, JSPromise::kRejectReactionsOffset));
        a->StoreFixedArrayElement(reject_reactions, 1, var_on_reject.value());

        // Store new FixedArrays in promise.
        a->StoreObjectField(promise, JSPromise::kDeferredOffset, deferreds);
        a->StoreObjectField(promise, JSPromise::kFulfillReactionsOffset,
                            fulfill_reactions);
        a->StoreObjectField(promise, JSPromise::kRejectReactionsOffset,
                            reject_reactions);
        a->Goto(&out);
      }

      a->Bind(&if_multiplecallbacks);
      {
        AppendPromiseCallback(a, JSPromise::kDeferredOffset, promise, deferred);
        AppendPromiseCallback(a, JSPromise::kFulfillReactionsOffset, promise,
                              var_on_resolve.value());
        AppendPromiseCallback(a, JSPromise::kRejectReactionsOffset, promise,
                              var_on_reject.value());
        a->Goto(&out);
      }
    }

    a->Bind(&fulfilled_check);
    {
      Label reject(a);
      Node* const result =
          a->LoadObjectField(promise, JSPromise::kResultOffset);
      a->GotoUnless(a->WordEqual(status, a->SmiConstant(kPromiseFulfilled)),
                    &reject);

      // TODO(gsathya): Move this to TF.
      a->CallRuntime(Runtime::kEnqueuePromiseReactionJob, context, promise,
                     result, var_on_resolve.value(), deferred,
                     a->SmiConstant(kPromiseFulfilled));
      a->Goto(&out);

      a->Bind(&reject);
      {
        Node* const has_handler = PromiseHasHandler(a, promise);
        Label enqueue(a);

        // TODO(gsathya): Fold these runtime calls and move to TF.
        a->GotoIf(has_handler, &enqueue);
        a->CallRuntime(Runtime::kPromiseRevokeReject, context, promise);
        a->Goto(&enqueue);

        a->Bind(&enqueue);
        {
          a->CallRuntime(Runtime::kEnqueuePromiseReactionJob, context, promise,
                         result, var_on_reject.value(), deferred,
                         a->SmiConstant(kPromiseRejected));

          a->Goto(&out);
        }
      }
    }
  }

  a->Bind(&out);
  PromiseSetHasHandler(a, promise);

  // TODO(gsathya): This call will be removed once we don't have to
  // deal with deferred objects.
  Callable getproperty_callable = CodeFactory::GetProperty(isolate);
  Node* const key =
      a->HeapConstant(isolate->factory()->NewStringFromAsciiChecked("promise"));
  Node* const result =
      a->CallStub(getproperty_callable, context, deferred, key);

  return result;
}

}  // namespace

void Builtins::Generate_PerformPromiseThen(
    compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;

  Node* const promise = a.Parameter(1);
  Node* const on_resolve = a.Parameter(2);
  Node* const on_reject = a.Parameter(3);
  Node* const deferred = a.Parameter(4);
  Node* const context = a.Parameter(7);

  Node* const result = InternalPerformPromiseThen(
      &a, context, promise, on_resolve, on_reject, deferred);

  // TODO(gsathya): This is unused, but value is returned according to spec.
  a.Return(result);
}

void Builtins::Generate_PromiseThen(compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  // 1. Let promise be the this value.
  Node* const promise = a.Parameter(0);
  Node* const on_resolve = a.Parameter(1);
  Node* const on_reject = a.Parameter(2);
  Node* const context = a.Parameter(5);
  Isolate* isolate = a.isolate();

  // 2. If IsPromise(promise) is false, throw a TypeError exception.
  a.ThrowIfNotInstanceType(context, promise, JS_PROMISE_TYPE,
                           "Promise.prototype.then");

  Node* const native_context = a.LoadNativeContext(context);
  Node* const promise_fun =
      a.LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);

  // 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  Node* constructor =
      SpeciesConstructor(&a, isolate, context, promise, promise_fun);

  // 4. Let resultCapability be ? NewPromiseCapability(C).
  Callable call_callable = CodeFactory::Call(isolate);
  Label fast_promise_capability(&a), promise_capability(&a),
      perform_promise_then(&a);
  Variable var_deferred(&a, MachineRepresentation::kTagged);

  a.Branch(a.WordEqual(promise_fun, constructor), &fast_promise_capability,
           &promise_capability);

  // TODO(gsathya): Remove deferred object and move
  // NewPromiseCapbability functions to TF.
  a.Bind(&fast_promise_capability);
  {
    // TODO(gsathya): Move this to TF.
    Node* const promise_internal_capability = a.LoadContextElement(
        native_context, Context::INTERNAL_PROMISE_CAPABILITY_INDEX);
    Node* const capability =
        a.CallJS(call_callable, context, promise_internal_capability,
                 a.UndefinedConstant());
    var_deferred.Bind(capability);
    a.Goto(&perform_promise_then);
  }

  a.Bind(&promise_capability);
  {
    // TODO(gsathya): Move this to TF.
    Node* const new_promise_capability = a.LoadContextElement(
        native_context, Context::NEW_PROMISE_CAPABILITY_INDEX);
    Node* const capability =
        a.CallJS(call_callable, context, new_promise_capability,
                 a.UndefinedConstant(), constructor);
    var_deferred.Bind(capability);
    a.Goto(&perform_promise_then);
  }

  // 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //    resultCapability).
  a.Bind(&perform_promise_then);
  Node* const result = InternalPerformPromiseThen(
      &a, context, promise, on_resolve, on_reject, var_deferred.value());
  a.Return(result);
}

namespace {

// Promise fast path implementations rely on unmodified JSPromise instances.
// We use a fairly coarse granularity for this and simply check whether both
// the promise itself is unmodified (i.e. its map has not changed) and its
// prototype is unmodified.
// TODO(gsathya): Refactor this out to prevent code dupe with builtins-regexp
void BranchIfFastPath(CodeStubAssembler* a, compiler::Node* context,
                      compiler::Node* promise,
                      CodeStubAssembler::Label* if_isunmodified,
                      CodeStubAssembler::Label* if_ismodified) {
  typedef compiler::Node Node;

  // TODO(gsathya): Assert if promise is receiver
  Node* const map = a->LoadMap(promise);
  Node* const native_context = a->LoadNativeContext(context);
  Node* const promise_fun =
      a->LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const initial_map =
      a->LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const has_initialmap = a->WordEqual(map, initial_map);

  a->GotoUnless(has_initialmap, if_ismodified);

  Node* const initial_proto_initial_map = a->LoadContextElement(
      native_context, Context::PROMISE_PROTOTYPE_MAP_INDEX);
  Node* const proto_map = a->LoadMap(a->LoadMapPrototype(map));
  Node* const proto_has_initialmap =
      a->WordEqual(proto_map, initial_proto_initial_map);

  a->Branch(proto_has_initialmap, if_isunmodified, if_ismodified);
}

void InternalResolvePromise(CodeStubAssembler* a, compiler::Node* context,
                            compiler::Node* promise, compiler::Node* result,
                            CodeStubAssembler::Label* out) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Isolate* isolate = a->isolate();

  Variable var_reason(a, MachineRepresentation::kTagged),
      var_then(a, MachineRepresentation::kTagged);

  Label do_enqueue(a), fulfill(a), if_cycle(a, Label::kDeferred),
      if_rejectpromise(a, Label::kDeferred);

  // 6. If SameValue(resolution, promise) is true, then
  a->GotoIf(a->SameValue(promise, result, context), &if_cycle);

  // 7. If Type(resolution) is not Object, then
  a->GotoIf(a->TaggedIsSmi(result), &fulfill);
  a->GotoUnless(a->IsJSReceiver(result), &fulfill);

  Label if_nativepromise(a), if_notnativepromise(a, Label::kDeferred);
  BranchIfFastPath(a, context, result, &if_nativepromise, &if_notnativepromise);

  // Resolution is a native promise and if it's already resolved or
  // rejected, shortcircuit the resolution procedure by directly
  // reusing the value from the promise.
  a->Bind(&if_nativepromise);
  {
    Node* const thenable_status =
        a->LoadObjectField(result, JSPromise::kStatusOffset);
    Node* const thenable_value =
        a->LoadObjectField(result, JSPromise::kResultOffset);

    Label if_isnotpending(a);
    a->GotoUnless(a->SmiEqual(a->SmiConstant(kPromisePending), thenable_status),
                  &if_isnotpending);

    // TODO(gsathya): Use a marker here instead of the actual then
    // callback, and check for the marker in PromiseResolveThenableJob
    // and perform PromiseThen.
    Node* const native_context = a->LoadNativeContext(context);
    Node* const then =
        a->LoadContextElement(native_context, Context::PROMISE_THEN_INDEX);
    var_then.Bind(then);
    a->Goto(&do_enqueue);

    a->Bind(&if_isnotpending);
    {
      Label if_fulfilled(a), if_rejected(a);
      a->Branch(a->SmiEqual(a->SmiConstant(kPromiseFulfilled), thenable_status),
                &if_fulfilled, &if_rejected);

      a->Bind(&if_fulfilled);
      {
        a->CallRuntime(Runtime::kPromiseFulfill, context, promise,
                       a->SmiConstant(kPromiseFulfilled), thenable_value);
        PromiseSetHasHandler(a, promise);
        a->Goto(out);
      }

      a->Bind(&if_rejected);
      {
        Label reject(a);
        Node* const has_handler = PromiseHasHandler(a, result);

        // Promise has already been rejected, but had no handler.
        // Revoke previously triggered reject event.
        a->GotoIf(has_handler, &reject);
        a->CallRuntime(Runtime::kPromiseRevokeReject, context, result);
        a->Goto(&reject);

        a->Bind(&reject);
        // Don't cause a debug event as this case is forwarding a rejection
        a->CallRuntime(Runtime::kPromiseReject, context, promise,
                       thenable_value, a->FalseConstant());
        PromiseSetHasHandler(a, result);
        a->Goto(out);
      }
    }
  }

  a->Bind(&if_notnativepromise);
  {
    // 8. Let then be Get(resolution, "then").
    Node* const then_str = a->HeapConstant(isolate->factory()->then_string());
    Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
    Node* const then =
        a->CallStub(getproperty_callable, context, result, then_str);

    // 9. If then is an abrupt completion, then
    a->GotoIfException(then, &if_rejectpromise, &var_reason);

    // 11. If IsCallable(thenAction) is false, then
    a->GotoIf(a->TaggedIsSmi(then), &fulfill);
    Node* const then_map = a->LoadMap(then);
    a->GotoUnless(a->IsCallableMap(then_map), &fulfill);
    var_then.Bind(then);
    a->Goto(&do_enqueue);
  }

  a->Bind(&do_enqueue);
  {
    Label enqueue(a);
    a->GotoUnless(a->IsDebugActive(), &enqueue);
    a->GotoIf(a->TaggedIsSmi(result), &enqueue);
    a->GotoUnless(a->HasInstanceType(result, JS_PROMISE_TYPE), &enqueue);
    // Mark the dependency of the new promise on the resolution
    Node* const key =
        a->HeapConstant(isolate->factory()->promise_handled_by_symbol());
    a->CallRuntime(Runtime::kSetProperty, context, result, key, promise,
                   a->SmiConstant(STRICT));
    a->Goto(&enqueue);

    // 12. Perform EnqueueJob("PromiseJobs",
    // PromiseResolveThenableJob, « promise, resolution, thenAction
    // »).
    a->Bind(&enqueue);
    a->CallRuntime(Runtime::kEnqueuePromiseResolveThenableJob, context, promise,
                   result, var_then.value());
    a->Goto(out);
  }
  // 7.b Return FulfillPromise(promise, resolution).
  a->Bind(&fulfill);
  {
    a->CallRuntime(Runtime::kPromiseFulfill, context, promise,
                   a->SmiConstant(kPromiseFulfilled), result);
    a->Goto(out);
  }

  a->Bind(&if_cycle);
  {
    // 6.a Let selfResolutionError be a newly created TypeError object.
    Node* const message_id = a->SmiConstant(MessageTemplate::kPromiseCyclic);
    Node* const error =
        a->CallRuntime(Runtime::kNewTypeError, context, message_id, result);
    var_reason.Bind(error);

    // 6.b Return RejectPromise(promise, selfResolutionError).
    a->Goto(&if_rejectpromise);
  }

  // 9.a Return RejectPromise(promise, then.[[Value]]).
  a->Bind(&if_rejectpromise);
  {
    a->CallRuntime(Runtime::kPromiseReject, context, promise,
                   var_reason.value(), a->TrueConstant());
    a->Goto(out);
  }
}

}  // namespace

// ES#sec-promise-resolve-functions
// Promise Resolve Functions
void Builtins::Generate_PromiseResolveClosure(
    compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Node* const value = a.Parameter(1);
  Node* const context = a.Parameter(4);

  Label out(&a);

  // 3. Let alreadyResolved be F.[[AlreadyResolved]].
  Node* const has_already_visited_slot =
      a.IntPtrConstant(PromiseUtils::kAlreadyVisitedSlot);

  Node* const has_already_visited =
      a.LoadFixedArrayElement(context, has_already_visited_slot);

  // 4. If alreadyResolved.[[Value]] is true, return undefined.
  a.GotoIf(a.SmiEqual(has_already_visited, a.SmiConstant(1)), &out);

  // 5.Set alreadyResolved.[[Value]] to true.
  a.StoreFixedArrayElement(context, has_already_visited_slot, a.SmiConstant(1));

  // 2. Let promise be F.[[Promise]].
  Node* const promise = a.LoadFixedArrayElement(
      context, a.IntPtrConstant(PromiseUtils::kPromiseSlot));

  InternalResolvePromise(&a, context, promise, value, &out);

  a.Bind(&out);
  a.Return(a.UndefinedConstant());
}

void Builtins::Generate_ResolvePromise(compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Node* const promise = a.Parameter(1);
  Node* const result = a.Parameter(2);
  Node* const context = a.Parameter(5);

  Label out(&a);
  InternalResolvePromise(&a, context, promise, result, &out);

  a.Bind(&out);
  a.Return(a.UndefinedConstant());
}

void Builtins::Generate_PromiseHandleReject(
    compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;
  typedef PromiseHandleRejectDescriptor Descriptor;

  Node* const promise = a.Parameter(Descriptor::kPromise);
  Node* const on_reject = a.Parameter(Descriptor::kOnReject);
  Node* const exception = a.Parameter(Descriptor::kException);
  Node* const context = a.Parameter(Descriptor::kContext);
  Isolate* isolate = a.isolate();

  Callable call_callable = CodeFactory::Call(isolate);
  Variable var_unused(&a, MachineRepresentation::kTagged);

  Label if_internalhandler(&a), if_customhandler(&a, Label::kDeferred);
  a.Branch(a.IsUndefined(on_reject), &if_internalhandler, &if_customhandler);

  a.Bind(&if_internalhandler);
  {
    a.CallRuntime(Runtime::kPromiseReject, context, promise, exception,
                  a.FalseConstant());
    a.Return(a.UndefinedConstant());
  }

  a.Bind(&if_customhandler);
  {
    a.CallJS(call_callable, context, on_reject, a.UndefinedConstant(),
             exception);
    a.Return(a.UndefinedConstant());
  }
}

void Builtins::Generate_PromiseHandle(compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Node* const value = a.Parameter(2);
  Node* const handler = a.Parameter(3);
  Node* const deferred = a.Parameter(4);
  Node* const context = a.Parameter(7);
  Isolate* isolate = a.isolate();

  // Get promise from deferred
  // TODO(gsathya): Remove this lookup by getting rid of the deferred object.
  Callable getproperty_callable = CodeFactory::GetProperty(isolate);
  Node* const key = a.HeapConstant(isolate->factory()->promise_string());
  Node* const deferred_promise =
      a.CallStub(getproperty_callable, context, deferred, key);

  Variable var_reason(&a, MachineRepresentation::kTagged);

  Node* const is_debug_active = a.IsDebugActive();
  Label run_handler(&a), if_rejectpromise(&a), debug_push(&a, Label::kDeferred),
      debug_pop(&a, Label::kDeferred);
  a.Branch(is_debug_active, &debug_push, &run_handler);

  a.Bind(&debug_push);
  {
    a.CallRuntime(Runtime::kDebugPushPromise, context, deferred_promise);
    a.Goto(&run_handler);
  }

  a.Bind(&run_handler);
  {
    Callable call_callable = CodeFactory::Call(isolate);

    Node* const result =
        a.CallJS(call_callable, context, handler, a.UndefinedConstant(), value);

    a.GotoIfException(result, &if_rejectpromise, &var_reason);

    // TODO(gsathya): Remove this lookup by getting rid of the deferred object.
    Node* const key = a.HeapConstant(isolate->factory()->resolve_string());
    Node* const on_resolve =
        a.CallStub(getproperty_callable, context, deferred, key);

    Label if_internalhandler(&a), if_customhandler(&a, Label::kDeferred);
    a.Branch(a.IsUndefined(on_resolve), &if_internalhandler, &if_customhandler);

    a.Bind(&if_internalhandler);
    InternalResolvePromise(&a, context, deferred_promise, result, &debug_pop);

    a.Bind(&if_customhandler);
    {
      Node* const maybe_exception = a.CallJS(call_callable, context, on_resolve,
                                             a.UndefinedConstant(), result);
      a.GotoIfException(maybe_exception, &if_rejectpromise, &var_reason);
      a.Goto(&debug_pop);
    }
  }

  a.Bind(&if_rejectpromise);
  {
    // TODO(gsathya): Remove this lookup by getting rid of the deferred object.
    Node* const key = a.HeapConstant(isolate->factory()->reject_string());
    Node* const on_reject =
        a.CallStub(getproperty_callable, context, deferred, key);

    Callable promise_handle_reject = CodeFactory::PromiseHandleReject(isolate);
    a.CallStub(promise_handle_reject, context, deferred_promise, on_reject,
               var_reason.value());
    a.Goto(&debug_pop);
  }

  a.Bind(&debug_pop);
  {
    Label out(&a);

    a.GotoUnless(is_debug_active, &out);
    a.CallRuntime(Runtime::kDebugPopPromise, context);
    a.Goto(&out);

    a.Bind(&out);
    a.Return(a.UndefinedConstant());
  }
}

}  // namespace internal
}  // namespace v8
