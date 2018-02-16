// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"

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

  void AsyncFunctionAwaitResume(Node* const context, Node* const argument,
                                Node* const generator,
                                JSGeneratorObject::ResumeMode resume_mode);
};

void AsyncFunctionBuiltinsAssembler::AsyncFunctionAwaitResume(
    Node* const context, Node* const argument, Node* const generator,
    JSGeneratorObject::ResumeMode resume_mode) {
  CSA_ASSERT(this, IsJSGeneratorObject(generator));
  DCHECK(resume_mode == JSGeneratorObject::kNext ||
         resume_mode == JSGeneratorObject::kThrow);

  // Ensure that the generator is neither closed nor running.
  CSA_SLOW_ASSERT(
      this,
      SmiGreaterThan(
          LoadObjectField(generator, JSGeneratorObject::kContinuationOffset),
          SmiConstant(JSGeneratorObject::kGeneratorClosed)));

  // Remember the {resume_mode} for the {generator}.
  StoreObjectFieldNoWriteBarrier(generator,
                                 JSGeneratorObject::kResumeModeOffset,
                                 SmiConstant(resume_mode));

  // Resume the {receiver} using our trampoline.
  Callable callable = CodeFactory::ResumeGenerator(isolate());
  CallStub(callable, context, argument, generator);

  // The resulting Promise is a throwaway, so it doesn't matter what it
  // resolves to. What is important is that we don't end up keeping the
  // whole chain of intermediate Promises alive by returning the return value
  // of ResumeGenerator, as that would create a memory leak.
}

TF_BUILTIN(AsyncFunctionAwaitFulfill, AsyncFunctionBuiltinsAssembler) {
  Node* const argument = Parameter(Descriptor::kArgument);
  Node* const generator = Parameter(Descriptor::kGenerator);
  Node* const context = Parameter(Descriptor::kContext);
  AsyncFunctionAwaitResume(context, argument, generator,
                           JSGeneratorObject::kNext);
  Return(UndefinedConstant());
}

TF_BUILTIN(AsyncFunctionAwaitReject, AsyncFunctionBuiltinsAssembler) {
  Node* const argument = Parameter(Descriptor::kArgument);
  Node* const generator = Parameter(Descriptor::kGenerator);
  Node* const context = Parameter(Descriptor::kContext);
  AsyncFunctionAwaitResume(context, argument, generator,
                           JSGeneratorObject::kThrow);
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
  CSA_SLOW_ASSERT(this, IsJSGeneratorObject(generator));
  CSA_SLOW_ASSERT(this, IsJSPromise(outer_promise));

  Node* const native_context = LoadNativeContext(context);
  Node* const promise = AllocateAndInitJSPromise(native_context);

  Node* const promise_reactions =
      LoadObjectField(promise, JSPromise::kReactionsOrResultOffset);
  Node* const fulfill_handler = HeapConstant(
      Builtins::CallableFor(isolate(), Builtins::kAsyncFunctionAwaitFulfill)
          .code());
  Node* const reject_handler = HeapConstant(
      Builtins::CallableFor(isolate(), Builtins::kAsyncFunctionAwaitReject)
          .code());
  Node* const reaction = AllocatePromiseReaction(
      promise_reactions, generator, fulfill_handler, reject_handler);
  StoreObjectField(promise, JSPromise::kReactionsOrResultOffset, reaction);
  PromiseSetHasHandler(promise);

  // Perform ! Call(promiseCapability.[[Resolve]], undefined, « value »).
  CallBuiltin(Builtins::kResolvePromise, native_context, promise, awaited);

  {
    Label done(this);
    GotoIfNot(IsDebugActive(), &done);
    CallRuntime(Runtime::kSetProperty, native_context, generator,
                LoadRoot(Heap::kgenerator_outer_promise_symbolRootIndex),
                outer_promise, SmiConstant(LanguageMode::kStrict));
    if (is_predicted_as_caught) {
      GotoIf(TaggedIsSmi(awaited), &done);
      GotoIfNot(IsJSPromise(awaited), &done);
      PromiseSetHandledHint(awaited);
    }
    Goto(&done);
    BIND(&done);
  }

  // Return outer promise to avoid adding an load of the outer promise before
  // suspending in BytecodeGenerator.
  Return(outer_promise);
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
    // Assign ID and create a recurring task to save stack for future
    // resumptions from await.
    CallRuntime(Runtime::kDebugAsyncFunctionPromiseCreated, context, promise);
    Return(promise);
  }
}

TF_BUILTIN(AsyncFunctionPromiseRelease, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 1);
  Node* const promise = Parameter(Descriptor::kPromise);
  Node* const context = Parameter(Descriptor::kContext);

  Label if_is_debug_active(this, Label::kDeferred);
  GotoIf(IsDebugActive(), &if_is_debug_active);

  // Early exit if debug is not active.
  Return(UndefinedConstant());

  BIND(&if_is_debug_active);
  {
    // Pop the Promise under construction in an async function on
    // from catch prediction stack.
    CallRuntime(Runtime::kDebugPopPromise, context);
    Return(promise);
  }
}

}  // namespace internal
}  // namespace v8
