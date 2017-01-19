// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-promise.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;
typedef CodeStubAssembler::ParameterMode ParameterMode;
typedef compiler::CodeAssemblerState CodeAssemblerState;

class AsyncFunctionBuiltinsAssembler : public PromiseBuiltinsAssembler {
 public:
  explicit AsyncFunctionBuiltinsAssembler(CodeAssemblerState* state)
      : PromiseBuiltinsAssembler(state) {}
};

TF_BUILTIN(AsyncFunctionPromiseCreate, AsyncFunctionBuiltinsAssembler) {
  CSA_ASSERT_JS_ARGC_EQ(this, 0);
  Node* const context = Parameter(3);

  Node* const promise = AllocateAndInitJSPromise(context);

  Label if_is_debug_active(this, Label::kDeferred);
  GotoIf(IsDebugActive(), &if_is_debug_active);

  // Early exit if debug is not active.
  Return(promise);

  Bind(&if_is_debug_active);
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
  Node* const promise = Parameter(1);
  Node* const context = Parameter(4);

  Label if_is_debug_active(this, Label::kDeferred);
  GotoIf(IsDebugActive(), &if_is_debug_active);

  // Early exit if debug is not active.
  Return(UndefinedConstant());

  Bind(&if_is_debug_active);
  {
    // Pop the Promise under construction in an async function on
    // from catch prediction stack.
    CallRuntime(Runtime::kDebugPopPromise, context);
    Return(promise);
  }
}

}  // namespace internal
}  // namespace v8
