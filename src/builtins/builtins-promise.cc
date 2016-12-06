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

// ES#sec-promise-resolve-functions
// Promise Resolve Functions
BUILTIN(PromiseResolveClosure) {
  HandleScope scope(isolate);

  Handle<Context> context(isolate->context(), isolate);

  if (PromiseUtils::HasAlreadyVisited(context)) {
    return isolate->heap()->undefined_value();
  }

  PromiseUtils::SetAlreadyVisited(context);
  Handle<JSObject> promise = handle(PromiseUtils::GetPromise(context), isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);

  MaybeHandle<Object> maybe_result;
  Handle<Object> argv[] = {promise, value};
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, Execution::Call(isolate, isolate->promise_resolve(),
                               isolate->factory()->undefined_value(),
                               arraysize(argv), argv));
  return isolate->heap()->undefined_value();
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

void PromiseInit(CodeStubAssembler* a, compiler::Node* promise,
                 compiler::Node* status, compiler::Node* result) {
  CSA_ASSERT(a, a->TaggedIsSmi(status));
  a->StoreObjectField(promise, JSPromise::kStatusOffset, status);
  a->StoreObjectField(promise, JSPromise::kResultOffset, result);
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
    Node* const initial_map = a.LoadObjectField(
        promise_fun, JSFunction::kPrototypeOrInitialMapOffset);

    Node* const instance = a.AllocateJSObjectFromMap(initial_map);
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
    PromiseInit(&a, var_result.value(), a.SmiConstant(kPromisePending),
                a.UndefinedConstant());
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
  Node* const native_context = a.LoadNativeContext(context);
  Node* const promise_fun =
      a.LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const initial_map =
      a.LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const instance = a.AllocateJSObjectFromMap(initial_map);

  PromiseInit(&a, instance, a.SmiConstant(kPromisePending),
              a.UndefinedConstant());
  a.Return(instance);
}

void Builtins::Generate_PromiseCreateAndSet(
    compiler::CodeAssemblerState* state) {
  typedef compiler::Node Node;
  CodeStubAssembler a(state);

  Node* const status = a.Parameter(1);
  Node* const result = a.Parameter(2);
  Node* const context = a.Parameter(5);
  Node* const native_context = a.LoadNativeContext(context);

  Node* const promise_fun =
      a.LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const initial_map =
      a.LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const instance = a.AllocateJSObjectFromMap(initial_map);

  PromiseInit(&a, instance, status, result);
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
  length = a->UntagParameter(length, mode);

  Node* delta = a->IntPtrOrSmiConstant(1, mode);
  Node* new_capacity = a->IntPtrAdd(length, delta);
  ElementsKind kind = FAST_ELEMENTS;

  Node* new_elements = a->AllocateFixedArray(kind, new_capacity, mode);

  a->CopyFixedArrayElements(kind, elements, new_elements, length,
                            UPDATE_WRITE_BARRIER, mode);
  a->StoreFixedArrayElement(new_elements, length, value, UPDATE_WRITE_BARRIER,
                            0, mode);

  a->StoreObjectField(promise, offset, new_elements);
}

void InternalPerformPromiseThen(CodeStubAssembler* a, compiler::Node* context,
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
        // Create new FixedArrays to store callbacks.
        Node* const deferreds =
            a->AllocateFixedArray(FAST_ELEMENTS, a->Int32Constant(2));
        Node* const fulfill_reactions =
            a->AllocateFixedArray(FAST_ELEMENTS, a->Int32Constant(2));
        Node* const reject_reactions =
            a->AllocateFixedArray(FAST_ELEMENTS, a->Int32Constant(2));

        // Store existing callbacks in FixedArrays.
        a->StoreFixedArrayElement(deferreds, 0, existing_deferred);
        a->StoreFixedArrayElement(
            fulfill_reactions, 0,
            a->LoadObjectField(promise, JSPromise::kFulfillReactionsOffset));
        a->StoreFixedArrayElement(
            reject_reactions, 0,
            a->LoadObjectField(promise, JSPromise::kRejectReactionsOffset));

        // Store new callbacks in FixedArrays.
        a->StoreFixedArrayElement(deferreds, 1, deferred);
        a->StoreFixedArrayElement(fulfill_reactions, 1, var_on_resolve.value());
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
      a->CallRuntime(Runtime::kEnqueuePromiseReactionJob, context, result,
                     var_on_resolve.value(), deferred,
                     a->SmiConstant(kPromiseFulfilled));
      a->Goto(&out);

      a->Bind(&reject);
      {
        Callable getproperty_callable = CodeFactory::GetProperty(isolate);
        Node* const key =
            a->HeapConstant(isolate->factory()->promise_has_handler_symbol());
        Node* const has_handler =
            a->CallStub(getproperty_callable, context, promise, key);

        Label enqueue(a);

        // TODO(gsathya): Fold these runtime calls and move to TF.
        a->GotoIf(a->WordEqual(has_handler, a->TrueConstant()), &enqueue);
        a->CallRuntime(Runtime::kPromiseRevokeReject, context, promise);
        a->Goto(&enqueue);

        a->Bind(&enqueue);
        {
          a->CallRuntime(Runtime::kEnqueuePromiseReactionJob, context, result,
                         var_on_reject.value(), deferred,
                         a->SmiConstant(kPromiseRejected));

          a->Goto(&out);
        }
      }
    }
  }

  a->Bind(&out);
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

  InternalPerformPromiseThen(&a, context, promise, on_resolve, on_reject,
                             deferred);

  // TODO(gsathya): This is unused, but value is returned according to spec.
  a.Return(promise);
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
  InternalPerformPromiseThen(&a, context, promise, on_resolve, on_reject,
                             var_deferred.value());

  // TODO(gsathya): Protect with debug check.
  a.CallRuntime(
      Runtime::kSetProperty, context, promise,
      a.HeapConstant(isolate->factory()->promise_has_handler_symbol()),
      a.TrueConstant(), a.SmiConstant(STRICT));

  // TODO(gsathya): This call will be removed once we don't have to
  // deal with deferred objects.
  Callable getproperty_callable = CodeFactory::GetProperty(isolate);
  Node* const key =
      a.HeapConstant(isolate->factory()->NewStringFromAsciiChecked("promise"));
  Node* const result =
      a.CallStub(getproperty_callable, context, var_deferred.value(), key);

  a.Return(result);
}

}  // namespace internal
}  // namespace v8
