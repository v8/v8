// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-unittest.h"
#include "src/compiler/js-builtin-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-properties-inl.h"
#include "src/compiler/typer.h"

namespace v8 {
namespace internal {
namespace compiler {

class JSBuiltinReducerTest : public GraphTest {
 public:
  JSBuiltinReducerTest() : javascript_(zone()) {}

 protected:
  Reduction Reduce(Node* node) {
    Typer typer(zone());
    MachineOperatorBuilder machine;
    JSGraph jsgraph(graph(), common(), javascript(), &typer, &machine);
    JSBuiltinReducer reducer(&jsgraph);
    return reducer.Reduce(node);
  }

  Node* Parameter(Type* t, int32_t index = 0) {
    Node* n = graph()->NewNode(common()->Parameter(index), graph()->start());
    NodeProperties::SetBounds(n, Bounds(Type::None(), t));
    return n;
  }

  Node* UndefinedConstant() {
    return HeapConstant(
        Unique<HeapObject>::CreateImmovable(factory()->undefined_value()));
  }

  JSOperatorBuilder* javascript() { return &javascript_; }

 private:
  JSOperatorBuilder javascript_;
};


namespace {

// TODO(mstarzinger): Find a common place and unify with test-js-typed-lowering.
Type* const kNumberTypes[] = {
    Type::UnsignedSmall(),   Type::OtherSignedSmall(), Type::OtherUnsigned31(),
    Type::OtherUnsigned32(), Type::OtherSigned32(),    Type::SignedSmall(),
    Type::Signed32(),        Type::Unsigned32(),       Type::Integral32(),
    Type::MinusZero(),       Type::NaN(),              Type::OtherNumber(),
    Type::OrderedNumber(),   Type::Number()};

}  // namespace


// -----------------------------------------------------------------------------
// Math.imul


TEST_F(JSBuiltinReducerTest, MathImul) {
  Handle<JSFunction> f(isolate()->context()->math_imul_fun());

  TRACED_FOREACH(Type*, t0, kNumberTypes) {
    TRACED_FOREACH(Type*, t1, kNumberTypes) {
      Node* p0 = Parameter(t0, 0);
      Node* p1 = Parameter(t1, 1);
      Node* fun = HeapConstant(Unique<HeapObject>::CreateUninitialized(f));
      Node* call =
          graph()->NewNode(javascript()->Call(4, NO_CALL_FUNCTION_FLAGS), fun,
                           UndefinedConstant(), p0, p1);
      Reduction r = Reduce(call);

      if (t0->Is(Type::Integral32()) && t1->Is(Type::Integral32())) {
        EXPECT_TRUE(r.Changed());
        EXPECT_THAT(r.replacement(), IsInt32Mul(p0, p1));
      } else {
        EXPECT_FALSE(r.Changed());
        EXPECT_EQ(IrOpcode::kJSCallFunction, call->opcode());
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
