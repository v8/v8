// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;
typedef CodeStubAssembler::ParameterMode ParameterMode;
typedef compiler::CodeAssemblerState CodeAssemblerState;

class PromiseBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit PromiseBuiltinsAssembler(CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  Node* ThrowIfNotJSReceiver(Node* context, Node* value,
                             MessageTemplate::Template msg_template);

  Node* SpeciesConstructor(Node* context, Node* object,
                           Node* default_constructor);

  Node* PromiseHasHandler(Node* promise);

  void PromiseSetHasHandler(Node* promise);

  void AppendPromiseCallback(int offset, compiler::Node* promise,
                             compiler::Node* value);

  Node* InternalPerformPromiseThen(Node* context, Node* promise,
                                   Node* on_resolve, Node* on_reject,
                                   Node* deferred);

  void InternalResolvePromise(Node* context, Node* promise, Node* result,
                              Label* out);

  void BranchIfFastPath(Node* context, Node* promise, Label* if_isunmodified,
                        Label* if_ismodified);
};

}  // namespace internal
}  // namespace v8
