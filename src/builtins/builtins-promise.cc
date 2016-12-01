// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"

#include "src/code-factory.h"
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
      run_executor(&a), debug_push(&a, Label::kDeferred);
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
    a.Branch(is_debug_active, &debug_push, &run_executor);
  }

  a.Bind(&if_targetismodified);
  {
    Callable fast_new_object_stub = CodeFactory::FastNewObject(isolate);
    Node* const instance =
        a.CallStub(fast_new_object_stub, context, promise_fun, new_target);

    var_result.Bind(instance);
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

    Node* const key = a.LoadRoot(Heap::kpromise_state_symbolRootIndex);
    Node* const value = a.SmiConstant(kPromisePending);
    Node* const language_mode = a.SmiConstant(STRICT);
    // TODO(ishell): Use SetProperty stub once available.
    a.CallRuntime(Runtime::kSetProperty, context, var_result.value(), key,
                  value, language_mode);
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
  a.Return(instance);
}

void Builtins::Generate_IsPromise(compiler::CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Node* const maybe_promise = a.Parameter(1);
  Label if_ispromise(&a), if_isnotpromise(&a, Label::kDeferred);
  a.GotoIf(a.TaggedIsSmi(maybe_promise), &if_isnotpromise);

  a.Branch(a.HasInstanceType(maybe_promise, JS_PROMISE_TYPE), &if_ispromise,
           &if_isnotpromise);

  a.Bind(&if_ispromise);
  a.Return(a.BooleanConstant(true));

  a.Bind(&if_isnotpromise);
  a.Return(a.BooleanConstant(false));
}

}  // namespace internal
}  // namespace v8
