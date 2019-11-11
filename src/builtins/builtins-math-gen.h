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

 protected:
  void MathRoundingOperation(
      TNode<Context> context, TNode<Object> x,
      TNode<Float64T> (CodeStubAssembler::*float64op)(SloppyTNode<Float64T>));
  void MathMaxMin(TNode<Context> context, TNode<Int32T> argc,
                  TNode<Float64T> (CodeStubAssembler::*float64op)(
                      SloppyTNode<Float64T>, SloppyTNode<Float64T>),
                  double default_val);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_MATH_GEN_H_
