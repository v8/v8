// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;
typedef compiler::CodeAssemblerState CodeAssemblerState;

class ConstructorBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit ConstructorBuiltinsAssembler(CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  Node* EmitFastNewClosure(Node* shared_info, Node* context);
  Node* EmitFastNewFunctionContext(Node* closure, Node* slots, Node* context,
                                   ScopeType scope_type);
  static int MaximumFunctionContextSlots();

 private:
  static const int kMaximumSlots = 0x8000;
  static const int kSmallMaximumSlots = 10;

  // FastNewFunctionContext can only allocate closures which fit in the
  // new space.
  STATIC_ASSERT(((kMaximumSlots + Context::MIN_CONTEXT_SLOTS) * kPointerSize +
                 FixedArray::kHeaderSize) < kMaxRegularHeapObjectSize);
};

}  // namespace internal
}  // namespace v8
