// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_MATH_GEN_H_
#define V8_BUILTINS_BUILTINS_MATH_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

class MathBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit MathBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  TNode<Number> MathPow(TNode<Context> context, TNode<Object> base,
                        TNode<Object> exponent);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_MATH_GEN_H_
