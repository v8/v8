// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_PROMISE_GEN_H_
#define V8_BUILTINS_BUILTINS_PROMISE_GEN_H_

#include "src/codegen/code-stub-assembler.h"
#include "src/objects/promise.h"

namespace v8 {
namespace internal {

using CodeAssemblerState = compiler::CodeAssemblerState;

class V8_EXPORT_PRIVATE PromiseBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit PromiseBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}
  // These allocate and initialize a promise with pending state and
  // undefined fields.
  //
  // This uses undefined as the parent promise for the promise init
  // hook.
  TNode<JSPromise> AllocateAndInitJSPromise(TNode<Context> context);
  // This uses the given parent as the parent promise for the promise
  // init hook.
  TNode<JSPromise> AllocateAndInitJSPromise(TNode<Context> context,
                                            TNode<Object> parent);

  // This allocates and initializes a promise with the given state and
  // fields.
  TNode<JSPromise> AllocateAndSetJSPromise(TNode<Context> context,
                                           v8::Promise::PromiseState status,
                                           TNode<Object> result);

  TNode<PromiseReaction> AllocatePromiseReaction(
      TNode<Object> next, TNode<HeapObject> promise_or_capability,
      TNode<HeapObject> fulfill_handler, TNode<HeapObject> reject_handler);

  TNode<PromiseReactionJobTask> AllocatePromiseReactionJobTask(
      TNode<Map> map, TNode<Context> context, TNode<Object> argument,
      TNode<HeapObject> handler, TNode<HeapObject> promise_or_capability);

  TNode<PromiseResolveThenableJobTask> AllocatePromiseResolveThenableJobTask(
      TNode<JSPromise> promise_to_resolve, TNode<JSReceiver> then,
      TNode<JSReceiver> thenable, TNode<Context> context);
  TNode<BoolT> PromiseHasHandler(TNode<JSPromise> promise);

  void BranchIfAccessCheckFailed(TNode<Context> context,
                                 TNode<Context> native_context,
                                 TNode<Object> promise_constructor,
                                 TNode<Object> executor, Label* if_noaccess);
  void PromiseInit(TNode<JSPromise> promise);

  // We can shortcut the SpeciesConstructor on {promise_map} if it's
  // [[Prototype]] is the (initial)  Promise.prototype and the @@species
  // protector is intact, as that guards the lookup path for the "constructor"
  // property on JSPromise instances which have the %PromisePrototype%.
  void BranchIfPromiseSpeciesLookupChainIntact(
      TNode<NativeContext> native_context, TNode<Map> promise_map,
      Label* if_fast, Label* if_slow);

  template <typename... TArgs>
  TNode<Object> InvokeThen(TNode<NativeContext> native_context,
                           TNode<Object> receiver, TArgs... args) {
    TVARIABLE(Object, var_result);
    Label if_fast(this), if_slow(this, Label::kDeferred),
        done(this, &var_result);
    GotoIf(TaggedIsSmi(receiver), &if_slow);
    const TNode<Map> receiver_map = LoadMap(CAST(receiver));
    // We can skip the "then" lookup on {receiver} if it's [[Prototype]]
    // is the (initial) Promise.prototype and the Promise#then protector
    // is intact, as that guards the lookup path for the "then" property
    // on JSPromise instances which have the (initial) %PromisePrototype%.
    BranchIfPromiseThenLookupChainIntact(native_context, receiver_map, &if_fast,
                                         &if_slow);

    BIND(&if_fast);
    {
      const TNode<Object> then =
          LoadContextElement(native_context, Context::PROMISE_THEN_INDEX);
      var_result =
          CallJS(CodeFactory::CallFunction(
                     isolate(), ConvertReceiverMode::kNotNullOrUndefined),
                 native_context, then, receiver, args...);
      Goto(&done);
    }

    BIND(&if_slow);
    {
      const TNode<Object> then = GetProperty(
          native_context, receiver, isolate()->factory()->then_string());
      var_result =
          CallJS(CodeFactory::Call(isolate(),
                                   ConvertReceiverMode::kNotNullOrUndefined),
                 native_context, then, receiver, args...);
      Goto(&done);
    }

    BIND(&done);
    return var_result.value();
  }

 protected:
  // We can skip the "resolve" lookup on {constructor} if it's the (initial)
  // Promise constructor and the Promise.resolve() protector is intact, as
  // that guards the lookup path for the "resolve" property on the %Promise%
  // intrinsic object.
  void BranchIfPromiseResolveLookupChainIntact(
      TNode<NativeContext> native_context, TNode<Object> constructor,
      Label* if_fast, Label* if_slow);
  void GotoIfNotPromiseResolveLookupChainIntact(
      TNode<NativeContext> native_context, TNode<Object> constructor,
      Label* if_slow);

  // We can skip the "then" lookup on {receiver_map} if it's [[Prototype]]
  // is the (initial) Promise.prototype and the Promise#then() protector
  // is intact, as that guards the lookup path for the "then" property
  // on JSPromise instances which have the (initial) %PromisePrototype%.
  void BranchIfPromiseThenLookupChainIntact(TNode<NativeContext> native_context,
                                            TNode<Map> receiver_map,
                                            Label* if_fast, Label* if_slow);

  TNode<JSPromise> AllocateJSPromise(TNode<Context> context);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_PROMISE_GEN_H_
