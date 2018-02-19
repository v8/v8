// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-call-reducer.h"
#include "src/compilation-dependencies.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/simplified-operator.h"
#include "src/isolate.h"
#include "test/unittests/compiler/graph-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

class JSCallReducerTest : public TypedGraphTest {
 public:
  JSCallReducerTest()
      : TypedGraphTest(3), javascript_(zone()), deps_(isolate(), zone()) {}
  ~JSCallReducerTest() override {}

 protected:
  Reduction Reduce(Node* node) {
    MachineOperatorBuilder machine(zone());
    SimplifiedOperatorBuilder simplified(zone());
    JSGraph jsgraph(isolate(), graph(), common(), javascript(), &simplified,
                    &machine);
    // TODO(titzer): mock the GraphReducer here for better unit testing.
    GraphReducer graph_reducer(zone(), graph());

    JSCallReducer reducer(&graph_reducer, &jsgraph, JSCallReducer::kNoFlags,
                          native_context(), &deps_);
    return reducer.Reduce(node);
  }

  JSOperatorBuilder* javascript() { return &javascript_; }

 private:
  JSOperatorBuilder javascript_;
  CompilationDependencies deps_;
};

TEST_F(JSCallReducerTest, PromiseConstructorNoArgs) {
  Node* promise = HeapConstant(handle(native_context()->promise_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();

  Node* construct =
      graph()->NewNode(javascript()->Construct(2), promise, promise, context,
                       frame_state, effect, control);

  Reduction r = Reduce(construct);

  ASSERT_FALSE(r.Changed());
}

TEST_F(JSCallReducerTest, PromiseConstructorSubclass) {
  Node* promise = HeapConstant(handle(native_context()->promise_function()));
  Node* new_target = HeapConstant(handle(native_context()->array_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();

  Node* executor = UndefinedConstant();
  Node* construct =
      graph()->NewNode(javascript()->Construct(3), promise, executor,
                       new_target, context, frame_state, effect, control);

  Reduction r = Reduce(construct);

  ASSERT_FALSE(r.Changed());
}

TEST_F(JSCallReducerTest, PromiseConstructorBasic) {
  Node* promise = HeapConstant(handle(native_context()->promise_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();

  Node* executor = UndefinedConstant();
  Node* construct =
      graph()->NewNode(javascript()->Construct(3), promise, executor, promise,
                       context, frame_state, effect, control);

  Reduction r = Reduce(construct);

  if (FLAG_experimental_inline_promise_constructor) {
    ASSERT_TRUE(r.Changed());
  } else {
    ASSERT_FALSE(r.Changed());
  }
}

// Exactly the same as PromiseConstructorBasic which expects a reduction, except
// that we invalidate the protector cell.
TEST_F(JSCallReducerTest, PromiseConstructorWithHook) {
  Node* promise = HeapConstant(handle(native_context()->promise_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();

  Node* executor = UndefinedConstant();
  Node* construct =
      graph()->NewNode(javascript()->Construct(3), promise, executor, promise,
                       context, frame_state, effect, control);

  isolate()->InvalidatePromiseHookProtector();

  Reduction r = Reduce(construct);

  ASSERT_FALSE(r.Changed());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
