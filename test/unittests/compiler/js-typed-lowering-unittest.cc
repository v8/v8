// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/js-typed-lowering.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties-inl.h"
#include "src/compiler/typer.h"
#include "test/unittests/compiler/compiler-test-utils.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

const ExternalArrayType kExternalArrayTypes[] = {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype, size) kExternal##Type##Array,
    TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
};


const StrictMode kStrictModes[] = {SLOPPY, STRICT};

}  // namespace


class JSTypedLoweringTest : public GraphTest {
 public:
  JSTypedLoweringTest() : GraphTest(3), javascript_(zone()) {}
  virtual ~JSTypedLoweringTest() {}

 protected:
  Reduction Reduce(Node* node) {
    Typer typer(zone());
    MachineOperatorBuilder machine;
    JSGraph jsgraph(graph(), common(), javascript(), &typer, &machine);
    JSTypedLowering reducer(&jsgraph);
    return reducer.Reduce(node);
  }

  Node* Parameter(Type* type, int index = 0) {
    Node* node = graph()->NewNode(common()->Parameter(index), graph()->start());
    NodeProperties::SetBounds(node, Bounds(Type::None(), type));
    return node;
  }

  Handle<JSArrayBuffer> NewArrayBuffer(void* bytes, size_t byte_length) {
    Handle<JSArrayBuffer> buffer = factory()->NewJSArrayBuffer();
    Runtime::SetupArrayBuffer(isolate(), buffer, true, bytes, byte_length);
    return buffer;
  }

  Matcher<Node*> IsIntPtrConstant(intptr_t value) {
    return sizeof(value) == 4 ? IsInt32Constant(static_cast<int32_t>(value))
                              : IsInt64Constant(static_cast<int64_t>(value));
  }

  JSOperatorBuilder* javascript() { return &javascript_; }

 private:
  JSOperatorBuilder javascript_;
};


// -----------------------------------------------------------------------------
// JSLoadProperty


TEST_F(JSTypedLoweringTest, JSLoadPropertyFromExternalTypedArray) {
  const size_t kLength = 17;
  uint8_t backing_store[kLength * 8];
  Handle<JSArrayBuffer> buffer =
      NewArrayBuffer(backing_store, arraysize(backing_store));
  VectorSlotPair feedback(Handle<TypeFeedbackVector>::null(),
                          FeedbackVectorSlot::Invalid());
  TRACED_FOREACH(ExternalArrayType, type, kExternalArrayTypes) {
    Handle<JSTypedArray> array =
        factory()->NewJSTypedArray(type, buffer, kLength);

    Node* key = Parameter(Type::Integral32());
    Node* base = HeapConstant(array);
    Node* context = UndefinedConstant();
    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* node = graph()->NewNode(javascript()->LoadProperty(feedback), base,
                                  key, context);
    if (FLAG_turbo_deoptimization) {
      node->AppendInput(zone(), UndefinedConstant());
    }
    node->AppendInput(zone(), effect);
    node->AppendInput(zone(), control);
    Reduction r = Reduce(node);

    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsLoadElement(AccessBuilder::ForTypedArrayElement(type, true),
                      IsIntPtrConstant(bit_cast<intptr_t>(&backing_store[0])),
                      key, IsInt32Constant(static_cast<int>(kLength)), effect,
                      control));
  }
}


// -----------------------------------------------------------------------------
// JSStoreProperty


TEST_F(JSTypedLoweringTest, JSStorePropertyToExternalTypedArray) {
  const size_t kLength = 17;
  uint8_t backing_store[kLength * 8];
  Handle<JSArrayBuffer> buffer =
      NewArrayBuffer(backing_store, arraysize(backing_store));
  TRACED_FOREACH(ExternalArrayType, type, kExternalArrayTypes) {
    TRACED_FOREACH(StrictMode, strict_mode, kStrictModes) {
      Handle<JSTypedArray> array =
          factory()->NewJSTypedArray(type, buffer, kLength);

      Node* key = Parameter(Type::Integral32());
      Node* base = HeapConstant(array);
      Node* value = Parameter(Type::Any());
      Node* context = UndefinedConstant();
      Node* effect = graph()->start();
      Node* control = graph()->start();
      Node* node = graph()->NewNode(javascript()->StoreProperty(strict_mode),
                                    base, key, value, context);
      if (FLAG_turbo_deoptimization) {
        node->AppendInput(zone(), UndefinedConstant());
      }
      node->AppendInput(zone(), effect);
      node->AppendInput(zone(), control);
      Reduction r = Reduce(node);

      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsStoreElement(
                      AccessBuilder::ForTypedArrayElement(type, true),
                      IsIntPtrConstant(bit_cast<intptr_t>(&backing_store[0])),
                      key, IsInt32Constant(static_cast<int>(kLength)), value,
                      effect, control));
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
